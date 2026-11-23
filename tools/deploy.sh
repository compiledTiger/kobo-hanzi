#!/bin/sh
# Copy the app to a plugged-in Kobo. Builds first and stops on any failure,
# so a broken build can never leave a stale binary on the device -- which has
# happened, because a build error looked like a warning in a pipeline.
#
# Every file goes over as NAME.new and is read back from the device and
# checked, and only when all of them have been is any renamed into place: an
# interrupted copy (a pulled cable, a full card) leaves the working files
# untouched -- above all run.sh, which stops Nickel and, cut short, would
# never start it again -- and never a new database beside an old binary.
set -e
cd "$(dirname "$0")/.."

K=${1:-/media/$USER/KOBOeReader}
H="$K/.adds/hanzi"
[ -d "$K/.adds" ] || { echo "no Kobo at $K"; exit 1; }

mkdir -p "$H" "$K/.adds/nm"

echo "building ..."
make kobo >/dev/null

# The OCR models (a press on a PDF page reads it with them): the app looks
# for models/ beside content.db.
MODELS="ppocrv4_det.param ppocrv4_det.bin ppocrv4_rec.param ppocrv4_rec.bin ppocrv4_keys.txt"

# Anything left half-copied, by this run or an earlier one that failed.
cleanup() {
    rm -f "$H/content.db.new" "$H/hanzi.new" "$H/run.sh.new" "$H/LXGWWenKaiLite-Regular.ttf.new" "$K/.adds/nm/hanzi.new"
    for m in $MODELS; do rm -f "$H/models/$m.new"; done
}
trap 'cleanup' EXIT

# The font goes over only where the device's differs or is missing. Asked
# once, here, because the answer also decides how much room is needed.
font_new=0
cmp -s data/fonts/LXGWWenKaiLite-Regular.ttf "$H/LXGWWenKaiLite-Regular.ttf" 2>/dev/null || font_new=1
# Likewise each model file (about 15 MB together).
models_new=
for m in $MODELS; do
    cmp -s "data/models/$m" "$H/models/$m" 2>/dev/null || models_new="$models_new $m"
done

# Room first. Each new file is written beside the old one, so both exist at
# once: what is free must hold all of the new ones -- the database, the
# binary, and the font (12 MB) when it is going over -- and a leftover .new
# from a failed run is space we get back. Refusing halfway would leave a new
# binary against an old database, the one combination neither was built for.
need=$(( $(stat -c %s out/hanzi-kobo) / 1024 + 4096 ))
[ ! -f out/content.db ] || need=$(( need + $(stat -c %s out/content.db) / 1024 ))
[ "$font_new" = 0 ] || need=$(( need + $(stat -c %s data/fonts/LXGWWenKaiLite-Regular.ttf) / 1024 ))
for m in $models_new; do need=$(( need + $(stat -c %s "data/models/$m") / 1024 )); done
have=$(df -Pk "$H" | awk 'NR==2 {print $4}')
for f in "$H/content.db.new" "$H/hanzi.new" "$H/run.sh.new" "$H/LXGWWenKaiLite-Regular.ttf.new" \
         "$K/.adds/nm/hanzi.new" $(for m in $MODELS; do echo "$H/models/$m.new"; done); do
    [ ! -f "$f" ] || have=$(( have + $(stat -c %s "$f") / 1024 ))
done
if [ "$have" -lt "$need" ]; then
    echo "not enough room on the Kobo: need ${need}KB, free ${have}KB"
    echo "nothing has been copied"
    exit 1
fi

# What arrived, read back from the device itself: md5sum alone reads the
# host's page cache, which says what was sent, not what was written. Direct
# I/O where the filesystem allows it; where it does not, dd reads nothing and
# this prints the checksum of nothing, which stage() below catches. (A fallback
# after the pipeline never ran: a pipeline's status is its last command's,
# and cut does not fail.)
EMPTY_MD5=d41d8cd98f00b204e9800998ecf8427e
device_md5() {
    dd if="$1" iflag=direct bs=1M 2>/dev/null | md5sum | cut -d' ' -f1
}

