"""Headless test: register the add-on, switch the render engine, export.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/test_register.py
"""

import os
import shutil
import sys
import tempfile

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(HERE)  # .../blender_addon

# install the package under its canonical module name
tmp = tempfile.mkdtemp(prefix="moonray_addon_test_")
pkg_dir = os.path.join(tmp, "moonray_blender")
shutil.copytree(ADDON_DIR, pkg_dir, ignore=shutil.ignore_patterns("tests", "__pycache__"))
sys.path.insert(0, tmp)

import moonray_blender  # noqa: E402


def main():
    # enable through the official add-on flow (registers + creates prefs)
    bpy.ops.preferences.addon_enable(module="moonray_blender")
    print("ADDON ENABLED:", "moonray_blender" in
          bpy.context.preferences.addons)

    scene = bpy.context.scene
    scene.render.engine = "MOONRAY_RENDER"
    print("ENGINE SET:", scene.render.engine)

    # preferences
    prefs = bpy.context.preferences.addons["moonray_blender"].preferences
    prefs.moonray_root = "/Applications/MoonRay/installs/openmoonray"
    prefs.debug_keep_files = True
    print("PREFS:", prefs.moonray_root)

    # default scene has a camera, cube and light already
    settings = scene.moonray
    settings.export_only = True

    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.filepath = os.path.join(tmp, "out.exr")

    # render() with export_only writes the rdla next to the target path
    # (engine passes its own temp path, so call the operator instead)
    bpy.ops.moonray.export_scene(filepath=os.path.join(tmp, "test_scene.exr"))
    rdla = os.path.join(tmp, "test_scene.rdla")
    print("EXPORTED:", os.path.exists(rdla), os.path.getsize(rdla))

    bpy.ops.preferences.addon_disable(module="moonray_blender")
    print("UNREGISTER OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
