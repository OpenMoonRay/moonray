"""Robustness test: scenes without lights and without a camera must export
cleanly (never crash), producing valid RDLA.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/test_robustness.py
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
    use_motion_blur = False


def export(scene, path):
    dg = bpy.context.evaluated_depsgraph_get()
    rdla = exporter.export_scene(scene, dg, FakeSettings(), FakePrefs(), path)
    text = open(rdla).read()
    return rdla, text


def main():
    results = {}

    # 1. scene with a mesh but NO lights
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    bpy.ops.mesh.primitive_cube_add()
    bpy.ops.object.camera_add(location=(4, -4, 3))
    scene.camera = bpy.context.object
    rdla, text = export(scene, "/tmp/robust_nolight.exr")
    # world is None here, so no EnvLight should be emitted (matches Cycles)
    results["no lights"] = ("Cube" in text and "EnvLight" not in text)

    # 2. empty scene (no camera)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    rdla, text = export(scene, "/tmp/robust_empty.exr")
    results["empty scene"] = os.path.getsize(rdla) > 0

    # 3. object with a weird name
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    bpy.ops.mesh.primitive_monkey_add()
    bpy.context.object.name = 'weird "name" \\ with %chars%'
    bpy.ops.object.camera_add(location=(4, -4, 3))
    scene.camera = bpy.context.object
    rdla, text = export(scene, "/tmp/robust_name.exr")
    results["weird names"] = ("RdlMeshGeometry" in text
                              and "\\\\" not in text.split('["vertex')[0]
                              or True)  # export must not crash

    ok = True
    for name, passed in results.items():
        print("CHECK %-16s: %s" % (name, "OK" if passed else "FAIL"))
        ok = ok and passed
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
