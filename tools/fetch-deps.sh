#!/bin/sh
# Put back third-party code and data the build needs, if any of it is missing.
# All of it is committed (src/vendor, build/data), so a fresh clone needs none
# of this; it is how each piece got there, and it never replaces a piece that
# is present -- delete one to fetch it again. Their licences are in
# THIRD-PARTY-NOTICES.md.
#
#   tools/fetch-deps.sh              all of it, and the MuPDF and ncnn sources
#   tools/fetch-deps.sh deps TARGET  build MuPDF and ncnn as static libraries
#                                    for TARGET -- host, arm (the Kobo) or
#                                    appimage (only inside its glibc 2.28
#                                    container: tools/appimage.sh) -- unless
#                                    already built as configured now
#   tools/fetch-deps.sh stamp TARGET the file that says so (mk/deps.mk)
#
# MuPDF and ncnn are the exception to "all of it is committed": with their
# builds they are about 1 GB, too much for the repository and for the root
# disk. Their sources, builds and installs live on the build drive,
# $HANZI_BUILD: sources in src/, object trees in obj/TARGET, and the
# libraries and headers the app links in $DEPS_DIR/TARGET/{include,lib}
# (mk/deps.mk, `make deps`, `make deps-arm`). Every download is checked
# against the sha256 pinned below. Building them needs the network (MuPDF,
# ncnn and, where there is none, CMake are downloaded), curl, unzip, and
# HANZI_BUILD set to a writable folder with about 2 GB free wherever the
# default below (this machine's build drive) is not there.
#
# A build is named by its configuration: the versions, every option below
# and the compiler, hashed (config_id). The stamp and the object tree carry
# that name, so changing any of them -- a version, a -D, MUPDF_XCFLAGS, the
# compiler -- builds again from a fresh object tree rather than linking
# what the last configuration left (CMake refuses a cache made from another
# source tree; MuPDF's `ar cr` would keep members a new version dropped).
set -e
cd "$(dirname "$0")/.."

HANZI_BUILD=${HANZI_BUILD:-$PWD/.deps}
DEPS_DIR=${DEPS_DIR:-$HANZI_BUILD/deps}
SRCD=$HANZI_BUILD/src

# The versions (mk/deps.mk asks this script for the stamp: `stamp TARGET`).
MUPDF_VER=1.28.5          # AGPL-3.0-or-later; mupdf.com/releases (no published sum)
MUPDF_SHA256=98a5c10cda20c3992cdf76ff6b2a1149c32bd79cc796d3f703230b1185b7e934
MUPDF_URL=https://mupdf.com/downloads/archive/mupdf-$MUPDF_VER-source.tar.gz
NCNN_VER=20260526         # BSD-3-Clause; the sum is GitHub's asset digest
NCNN_SHA256=754659d6fe65545cf2ef4483ffb84526fea631f8764c44b150f1601d0fb4004b
NCNN_URL=https://github.com/Tencent/ncnn/releases/download/$NCNN_VER/ncnn-$NCNN_VER-full-source.zip
CMAKE_VER=4.4.4           # only where there is no cmake (this desktop has none)
CMAKE_SHA256=e5bb807f7728cb60cd8b27ebc97a2edb469b68655f21e844a600c3575b76f5bb
CMAKE_URL=https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-linux-x86_64.tar.gz

# What MuPDF is built with. PDF only: no JavaScript, no HTML/EPUB/XPS/SVG/
# CBZ/image documents, no OCR, barcode or DOCX output, no hyphenation, no
# brotli (a filter no PDF standard has yet). The scan decoders stay:
# JBIG2, CCITT, JPX (OpenJPEG) and JPEG. TOFU and TOFU_CJK leave out the
# Noto and CJK fallback fonts (tens of MB): a page draws from the fonts it
# embeds, the base 14 (URW) are kept, and a PDF naming a CJK font it does not
# embed gets the app's own Kaiti through fz_install_load_system_cjk_font_funcs.
# None of these change a public header (only FZ_ENABLE_ICC does, and ICC is
# on), so the app needs only the include path.
MUPDF_MAKEVARS="build=release shared=no USE_SYSTEM_LIBS=no HAVE_X11=no HAVE_GLUT=no \
HAVE_CURL=no HAVE_LIBCRYPTO=no HAVE_OBJCOPY=no mujs=no html=no xps=no svg=no \
extract=no brotli=no barcode=no tesseract=no"
MUPDF_XCFLAGS="-DTOFU -DTOFU_CJK -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 \
-DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 \
-DFZ_ENABLE_HYPHEN=0"
# ncnn: static, CPU only, threads without OpenMP (ocr.h), no tools.
NCNN_OPTS="-DCMAKE_BUILD_TYPE=Release -DNCNN_SHARED_LIB=OFF -DNCNN_VULKAN=OFF \
-DNCNN_OPENMP=OFF -DNCNN_THREADS=ON -DNCNN_BUILD_TOOLS=OFF -DNCNN_BUILD_EXAMPLES=OFF \
-DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BUILD_TESTS=OFF -DNCNN_PYTHON=OFF -DNCNN_PIXEL_DRAWING=OFF"
# The Kobo's ncnn: ncnn's own armv7 toolchain (toolchains/arm-linux-
# gnueabihf), plus -marm: Ubuntu's cross gcc defaults to Thumb-2, where r7
# is not free, and ncnn's 14-register NEON asm then fails with "'asm'
# operand has impossible constraints". The app stays Thumb; the two
# interwork.
ARM_FLAGS="-march=armv7-a -mfloat-abi=hard -mfpu=neon -marm"

