#!/bin/bash
# Install (symlink) the MoonRay add-on into Blender's add-ons directory.
# Usage: ./install_addon.sh [blender-version]   (default: detected 5.2)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ADDON_NAME="moonray_blender"
ADDON_SRC="$HERE/blender_addon"

# Detect Blender version
BLENDER_BIN="/Applications/Blender.app/Contents/MacOS/Blender"
if [ ! -x "$BLENDER_BIN" ]; then
    echo "Blender not found at $BLENDER_BIN"
    exit 1
fi
VER="$("$BLENDER_BIN" --version | head -1 | awk '{print $2}')"
MAJOR_MINOR="$(echo "$VER" | cut -d. -f1-2)"

TARGET_DIR="$HOME/Library/Application Support/Blender/$MAJOR_MINOR/scripts/addons/$ADDON_NAME"
mkdir -p "$(dirname "$TARGET_DIR")"
rm -rf "$TARGET_DIR"
ln -s "$ADDON_SRC" "$TARGET_DIR"

echo "Installed MoonRay add-on for Blender $MAJOR_MINOR:"
echo "  $TARGET_DIR -> $ADDON_SRC"
echo
echo "Enable it in Blender: Edit > Preferences > Add-ons > Render > MoonRay Render"
