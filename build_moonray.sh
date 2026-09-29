#!/bin/bash
# Configure and build MoonRay itself (after the dependency superbuild).
# Usage: ./build_moonray.sh
set -uo pipefail

WORKSPACE="$(cd "$(dirname "$0")" && pwd)"
OPENMOONRAY="$WORKSPACE/openmoonray"
LOG="$WORKSPACE/build/main_build.log"
mkdir -p "$WORKSPACE/build"

cd "$OPENMOONRAY"

echo "== Configure (macos-release-ninja) =="
cmake --preset macos-release-ninja 2>&1 | tee -a "$LOG"
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "CONFIGURE FAILED - see $LOG"
    exit 1
fi

echo "== Build =="
cmake --build --preset macos-release-ninja 2>&1 | tee -a "$LOG"
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "BUILD FAILED - see $LOG"
    exit 1
fi

echo
echo "BUILD COMPLETE. Run ./verify_moonray.sh next."