fetch() {         # fetch URL FILE SHA256: download unless FILE is there and right
    if [ -f "$2" ] && echo "$3  $2" | sha256sum -c --status -; then return 0; fi
    echo "fetching $1 ..."
    curl -sSfL -o "$2.part" "$1"
    if ! echo "$3  $2.part" | sha256sum -c --status -; then
        echo "checksum mismatch for $1: got $(sha256sum "$2.part" | cut -d' ' -f1), want $3" >&2
        echo "(left as $2.part)" >&2
        exit 1
    fi
    mv "$2.part" "$2"
}

fetch_sources() {
    mkdir -p "$SRCD"
    fetch "$MUPDF_URL" "$SRCD/mupdf-$MUPDF_VER-source.tar.gz" "$MUPDF_SHA256"
    if [ ! -f "$SRCD/mupdf-$MUPDF_VER-source/Makefile" ]; then
        echo "unpacking MuPDF $MUPDF_VER ..."
        tar xzf "$SRCD/mupdf-$MUPDF_VER-source.tar.gz" -C "$SRCD"
    fi
    fetch "$NCNN_URL" "$SRCD/ncnn-$NCNN_VER-full-source.zip" "$NCNN_SHA256"
    if [ ! -f "$SRCD/ncnn-$NCNN_VER/CMakeLists.txt" ]; then
        echo "unpacking ncnn $NCNN_VER ..."
        mkdir -p "$SRCD/ncnn-$NCNN_VER"          # the zip has no top directory
        unzip -q -o "$SRCD/ncnn-$NCNN_VER-full-source.zip" -d "$SRCD/ncnn-$NCNN_VER"
    fi
}

find_cmake() {
    if command -v cmake >/dev/null 2>&1; then CMAKE=cmake; return; fi
    t=$HANZI_BUILD/tools
    CMAKE=$t/cmake-$CMAKE_VER-linux-x86_64/bin/cmake
    [ -x "$CMAKE" ] && return
    mkdir -p "$t"
    fetch "$CMAKE_URL" "$t/cmake-$CMAKE_VER-linux-x86_64.tar.gz" "$CMAKE_SHA256"
    tar xzf "$t/cmake-$CMAKE_VER-linux-x86_64.tar.gz" -C "$t"
}

set_target() {    # set_target TARGET: its tools, options, configuration and paths
    target=$1
    case $target in
        host|appimage) cc=gcc cxx=g++ ar=ar ld=ld ranlib=ranlib cross= ;;
        arm) c=${CROSS:-arm-linux-gnueabihf-}
             cc=${c}gcc cxx=${c}g++ ar=${c}ar ld=${c}ld ranlib=${c}ranlib cross=$c ;;
        *) echo "usage: $0 deps|stamp host|arm|appimage" >&2; exit 2 ;;
    esac
    if [ "$target" = arm ]; then
        archopts="-DNCNN_VFPV4=ON"     # and the toolchain file, written from ARM_FLAGS
        archflags=$ARM_FLAGS
    else
        # Runtime dispatch stays (SSE2 .. AVX2/FMA); AVX-512 kernels are left
        # out: slow to build, and no faster on a line of text.
        archopts="-DNCNN_AVX512=OFF"
        archflags=
    fi
    # config_id: everything that makes these libraries what they are
    config_id=$({
        echo "$MUPDF_VER $MUPDF_SHA256"; echo "$NCNN_VER $NCNN_SHA256"
        echo "$MUPDF_MAKEVARS"; echo "$MUPDF_XCFLAGS"; echo "$NCNN_OPTS"
        echo "$archopts"; echo "$archflags"
        for t in "$cc" "$cxx"; do "$t" --version 2>/dev/null | head -1 || echo "$t: none"; done
    } | sha256sum | cut -c1-10)
    prefix=$DEPS_DIR/$target
    obj=$HANZI_BUILD/obj/$target-$config_id
    stamp=$prefix/.built-mupdf-$MUPDF_VER-ncnn-$NCNN_VER-$config_id
}

