#!/bin/bash
# One-shot: wait for the dependency superbuild, build MoonRay, verify it,
# and run the Blender end-to-end render test.
# Usage: ./finish_build_and_test.sh
set -uo pipefail

WORKSPACE="$(cd "$(dirname "$0")" && pwd)"
DEPS_LOG="$WORKSPACE/../build-deps/deps_build.log"   # workspace = .../wave-tracer/moonray
DEPS_DIR="$(cd "$WORKSPACE/.." && pwd)/build-deps"

echo "== 1/4 Waiting for dependency superbuild =="
# The superbuild runs via `cmake --build .` in $DEPS_DIR; poll for the
# stamp-free end state: all ExternalProject stamps done. Simplest robust
# check: the build process must not be running AND the log must end with
# an install of the last dep (GLFW).
while pgrep -f "cmake --build ." >/dev/null 2>&1 || pgrep -f "$DEPS_DIR" >/dev/null 2>&1; do
    sleep 30
done
if ! grep -q "Performing install step for 'GLFW'" "$DEPS_LOG"; then
    echo "Dependency build did not complete successfully. Tail of log:"
    tail -20 "$DEPS_LOG"
    exit 1
fi
echo "Dependencies built."

echo
echo "== 2/4 Building MoonRay =="
"$WORKSPACE/build_moonray.sh" || exit 1

echo
echo "== 3/4 Verifying install (official sphere test scene) =="
"$WORKSPACE/verify_moonray.sh" || exit 1

echo
echo "== 4/4 Blender end-to-end render test =="
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
    --python "$WORKSPACE/blender_addon/tests/test_render.py" -- \
    /tmp/moonray_blender_render.png
RC=$?
if [ $RC -eq 0 ]; then
    echo "E2E OK: /tmp/moonray_blender_render.png"
else
    echo "E2E FAILED (exit $RC)"
    exit 1
fi

echo
echo "ALL DONE. Enable the add-on in Blender:"
echo "  ./install_addon.sh"
echo "  Edit > Preferences > Add-ons > Render > MoonRay Render"
