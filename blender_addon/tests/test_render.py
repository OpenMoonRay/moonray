"""End-to-end test: enable add-on, render a Blender scene with MoonRay.

Requires a working MoonRay installation (set MOONRAY_ROOT env or edit below).

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/test_render.py -- <out.png>
"""

import os
import shutil
import sys
import tempfile

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(HERE)

INSTALLS_ROOT = os.environ.get(
    "MOONRAY_INSTALLS",
    "/Users/faputa/Documents/wave-tracer/installs")
MOONRAY_ROOT = os.environ.get(
    "MOONRAY_ROOT",
    os.path.join(INSTALLS_ROOT, "openmoonray"))


def main(out_path):
    # fresh scene first (read_factory_settings resets add-on enablement)
    bpy.ops.wm.read_factory_settings(use_empty=True)

    # install under canonical module name and enable through the add-on flow
    tmp = tempfile.mkdtemp(prefix="moonray_render_test_")
    pkg_dir = os.path.join(tmp, "moonray_blender")
    shutil.copytree(ADDON_DIR, pkg_dir,
                    ignore=shutil.ignore_patterns("tests", "__pycache__"))
    sys.path.insert(0, tmp)
    import moonray_blender  # noqa: F401  (pre-load so enable finds it)
    bpy.ops.preferences.addon_enable(module="moonray_blender")

    prefs = bpy.context.preferences.addons["moonray_blender"].preferences
    prefs.moonray_root = MOONRAY_ROOT
    prefs.installs_root = INSTALLS_ROOT
    prefs.debug_keep_files = False
    print("MOONRAY_ROOT:", MOONRAY_ROOT)
    print("BIN EXISTS:", os.path.isfile(os.path.join(MOONRAY_ROOT, "bin", "moonray")))

    # build a small scene
    scene = bpy.context.scene
    scene.render.engine = "MOONRAY_RENDER"
    scene.render.resolution_x = 480
    scene.render.resolution_y = 270
    scene.render.resolution_percentage = 100
    scene.render.filepath = out_path
    scene.render.image_settings.file_format = "PNG"

    bpy.ops.mesh.primitive_uv_sphere_add(location=(0, 0, 1))
    bpy.ops.object.light_add(type="SUN", location=(5, 5, 8))
    bpy.context.object.data.energy = 3.0
    bpy.ops.object.camera_add(location=(5, -5, 3))
    cam = bpy.context.object
    # aim the camera at the origin
    from mathutils import Vector
    direction = Vector((0.0, 0.0, 0.0)) - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam

    settings = scene.moonray
    settings.pixel_samples = 6
    settings.max_adaptive_samples = 64
    settings.threads = 8

    # render
    bpy.ops.render.render(write_still=True)

    ok = os.path.isfile(out_path) and os.path.getsize(out_path) > 1000
    # verify the render is not black (guards against silently-empty results)
    mean = 0.0
    if ok:
        try:
            img = bpy.data.images.load(out_path)
            px = list(img.pixels)
            mean = sum(px) / max(1, len(px))
            bpy.data.images.remove(img)
        except Exception:
            pass
    ok = ok and mean > 0.01
    print("RENDER RESULT:", "OK" if ok else "BLACK/MISSING",
          out_path, "mean_pixel=%.4f" % mean)

    bpy.ops.preferences.addon_disable(module="moonray_blender")
    return 0 if ok else 1


if __name__ == "__main__":
    argv = sys.argv
    out = None
    if "--" in argv:
        out = argv[argv.index("--") + 1]
    sys.exit(main(out or "/tmp/moonray_blender_render.png"))
