"""Cycles-parity checks: lights and Principled BSDF inputs map to RDLA.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/test_cycles_parity.py
"""

import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(HERE)
sys.path.insert(0, ADDON_DIR)

from exporter import export_scene  # noqa: E402


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
    use_progressive = False


def export(text_out):
    scene = bpy.context.scene
    dg = bpy.context.evaluated_depsgraph_get()
    rdla = export_scene(scene, dg, FakeSettings(), FakePrefs(), text_out)
    return open(rdla).read()


def main():
    results = {}

    # --- lights ----------------------------------------------------------
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    bpy.ops.object.camera_add(location=(6, -6, 4))
    scene.camera = bpy.context.object

    bpy.ops.object.light_add(type="AREA", location=(0, 0, 3))
    area = bpy.context.object.data
    area.shape = "DISK"
    area.size = 2.0
    area.energy = 100.0
    text = export("/tmp/parity_lights.exr")
    results["area disk -> DiskLight"] = "DiskLight(" in text
    results["area disk radius"] = '["radius"] = 1' in text

    bpy.ops.object.light_add(type="AREA", location=(3, 0, 3))
    area2 = bpy.context.object.data
    area2.shape = "SQUARE"
    area2.size = 2.0
    area2.spread = 0.5
    text = export("/tmp/parity_lights2.exr")
    results["area square -> RectLight"] = "RectLight(" in text
    results["area spread"] = '["spread"] = 0.5' in text

    bpy.ops.object.light_add(type="POINT", location=(0, 2, 0))
    pnt = bpy.context.object.data
    pnt.use_temperature = True
    pnt.temperature = 6500.0
    text = export("/tmp/parity_lights3.exr")
    results["point temperature color"] = (
        "SphereLight(" in text and '["color"] = Rgb(' in text)

    # --- Principled BSDF full mapping ------------------------------------
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    bpy.ops.object.camera_add(location=(6, -6, 4))
    scene.camera = bpy.context.object
    bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, 0))
    cube = bpy.context.object

    mat = bpy.data.materials.new("principled")
    mat.use_nodes = True
    node = mat.node_tree.nodes["Principled BSDF"]
    node.inputs["Base Color"].default_value = (0.8, 0.3, 0.1, 1.0)
    node.inputs["Metallic"].default_value = 0.7
    node.inputs["Roughness"].default_value = 0.2
    node.inputs["IOR"].default_value = 1.6
    node.inputs["Specular IOR Level"].default_value = 0.9
    node.inputs["Anisotropic"].default_value = 0.4
    node.inputs["Anisotropic Rotation"].default_value = 0.5
    node.inputs["Transmission Weight"].default_value = 0.6
    node.inputs["Coat Weight"].default_value = 0.3
    node.inputs["Coat Roughness"].default_value = 0.1
    node.inputs["Sheen Weight"].default_value = 0.2
    node.inputs["Subsurface Weight"].default_value = 0.5
    node.inputs["Emission Color"].default_value = (0.1, 0.5, 0.9, 1.0)
    node.inputs["Emission Strength"].default_value = 4.0
    node.inputs["Diffuse Roughness"].default_value = 0.3
    cube.data.materials.append(mat)

    text = export("/tmp/parity_mat.exr")
    results["ior"] = '["refractive_index"] = 1.6' in text
    results["specular level"] = '["specular"] = 0.899' in text
    results["metallic"] = '["metallic"] = 0.699' in text
    results["anisotropy"] = '["anisotropy"] = 0.4000' in text
    results["anisotropy tangent"] = '["shading_tangent"] = Vec2(' in text
    results["transmission"] = '["transmission"] = 0.6000' in text
    results["transmission color = base"] = \
        '["transmission_color"] = Rgb(0.8000' in text
    results["clearcoat"] = '["clearcoat"] = 0.3000' in text
    results["clearcoat roughness"] = '["clearcoat_roughness"] = 0.1000' in text
    results["fuzz"] = '["fuzz"] = 0.2000' in text
    results["subsurface"] = '["bssrdf"] = 2' in text
    results["emission strength applied"] = (
        'MeshLight(' in text and '["intensity"] = 4' in text)
    results["diffuse roughness"] = '["diffuse_roughness"] = 0.3000' in text

    # --- other Cycles BSDF nodes -----------------------------------------
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    bpy.ops.object.camera_add(location=(6, -6, 4))
    scene.camera = bpy.context.object
    bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, 0))
    cube = bpy.context.object

    def node_only(node_type, set_inputs):
        m = bpy.data.materials.new(node_type)
        m.use_nodes = True
        tree = m.node_tree
        # replace the principled node with the requested shader
        for n in list(tree.nodes):
            if n.type == "BSDF_PRINCIPLED":
                tree.nodes.remove(n)
        out = next(n for n in tree.nodes if n.type == "OUTPUT_MATERIAL")
        n = tree.nodes.new(node_type)
        tree.links.new(n.outputs[0], out.inputs["Surface"])
        for k, v in set_inputs.items():
            n.inputs[k].default_value = v
        return m

    m = node_only("ShaderNodeBsdfAnisotropic", {
        "Color": (0.9, 0.2, 0.2, 1.0), "Roughness": 0.3, "Anisotropy": 0.6})
    cube.data.materials.append(m)
    text = export("/tmp/parity_aniso.exr")
    results["bsdf anisotropic"] = '["anisotropy"] = 0.6' in text

    m = node_only("ShaderNodeBsdfRefraction", {
        "Color": (0.2, 0.8, 0.9, 1.0), "IOR": 1.33})
    cube.data.materials.clear()
    cube.data.materials.append(m)
    text = export("/tmp/parity_refr.exr")
    results["bsdf refraction transmission"] = '["transmission"] = 1' in text
    results["bsdf refraction ior"] = '["refractive_index"] = 1.33' in text

    m = node_only("ShaderNodeBsdfTranslucent", {
        "Color": (0.5, 0.1, 0.1, 1.0)})
    cube.data.materials.clear()
    cube.data.materials.append(m)
    text = export("/tmp/parity_translucent.exr")
    results["bsdf translucent transmission"] = '["transmission"] = 1' in text

    ok = True
    for k, v in results.items():
        print("CHECK %-28s: %s" % (k, "OK" if v else "MISSING"))
        ok = ok and v
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
