#!/bin/bash
# Verify a MoonRay macOS build: checks the install layout and renders the
# official sphere test scene.
# Usage: ./verify_moonray.sh [installs_root]
set -uo pipefail

INSTALLS_ROOT="${1:-/Users/faputa/Documents/wave-tracer/installs}"
MOONRAY_ROOT="$INSTALLS_ROOT/openmoonray"
TESTDATA_DIR="$(cd "$(dirname "$0")" && pwd)/openmoonray/testdata"

echo "== Install layout =="
for p in \
    "$MOONRAY_ROOT/bin/moonray" \
    "$MOONRAY_ROOT/rdl2dso" \
    "$MOONRAY_ROOT/sessions" \
    "$INSTALLS_ROOT/lib" \
    "$MOONRAY_ROOT/lib"; do
    if [ -e "$p" ]; then
        echo "OK   $p"
    else
        echo "MISS $p"
    fi
done

if [ ! -x "$MOONRAY_ROOT/bin/moonray" ]; then
    echo "FATAL: moonray binary not found - build incomplete?"
    exit 1
fi

echo
echo "== Environment =="
export PATH="$MOONRAY_ROOT/bin:$PATH"
export RDL2_DSO_PATH="$MOONRAY_ROOT/rdl2dso"
export REZ_MOONRAY_ROOT="$MOONRAY_ROOT"
export ARRAS_SESSION_PATH="$MOONRAY_ROOT/sessions"
export MOONRAY_CLASS_PATH="$MOONRAY_ROOT/shader_json"
export PXR_PLUGINPATH_NAME="$MOONRAY_ROOT/plugin/pxr"
export PXR_PLUGIN_PATH="$MOONRAY_ROOT/plugin/pxr"
export PYTHONPATH="$INSTALLS_ROOT/lib/python:$INSTALLS_ROOT/lib64/python3.9/site-packages:$MOONRAY_ROOT/lib/python:${PYTHONPATH:-}"
export DYLD_LIBRARY_PATH="$INSTALLS_ROOT/lib:$MOONRAY_ROOT/lib:${DYLD_LIBRARY_PATH:-}"

echo
echo "== moonray --help (sanity) =="
"$MOONRAY_ROOT/bin/moonray" -help 2>&1 | head -8 || true

echo
echo "== Render sphere.rdla =="
WORK=$(mktemp -d)
cp "$TESTDATA_DIR/sphere.rdla" "$WORK/scene.rdla"

time "$MOONRAY_ROOT/bin/moonray" -in "$WORK/scene.rdla" \
    -out "$WORK/sphere.exr" -threads 8 2>&1 | tail -6
RC=$?
echo "render exit code: $RC"
if [ $RC -eq 0 ] && [ -f "$WORK/sphere.exr" ]; then
    echo "RENDER OK -> $WORK/sphere.exr"
    exit 0
else
    echo "RENDER FAILED (logs above)"
    exit 1
fi
