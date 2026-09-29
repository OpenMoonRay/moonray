"""Motion blur export test.

Blender >= 5.0 no longer exposes the per-vertex "velocity" attribute on
evaluated meshes, so object motion blur is exported only when that attribute
is available (Blender 4.x). The test verifies: shutter attributes are
exported, the camera block is valid, and export never crashes even with
motion blur requested on Blender 5.2.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/test_motion_blur.py
"""

import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import exporter  # noqa: E402


class FakePrefs:
    light_scale = 1.0


class FakeSettings:
    pixel_samples = 8
    min_adaptive_samples = 16
    max_adaptive_samples = 4096
    pixel_filter = "DEFAULT"
    pixel_filter_width = 3.0
    use_progressive_tiles = False
    use_motion_blur = True


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.frame_set(5)

    # animated cube (translate + rotate)
    bpy.ops.mesh.primitive_cube_add(location=(0, 0, 0))
    cube = bpy.context.object
    cube.keyframe_insert("location", frame=1)
    cube.keyframe_insert("rotation_euler", frame=1)
    cube.location = (4, 2, 0)
    cube.rotation_euler = (0, 0, 1.0)
    cube.keyframe_insert("location", frame=10)
    cube.keyframe_insert("rotation_euler", frame=10)

    bpy.ops.object.camera_add(location=(6, -6, 4))
    scene.camera = bpy.context.object

    dg = bpy.context.evaluated_depsgraph_get()
    rdla = exporter.export_scene(scene, dg, FakeSettings(), FakePrefs(),
                                 "/tmp/mb_test.exr")
    text = open(rdla).read()

    has_velocity = '"velocity_list_0"' in text
    checks = {
        "export completed": os.path.getsize(rdla) > 0,
        "shutter open": '["mb_shutter_open"] = -0.5' in text,
        "shutter close": '["mb_shutter_close"] = 0.5' in text,
        "camera xform valid": 'PerspectiveCamera("camera")' in text,
    }
    ok = True
    for name, passed in checks.items():
        print("CHECK %-22s: %s" % (name, "OK" if passed else "MISSING"))
        ok = ok and passed
    print("INFO velocity attribute available:", has_velocity,
          "(expected False on Blender >= 5.0, True on Blender 4.x)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
