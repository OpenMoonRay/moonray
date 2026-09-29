"""Headless Blender test for the MoonRay add-on exporter.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/test_export.py -- <out.rdla>
"""

import os
import sys

import bpy

# locate the add-on package next to this test
HERE = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(HERE)
sys.path.insert(0, ADDON_DIR)

import exporter  # noqa: E402


class FakePrefs:
    light_scale = 1.0


def main(out_path):
    # start from a fresh scene: cube, sun, camera
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.resolution_x = 640
    scene.render.resolution_y = 480
    scene.render.resolution_percentage = 100

    bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, 0))
    cube = bpy.context.object
    if len(cube.data.uv_layers) == 0:
        cube.data.uv_layers.new(name="UVMap")
    bpy.ops.object.light_add(type="SUN", location=(5, 5, 5))
    sun = bpy.context.object
    sun.data.energy = 5.0
    bpy.ops.object.camera_add(location=(6, -6, 4))
    cam = bpy.context.object
    cam.rotation_euler = (1.1, 0, 0.8)
    scene.camera = cam

    # material with emission + color
    mat = bpy.data.materials.new("test_mat")
    mat.use_nodes = True
    principled = mat.node_tree.nodes["Principled BSDF"]
    principled.inputs["Base Color"].default_value = (0.8, 0.2, 0.2, 1.0)
    principled.inputs["Roughness"].default_value = 0.4
    cube.data.materials.append(mat)

    depsgraph = bpy.context.evaluated_depsgraph_get()

    # a minimal settings stand-in
    class FakeSettings:
        pixel_samples = 8
        min_adaptive_samples = 16
        max_adaptive_samples = 4096
        pixel_filter = "DEFAULT"
        pixel_filter_width = 3.0
        use_progressive_tiles = False
        use_motion_blur = False
        use_progressive = False

    rdla = exporter.export_scene(scene, depsgraph, FakeSettings(), FakePrefs(),
                                 out_path)
    print("EXPORTED:", rdla)
    print("BYTES:", os.path.getsize(rdla))
    with open(rdla) as f:
        text = f.read()
    print(text[:2000])
    return 0


if __name__ == "__main__":
    argv = sys.argv
    out = None
    if "--" in argv:
        out = argv[argv.index("--") + 1]
    if not out:
        out = "/tmp/moonray_test_export.exr"
    sys.exit(main(out))