# stage SRC DEST: copy as DEST.new, flush, verify. Nothing is renamed here:
# a failure later, in another file, must find the device as it was.
stage() {
    cp "$1" "$2.new"
    sync "$2.new" 2>/dev/null || sync     # sync FILE reports a failed write
    a=$(md5sum "$1" | cut -d' ' -f1)
    b=$(device_md5 "$2.new")
    if [ -z "$b" ] || { [ "$b" = "$EMPTY_MD5" ] && [ -s "$1" ]; }; then
        echo "note: no direct read here; $(basename "$2") checked through the page cache"
        b=$(md5sum "$2.new" | cut -d' ' -f1)
    fi
    if [ "$a" != "$b" ]; then
        echo "VERIFY FAILED: $(basename "$2") did not copy cleanly; nothing on"
        echo "the device has been replaced"
        exit 1
    fi
    echo "$(basename "$2") verified: $a"
}

# The binary and the database are left in deploy/.adds/hanzi too, beside the
# tracked run.sh, so what is there is never older than what went to the
# device -- a stale content.db once sat in it for days. (They are gitignored;
# a fresh clone has neither, which is why the README copies from out/.)
cp -a out/hanzi-kobo deploy/.adds/hanzi/hanzi
[ -f out/content.db ] && cp -a out/content.db deploy/.adds/hanzi/content.db

if [ -f out/content.db ]; then
    echo "copying content.db ($(( $(stat -c %s out/content.db) / 1048576 ))MB) ..."
    stage out/content.db "$H/content.db"
else
    echo "note: no out/content.db -- run build/build_db.py; leaving the one on the device"
fi
stage out/hanzi-kobo "$H/hanzi"
stage deploy/.adds/hanzi/run.sh "$H/run.sh"
stage deploy/.adds/nm/hanzi "$K/.adds/nm/hanzi"
# the font, from the repository, where the device's differs or is missing
if [ "$font_new" = 1 ]; then
    stage data/fonts/LXGWWenKaiLite-Regular.ttf "$H/LXGWWenKaiLite-Regular.ttf"
fi
# the OCR models, likewise only where they differ
[ -z "$models_new" ] || mkdir -p "$H/models"
for m in $models_new; do stage "data/models/$m" "$H/models/$m"; done

# Every file is there and checked: now into place, one rename each, the
# database first -- until it is in place, the old binary and the old
# database still match each other.
[ ! -f out/content.db ] || mv "$H/content.db.new" "$H/content.db"
mv "$H/hanzi.new" "$H/hanzi"
chmod +x "$H/hanzi" 2>/dev/null || true
mv "$H/run.sh.new" "$H/run.sh"
mv "$K/.adds/nm/hanzi.new" "$K/.adds/nm/hanzi"
[ "$font_new" = 0 ] || mv "$H/LXGWWenKaiLite-Regular.ttf.new" "$H/LXGWWenKaiLite-Regular.ttf"
for m in $models_new; do mv "$H/models/$m.new" "$H/models/$m"; done

# Keep the previous runs' logs instead of deleting them: they are the only
# evidence of what happened on the device. hanzi-prev.log is what run.sh
# rotated there itself, and moving hanzi.log onto it would lose it.
# It is often a log already kept: the hanzi.log this script moved there last
# time, unchanged since. Archived only when no copy in logs/ is identical --
# six identical copies of one piled up otherwise.
mkdir -p logs
ts=$(date +%Y%m%d-%H%M%S)
if [ -s "$H/hanzi-prev.log" ]; then
    kept=
    for f in logs/*.log; do
        if cmp -s "$H/hanzi-prev.log" "$f"; then kept=$f; break; fi
    done
    if [ -z "$kept" ]; then
        cp "$H/hanzi-prev.log" "logs/hanzi-prev-$ts.log"
        echo "the device's hanzi-prev.log archived to logs/hanzi-prev-$ts.log"
    fi
fi
if [ -s "$H/hanzi.log" ]; then
    cp "$H/hanzi.log" "logs/hanzi-$ts.log"
    mv "$H/hanzi.log" "$H/hanzi-prev.log"
    echo "the device's log archived to logs/hanzi-$ts.log; on the device it is now hanzi-prev.log"
fi
sync

echo "deployed and verified; eject and run"
