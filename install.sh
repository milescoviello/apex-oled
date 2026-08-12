#!/bin/bash
# Install apex-oled: symlink the tools, add the udev rule, seed a config.
# Idempotent - safe to re-run after a git pull.
set -euo pipefail

REPO="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BIN="$HOME/.local/bin"
CFG="$HOME/.config/apex-oled"

echo "repo: $REPO"
mkdir -p "$BIN" "$CFG" "$HOME/.config/autostart"

for f in apex-oled apex-oledd apex-oledd-supervise apex-oled-test; do
    chmod +x "$REPO/$f"
    ln -sfn "$REPO/$f" "$BIN/$f"
    echo "  linked $BIN/$f"
done

if [ ! -e "$CFG/config.toml" ]; then
    cp "$REPO/config.example.toml" "$CFG/config.toml"
    echo "  wrote $CFG/config.toml"
else
    echo "  kept existing $CFG/config.toml"
fi

# Autostart the daemon under its supervisor (KDE/GNOME/XFCE all read this).
sed "s|Exec=apex-oledd-supervise|Exec=$BIN/apex-oledd-supervise|" \
    "$REPO/apex-oledd.desktop" > "$HOME/.config/autostart/apex-oledd.desktop"
echo "  wrote ~/.config/autostart/apex-oledd.desktop"

# hidraw is root-only by default; this grants the logged-in user access.
RULE=/etc/udev/rules.d/99-steelseries-apex-oled.rules
if [ ! -e "$RULE" ]; then
    echo
    echo "The udev rule needs root (so the panel works without sudo):"
    sudo install -m 0644 "$REPO/udev/99-steelseries-apex-oled.rules" "$RULE"
    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=hidraw --action=change
    echo "  installed $RULE"
else
    echo "  kept existing $RULE"
fi

echo
if "$BIN/apex-oled" info; then
    echo
    echo "Start it now with:  $BIN/apex-oledd-supervise &"
fi