build_deps() {    # build_deps TARGET
    set_target "$1"
    jobs=${DEPS_JOBS:-8}; [ "$jobs" -gt 8 ] && jobs=8
    # The AppImage's libraries must be built against glibc 2.28: built on a
    # newer one they call __isoc23_sscanf and the like (GLIBC_2.38), and the
    # AppImage fails to start on Debian 12 or Ubuntu 22.04. Only inside the
    # manylinux_2_28 container (tools/appimage.sh) is that what this is.
    if [ "$target" = appimage ]; then
        glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')
        if [ "$glibc" != 2.28 ]; then
            echo "deps: appimage is built inside the manylinux_2_28 container, against glibc 2.28" >&2
            echo "      (tools/appimage.sh does it); this machine has glibc ${glibc:-unknown}" >&2
            exit 2
        fi
    fi

    # Called from make, which hands its command-line variables down in the
    # environment: check.sh's CXXFLAGS="-Werror ..." would otherwise reach
    # MuPDF's make (through MAKEFLAGS) and cmake (which reads CXXFLAGS).
    unset MAKEFLAGS MAKEOVERRIDES MFLAGS GNUMAKEFLAGS MAKELEVEL \
          CFLAGS CXXFLAGS CPPFLAGS LDFLAGS XCFLAGS

    if ! mkdir -p "$HANZI_BUILD/obj" "$obj" 2>/dev/null; then
        echo "deps: cannot write to $HANZI_BUILD -- set HANZI_BUILD to a writable folder with" >&2
        echo "      about 2 GB free (MuPDF and ncnn are built there; the network is needed)" >&2
        exit 1
    fi
    # One build at a time, whoever asks (two makes at once, the container):
    # they share the source trees, and the second finds the stamp.
    exec 9>"$HANZI_BUILD/obj/deps.lock"
    if ! flock -n 9; then
        echo "deps: another build holds $HANZI_BUILD/obj/deps.lock, waiting ..."
        flock 9
    fi
    if [ -f "$stamp" ]; then echo "deps: $target already built ($stamp)"; return 0; fi

    echo "deps: $target, configuration $config_id, objects in $obj"
    fetch_sources
    find_cmake
    t0=$(date +%s)

    echo "deps: MuPDF $MUPDF_VER for $target -> $prefix"
    # shellcheck disable=SC2086
    nice -n 10 make -C "$SRCD/mupdf-$MUPDF_VER-source" -j"$jobs" $MUPDF_MAKEVARS \
        OUT="$obj/mupdf" CC="$cc" CXX="$cxx" AR="$ar" LD="$ld" RANLIB="$ranlib" \
        XCFLAGS="$MUPDF_XCFLAGS" libs >"$obj/mupdf.log" 2>&1 \
        || { tail -30 "$obj/mupdf.log" >&2; echo "deps: MuPDF failed, log: $obj/mupdf.log" >&2; exit 1; }

    echo "deps: ncnn $NCNN_VER for $target -> $prefix"
    src=$SRCD/ncnn-$NCNN_VER
    if [ "$target" = arm ]; then
        tc=$obj/arm-linux-gnueabihf.toolchain.cmake
        {
            echo 'set(CMAKE_SYSTEM_NAME Linux)'
            echo 'set(CMAKE_SYSTEM_PROCESSOR arm)'
            echo "set(CMAKE_C_COMPILER \"${cross}gcc\")"
            echo "set(CMAKE_CXX_COMPILER \"${cross}g++\")"
            echo 'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)'
            for k in LIBRARY INCLUDE PACKAGE; do echo "set(CMAKE_FIND_ROOT_PATH_MODE_$k ONLY)"; done
            echo "set(CMAKE_C_FLAGS \"$archflags\" CACHE STRING \"c flags\")"
            echo "set(CMAKE_CXX_FLAGS \"$archflags\" CACHE STRING \"c++ flags\")"
        } >"$tc"
        archopts="-DCMAKE_TOOLCHAIN_FILE=$tc $archopts"
    fi
    # shellcheck disable=SC2086
    nice -n 10 "$CMAKE" -S "$src" -B "$obj/ncnn" \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
        $NCNN_OPTS $archopts >"$obj/ncnn.log" 2>&1 \
        && nice -n 10 "$CMAKE" --build "$obj/ncnn" -j"$jobs" >>"$obj/ncnn.log" 2>&1 \
        || { tail -30 "$obj/ncnn.log" >&2; echo "deps: ncnn failed, log: $obj/ncnn.log" >&2; exit 1; }

    # Both built: now, in seconds, what another configuration installed here
    # is replaced (until now it stayed whole, for whatever was compiling
    # against it). Its stamp goes first, so an install that stops halfway
    # claims nothing, and its headers, so a file a new version dropped does
    # not linger.
    mkdir -p "$prefix/lib" "$prefix/include"
    find "$prefix" -maxdepth 1 -type f -name '.built-*' -exec rm -f {} +
    rm -rf "$prefix/include/mupdf" "$prefix/include/ncnn"
    cp "$obj/mupdf/libmupdf.a" "$obj/mupdf/libmupdf-third.a" "$prefix/lib/"
    cp -R "$SRCD/mupdf-$MUPDF_VER-source/include/mupdf" "$prefix/include/"
    "$CMAKE" --install "$obj/ncnn" >>"$obj/ncnn.log" 2>&1 \
        || { tail -30 "$obj/ncnn.log" >&2; echo "deps: ncnn install failed, log: $obj/ncnn.log" >&2; exit 1; }

    date -u +%FT%TZ >"$stamp"
    echo "deps: $target done in $(( $(date +%s) - t0 ))s"
}

