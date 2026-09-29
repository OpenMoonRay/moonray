"""Headless test covering all exporter paths: every light type, textured
Principled BSDF, emission, transparency, UVs, normals, DOF.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/test_full_scene.py \
      -- <out.exr>
"""

import os
import sys
import tempfile

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(HERE)
sys.path.insert(0, ADDON_DIR)

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


def main(out_path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100

    # textured image on disk
    tex_dir = tempfile.mkdtemp(prefix="moonray_tex_")
    tex_path = os.path.join(tex_dir, "grid.png")
    img = bpy.data.images.new("grid", width=64, height=64)
    import math
    px = [0.0] * (64 * 64 * 4)
    for y in range(64):
        for x in range(64):
            v = 0.8 if ((x // 8) + (y // 8)) % 2 == 0 else 0.2
            i = (y * 64 + x) * 4
            px[i:i + 4] = [v, v, v, 1.0]
    img.pixels[:] = px
    img.filepath_raw = tex_path
    img.file_format = "PNG"
    img.save()

    # floor plane
    bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, 0))
    floor = bpy.context.object
    mat = bpy.data.materials.new("floor_mat")
    mat.use_nodes = True
    tree = mat.node_tree
    principled = tree.nodes["Principled BSDF"]
    tex_node = tree.nodes.new(type="ShaderNodeTexImage")
    tex_node.image = img
    tree.links.new(tex_node.outputs["Color"], principled.inputs["Base Color"])
    principled.inputs["Roughness"].default_value = 0.6
    floor.data.materials.append(mat)

    # emissive sphere
    bpy.ops.mesh.primitive_uv_sphere_add(location=(0, 0, 1))
    sphere = bpy.context.object
    emat = bpy.data.materials.new("emit_mat")
    emat.use_nodes = True
    em_prin = emat.node_tree.nodes["Principled BSDF"]
    em_prin.inputs["Emission Color"].default_value = (1, 0.5, 0.1, 1)
    em_prin.inputs["Emission Strength"].default_value = 8.0
    sphere.data.materials.append(emat)

    # transparent cube
    bpy.ops.mesh.primitive_cube_add(location=(-2, 1, 1))
    cube = bpy.context.object
    tmat = bpy.data.materials.new("glass_mat")
    tmat.use_nodes = True
    t_prin = tmat.node_tree.nodes["Principled BSDF"]
    t_prin.inputs["Transmission Weight"].default_value = 1.0
    t_prin.inputs["Alpha"].default_value = 0.5
    cube.data.materials.append(tmat)

    # one of each light type
    bpy.ops.object.light_add(type="SUN", location=(5, 5, 8))
    sun = bpy.context.object
    sun.data.energy = 2.0
    bpy.ops.object.light_add(type="POINT", location=(-3, -2, 3))
    pt = bpy.context.object
    pt.data.energy = 100.0
    bpy.ops.object.light_add(type="SPOT", location=(4, -4, 4))
    spot = bpy.context.object
    spot.data.energy = 200.0
    bpy.ops.object.light_add(type="AREA", location=(0, 3, 4))
    area = bpy.context.object
    area.data.size = 4
    area.data.energy = 50.0

    # HDRI world environment
    world = bpy.data.worlds.new("hdri_world")
    world.use_nodes = True
    scene.world = world
    wt = world.node_tree
    bg = next(n for n in wt.nodes if n.type == "BACKGROUND")
    env_tex = wt.nodes.new("ShaderNodeTexEnvironment")
    env_tex.image = img
    wmap = wt.nodes.new("ShaderNodeMapping")
    wmap.inputs["Rotation"].default_value = (0.0, 0.0, 0.5)
    wt.links.new(wmap.outputs["Vector"], env_tex.inputs["Vector"])
    wt.links.new(env_tex.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 1.5

    # backface-culled plane (single-sided export)
    bpy.ops.mesh.primitive_plane_add(size=3, location=(2, 2, 1))
    cull = bpy.context.object
    cmat = bpy.data.materials.new("culled")
    cmat.use_backface_culling = True
    cull.data.materials.append(cmat)

    # camera with DOF
    bpy.ops.object.camera_add(location=(6, -7, 4))
    cam = bpy.context.object
    cam.rotation_euler = (1.15, 0, 0.7)
    cam.data.lens = 35
    cam.data.dof.use_dof = True
    cam.data.dof.aperture_fstop = 2.8
    cam.data.dof.focus_distance = 8.0
    scene.camera = cam

    depsgraph = bpy.context.evaluated_depsgraph_get()
    rdla = exporter.export_scene(scene, depsgraph, FakeSettings(), FakePrefs(),
                                 out_path)
    text = open(rdla).read()
    checks = {
        "SphereLight": "SphereLight(" in text,
        "DistantLight": "DistantLight(" in text,
        "SpotLight": "SpotLight(" in text,
        "RectLight": "RectLight(" in text,
        "EnvLight": "EnvLight(" in text,
        "ImageMap": "ImageMap(" in text,
        "emissive MeshLight": "MeshLight(" in text,
        "presence(alpha)": '"presence"' in text,
        "transmission": '"transmission"' in text,
        "uv_list": '"uv_list"' in text,
        "normal_list": '"normal_list"' in text,
        "dof": '["dof"] = true' in text,
        "dof_aperture": '"dof_aperture"' in text,
        "layer_entries": text.count("GeometrySet(") >= 3,
        "env_texture": '["texture"]' in text and tex_path in text,
        "side_type single": '["side_type"] = 1' in text,
        "env rotation": '["node_xform"] = Mat4(0.87758255' in text,
    }
    ok = True
    for name, passed in checks.items():
        print("CHECK %-18s: %s" % (name, "OK" if passed else "MISSING"))
        ok = ok and passed
    print("RDLA:", rdla, "bytes:", os.path.getsize(rdla))
    return 0 if ok else 1


if __name__ == "__main__":
    argv = sys.argv
    out = None
    if "--" in argv:
        out = argv[argv.index("--") + 1]
    sys.exit(main(out or "/tmp/moonray_full_test.exr"))
