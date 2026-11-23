#!/bin/sh
# Everything that can be verified without the device: both builds with
# warnings as errors, the unit tests under ASan and UBSan, the test sessions,
# and every main screen at both resolutions through the real hit-testing.
#
#   tools/check.sh [--quick]        --quick leaves out valgrind
set -e
cd "$(dirname "$0")/.."
FONT=$PWD/data/fonts/LXGWWenKaiLite-Regular.ttf
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
say() { printf '\n== %s\n' "$1"; }

[ -f out/content.db ] || python3 build/build_db.py

say "build (warnings are errors)"
STRICT="-O2 -std=gnu++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation"
rm -f out/hanzi out/hanzi-kobo          # rebuilt, or an old binary would hide a warning
make CXXFLAGS="$STRICT" >/dev/null
echo "    desktop ok"
if command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
    if make kobo CXXFLAGS="$STRICT" >"$TMP/kobo.log" 2>&1; then echo "    kobo ok"
    else echo "    FAIL: the kobo build"; tail -20 "$TMP/kobo.log"; fail=1; fi
else
    echo "    kobo skipped (no cross-compiler)"
fi

say "unit tests (AddressSanitizer + UBSan)"
DEPS=$(printf 'p:\n\t@echo $(DEPS_DIR)\n' | make -s --no-print-directory -f Makefile -f - p)/host
SAN="-O1 -g -std=gnu++17 -fsanitize=address,undefined"
DB="src/core/db.cpp src/core/srs.cpp out/sqlite3.o"
t() {           # t NAME "SOURCES AND LIBRARIES" ARGUMENTS...
    name=$1; src=$2; shift 2
    # shellcheck disable=SC2086
    g++ $SAN -isystem "$DEPS/include" -o "$TMP/$name" "tests/$name.cpp" $src -lm
    if (cd out && TMPDIR="$TMP" "$TMP/$name" "$@") >"$TMP/$name.log" 2>&1; then echo "    $name ok"
    else echo "    FAIL: $name"; grep -vE '^(db|pdf):' "$TMP/$name.log" | tail -15; fail=1; fi
}
t srs_test   src/core/srs.cpp
t utf8_test  "src/core/gfx.cpp src/ui/ui.cpp" "$FONT"
t mt_test    src/backend/mtstate.cpp
t ink_test   "src/core/gfx.cpp src/ui/ui.cpp" "$FONT"
t light_test ""
t rot_test   ""
t pdf_test   "src/core/pdf.cpp $DEPS/lib/libmupdf.a $DEPS/lib/libmupdf-third.a" ../tests/data "$FONT"
t dict_test  "$DB" content.db "$TMP/dict.db"
t settings_test "$DB" content.db "$TMP/set.db"
t userdb_test   "$DB" content.db "$TMP/userdb.db" "$TMP/pleco"
SAN="-O2 -g -std=gnu++17"               # thousands of characters replayed: no sanitiser
t match_test "src/core/match.cpp $DB" content.db
t hw_test    "$DB" content.db
if sh tests/run_sh_test.sh >"$TMP/run.log" 2>&1; then echo "    run_sh_test ok"
else echo "    FAIL: run_sh_test"; tail -15 "$TMP/run.log"; fail=1; fi

for mode in "" --clara; do
    say "sessions and screens, ${mode:-libra}"
    (cd out && ./hanzi $mode --font "$FONT" --user "$TMP/s$mode.db" --selftest) >"$TMP/self.log" 2>&1 \
        || { echo "    FAIL: selftest"; grep FAIL "$TMP/self.log"; fail=1; }
    (cd out && ./hanzi $mode --font "$FONT" --user "$TMP/r$mode.db" --shot "$TMP") >"$TMP/shot.log" 2>&1 || fail=1
    n=$(grep -c FAIL "$TMP/shot.log" || true)
    echo "    selftest run; screens: $n failures"
    [ "$n" = 0 ] || { grep FAIL "$TMP/shot.log"; fail=1; }
done

if [ "${1:-}" != --quick ] && command -v valgrind >/dev/null 2>&1; then
    say valgrind
    (cd out && valgrind -q --leak-check=full --error-exitcode=9 ./hanzi --font "$FONT" \
        --user "$TMP/v.db" --shot "$TMP" >/dev/null 2>&1) && echo "    clean" || { echo "    FAIL"; fail=1; }
fi
[ "$fail" = 0 ] && printf '\nall checks passed\n' || printf '\nFAILURES\n'
exit $fail