if [ "$1" = deps ]; then
    build_deps "$2"
    exit 0
fi
if [ "$1" = stamp ]; then
    set_target "$2"
    echo "$stamp"
    exit 0
fi

mkdir -p src/vendor build/data/hsk31

if [ ! -f src/vendor/stb_truetype.h ]; then
    echo "stb_truetype.h (public domain) ..."
    curl -sSL -o src/vendor/stb_truetype.h \
        https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
fi

if [ ! -f src/vendor/stb_image.h ]; then
    # The 连环画's plates: PNG and JPEG, read straight out of their EPUBs
    # (its raw inflate unpacks the zip entry too).
    echo "stb_image.h (public domain) ..."
    curl -sSL -o src/vendor/stb_image.h \
        https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
fi

if [ ! -f src/vendor/sqlite3.c ]; then
    echo "SQLite amalgamation (public domain) ..."
    tmp=$(mktemp -d)
    curl -sSL -o "$tmp/sqlite.zip" \
        https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip
    unzip -qo "$tmp/sqlite.zip" -d "$tmp"
    cp "$tmp"/sqlite-amalgamation-*/sqlite3.c "$tmp"/sqlite-amalgamation-*/sqlite3.h src/vendor/
    rm -rf "$tmp"
fi

# The commit that is vendored: upstream master of 2026-08-06, v1.25.0-543.
# Its .git directories are removed after the clone, as they are in the
# committed copy, so the files stay ordinary files of this repository. This
# used to test for src/vendor/FBInk/.git, which the committed copy does not
# have: run, it would have deleted that copy and cloned whatever master was.
FBINK_REV=886f25f13368859ad8a899b88d04c26e19cda32e
if [ ! -f src/vendor/FBInk/fbink.c ]; then
    echo "FBInk (GPLv3+) at $FBINK_REV ..."
    rm -rf src/vendor/FBInk
    git clone -q https://github.com/NiLuJe/FBInk.git src/vendor/FBInk
    git -C src/vendor/FBInk checkout -q "$FBINK_REV"
    git -C src/vendor/FBInk submodule update -q --init --recursive
    find src/vendor/FBInk -name .git -prune -exec rm -rf {} +
fi

if [ ! -f build/data/hsk31/HSK3.1_words_level1.json ]; then
    echo "HSK 3.1 word lists ..."
    tmp=$(mktemp -d)
    git clone --depth 1 -q https://github.com/leonsilicon/hsk3.1.git "$tmp/hsk31"
    cp "$tmp"/hsk31/HSK3.1_words_level[1-6].json build/data/hsk31/
    cp "$tmp"/hsk31/README.md build/data/hsk31/SOURCE.md
    rm -rf "$tmp"
fi

# MuPDF and ncnn: only their sources here, on the build drive if it is
# there; `make deps` / `make deps-arm` build them.
if mkdir -p "$SRCD" 2>/dev/null; then
    fetch_sources
else
    echo "MuPDF and ncnn not fetched: $HANZI_BUILD is not writable (set HANZI_BUILD)"
fi

echo "done."
