#!/bin/bash
# Build Doom for the Apex OLED.
#
# doomgeneric is GPL and lives upstream, so it is fetched rather than vendored;
# this directory holds only the backend that renders to the panel.
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
WORK="${1:-$HERE/build}"
DEST="$HOME/.local/share/apex-oled"

mkdir -p "$WORK" "$DEST"
if [ ! -d "$WORK/doomgeneric" ]; then
    git clone --depth 1 https://github.com/ozkl/doomgeneric.git "$WORK/doomgeneric"
fi
SRC="$WORK/doomgeneric/doomgeneric"
cp "$HERE/doomgeneric_apex.c" "$SRC/"

# The upstream Makefile builds one backend, named in SRC_DOOM, and the
# directory ships several others that would also be compiled and fail to link.
cd "$SRC"
for f in doomgeneric_*.c; do
    [ "$f" = doomgeneric_apex.c ] || mv -f "$f" "$f.unused" 2>/dev/null || true
done
sed -e 's/doomgeneric_xlib\.o/doomgeneric_apex.o/' \
    -e 's|^CFLAGS.*|CFLAGS+= -O2 -DNORMALUNIX -DLINUX -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200|' \
    -e 's|^LDFLAGS.*|LDFLAGS+=|' \
    -e 's|^LIBS.*|LIBS+= -lm|' Makefile > Makefile.apex

rm -rf build
make -f Makefile.apex -j"$(nproc)"
install -m755 doomgeneric "$DEST/doomgeneric-apex"
install -m755 "$HERE/apex-doom" "$HOME/.local/bin/apex-doom"
echo
echo "installed: $DEST/doomgeneric-apex"
echo "run with:  apex-doom     (needs a WAD; default /usr/share/doom/doom1.wad)"
