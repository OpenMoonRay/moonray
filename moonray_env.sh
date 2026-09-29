#!/bin/bash
# Source this script to run moonray/denoise from your shell.
# Usage: source moonray_env.sh [installs_root]
INSTALLS_ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/installs}"
MOONRAY_ROOT="$INSTALLS_ROOT/openmoonray"

if [ ! -x "$MOONRAY_ROOT/bin/moonray" ]; then
    echo "moonray not found under $MOONRAY_ROOT (build it first)" >&2
    return 1 2>/dev/null || exit 1
fi

export PATH="$MOONRAY_ROOT/bin:$PATH"
export RDL2_DSO_PATH="$MOONRAY_ROOT/rdl2dso"
export REZ_MOONRAY_ROOT="$MOONRAY_ROOT"
export ARRAS_SESSION_PATH="$MOONRAY_ROOT/sessions"
export MOONRAY_CLASS_PATH="$MOONRAY_ROOT/shader_json"
export PXR_PLUGINPATH_NAME="$MOONRAY_ROOT/plugin/pxr"
export PXR_PLUGIN_PATH="$MOONRAY_ROOT/plugin/pxr"
export PYTHONPATH="$INSTALLS_ROOT/lib/python:$INSTALLS_ROOT/lib64/python3.9/site-packages:$MOONRAY_ROOT/lib/python:${PYTHONPATH:-}"
export DYLD_LIBRARY_PATH="$INSTALLS_ROOT/lib:$MOONRAY_ROOT/lib:${DYLD_LIBRARY_PATH:-}"

echo "MoonRay environment ready: $MOONRAY_ROOT"
