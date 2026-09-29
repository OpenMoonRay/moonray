"""Full engine end-to-end test using the mock moonray binary.

Validates: add-on enable -> render op -> exporter -> process launch ->
progress -> EXR load into Render Result -> Blender saves the PNG.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/test_engine_mock.py -- <out.png>
"""

import os
import shutil
import sys
import tempfile

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(HERE)


def main(out_path):
    tmp = tempfile.mkdtemp(prefix="moonray_engine_mock_")
    pkg_dir = os.path.join(tmp, "moonray_blender")
    shutil.copytree(ADDON_DIR, pkg_dir,
                    ignore=shutil.ignore_patterns("tests", "__pycache__"))
    sys.path.insert(0, tmp)
    bpy.ops.preferences.addon_enable(module="moonray_blender")

    # mock installation: bin/moonray = wrapper around mock_moonray.py
    root = os.path.join(tmp, "mock_install")
    bin_dir = os.path.join(root, "bin")
    os.makedirs(bin_dir)
    mock = os.path.join(bin_dir, "moonray")
    with open(mock, "w") as f:
        f.write("#!/usr/bin/env python3\n")
        f.write("import sys\n")
        f.write("sys.path.insert(0, %r)\n" % HERE)
        f.write("from mock_moonray import main\n")
        f.write("sys.exit(main())\n")
    os.chmod(mock, 0o755)
    # also fake the denoise binary so the denoise option can be exercised
    with open(os.path.join(bin_dir, "denoise"), "w") as f:
        f.write("#!/bin/sh\necho mock denoise\ncp \"$2\" \"$4\"\n")
    os.chmod(os.path.join(bin_dir, "denoise"), 0o755)

    prefs = bpy.context.preferences.addons["moonray_blender"].preferences
    prefs.moonray_root = root

    scene = bpy.context.scene
    scene.render.engine = "MOONRAY_RENDER"
    scene.render.resolution_x = 256
    scene.render.resolution_y = 128
    scene.render.resolution_percentage = 100
    scene.render.filepath = out_path
    scene.render.image_settings.file_format = "PNG"

    settings = scene.moonray
    settings.threads = 4
    settings.use_denoise = True

    bpy.ops.render.render(write_still=True)

    ok = os.path.isfile(out_path) and os.path.getsize(out_path) > 1000
    print("ENGINE E2E:", "OK" if ok else "MISSING", out_path,
          os.path.getsize(out_path) if os.path.exists(out_path) else 0)

    bpy.ops.preferences.addon_disable(module="moonray_blender")
    return 0 if ok else 1


if __name__ == "__main__":
    argv = sys.argv
    out = None
    if "--" in argv:
        out = argv[argv.index("--") + 1]
    sys.exit(main(out or "/tmp/moonray_engine_mock.png"))
