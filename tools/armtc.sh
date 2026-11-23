#!/bin/sh
# Set up an armhf C++ cross-compiler without root.
#
# The preferred way is simply:
#     sudo apt install g++-arm-linux-gnueabihf
# and if that has been done this script is unnecessary -- `make kobo` will
# already work. It exists because the app moved to C++17 while only the C
# cross-compiler was installed, and waiting on a root password blocks the
# only build that matters.
#
# It unpacks the Debian packages into a directory and writes a wrapper that
# points the extracted driver at the ARM binutils and armhf sysroot the C
# cross-toolchain already provides. Prints the directory to put on PATH.
set -e

DEST="${1:-$HOME/.cache/hanzi-armtc}"
WRAP="$DEST/wrap"

if command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
    echo "arm-linux-gnueabihf-g++ is already installed; nothing to do." >&2
    exit 0
fi
if [ ! -d /usr/arm-linux-gnueabihf/include ]; then
    echo "need the C cross-toolchain first: sudo apt install gcc-arm-linux-gnueabihf" >&2
    exit 1
fi

GCCDIR=$(ls -d /usr/lib/gcc-cross/arm-linux-gnueabihf/* 2>/dev/null | tail -1)
[ -n "$GCCDIR" ] || { echo "no /usr/lib/gcc-cross/arm-linux-gnueabihf" >&2; exit 1; }
VER=$(basename "$GCCDIR")

mkdir -p "$DEST/debs" "$DEST/root" "$WRAP" "$DEST/bin"
(cd "$DEST/debs" && apt-get download "g++-$VER-arm-linux-gnueabihf" \
                                     "libstdc++-$VER-dev-armhf-cross" >/dev/null)
for d in "$DEST"/debs/*.deb; do dpkg-deb -x "$d" "$DEST/root"; done

# gcc looks for `as` and `ld` by those bare names; give it the ARM ones.
for t in as ld ar ranlib strip objcopy nm; do
    ln -sf "/usr/bin/arm-linux-gnueabihf-$t" "$DEST/bin/$t"
done

cat > "$WRAP/arm-linux-gnueabihf-g++" <<EOF
#!/bin/sh
# Locally extracted armhf g++ -- see tools/armtc.sh.
exec "$DEST/root/usr/bin/arm-linux-gnueabihf-g++-$VER" \\
    -B"$DEST/bin" -fno-use-linker-plugin \\
    -B"$GCCDIR/" -B/usr/arm-linux-gnueabihf/lib/ \\
    -L"$DEST/root/usr/lib/gcc-cross/arm-linux-gnueabihf/$VER" \\
    -isystem /usr/arm-linux-gnueabihf/include \\
    "\$@"
EOF
chmod +x "$WRAP/arm-linux-gnueabihf-g++"

printf 'int main(){return 0;}\n' > "$DEST/t.cpp"
"$WRAP/arm-linux-gnueabihf-g++" -std=gnu++17 -static -o "$DEST/t" "$DEST/t.cpp"
file "$DEST/t" | grep -q ARM || { echo "built binary is not ARM" >&2; exit 1; }
rm -f "$DEST/t" "$DEST/t.cpp"

echo "ok -- build with:"
echo "    PATH=\"$WRAP:\$PATH\" make kobo"
