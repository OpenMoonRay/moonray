import os, sys, tempfile
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

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

tex_dir = tempfile.mkdtemp(prefix="moonray_tex2_")
tex_path = os.path.join(tex_dir, "norm.png")
img = bpy.data.images.new("norm", width=16, height=16)
img.generated_color = (0.5, 0.5, 1.0, 1.0)
img.filepath_raw = tex_path
img.file_format = "PNG"
img.save()

# --- sphere with normal-mapped principled ---
bpy.ops.mesh.primitive_uv_sphere_add(location=(0, 0, 1))
sph = bpy.context.object
if len(sph.data.uv_layers) == 0:
    sph.data.uv_layers.new()
m1 = bpy.data.materials.new("normal_mapped")
m1.use_nodes = True
t1 = m1.node_tree
prin = t1.nodes["Principled BSDF"]
img_node = t1.nodes.new("ShaderNodeTexImage")
img_node.image = img
nm = t1.nodes.new("ShaderNodeNormalMap")
nm.inputs["Strength"].default_value = 0.8
t1.links.new(img_node.outputs["Color"], nm.inputs["Color"])
t1.links.new(nm.outputs["Normal"], prin.inputs["Normal"])
prin.inputs["Base Color"].default_value = (0.9, 0.1, 0.1, 1)
sph.data.materials.append(m1)

# --- cube with Mix Shader (diffuse + glossy) ---
bpy.ops.mesh.primitive_cube_add(location=(2, 0, 1))
cube = bpy.context.object
m2 = bpy.data.materials.new("mixed")
m2.use_nodes = True
t2 = m2.node_tree
out = next(n for n in t2.nodes if n.type == "OUTPUT_MATERIAL")
mix = t2.nodes.new("ShaderNodeMixShader")
diff = t2.nodes.new("ShaderNodeBsdfDiffuse")
gloss = t2.nodes.new("ShaderNodeBsdfGlossy")
diff.inputs["Color"].default_value = (0.1, 0.8, 0.2, 1)
gloss.inputs["Roughness"].default_value = 0.15
mix.inputs["Fac"].default_value = 0.35
t2.links.new(diff.outputs["BSDF"], mix.inputs[1])
t2.links.new(gloss.outputs["BSDF"], mix.inputs[2])
t2.links.new(mix.outputs["Shader"], out.inputs["Surface"])
cube.data.materials.append(m2)

# --- plane with mapping-node texture (scale + offset) ---
bpy.ops.mesh.primitive_plane_add(size=4, location=(0, 2, 0))
mplane = bpy.context.object
m5 = bpy.data.materials.new("mapped_tex")
m5.use_nodes = True
t5 = m5.node_tree
p5 = t5.nodes["Principled BSDF"]
img5 = t5.nodes.new("ShaderNodeTexImage")
img5.image = img
map5 = t5.nodes.new("ShaderNodeMapping")
map5.inputs["Location"].default_value = (0.25, 0.25, 0.0)
map5.inputs["Scale"].default_value = (2.0, 3.0, 1.0)
t5.links.new(map5.outputs["Vector"], img5.inputs["Vector"])
t5.links.new(img5.outputs["Color"], p5.inputs["Base Color"])
mplane.data.materials.append(m5)

# --- plane with static-baked color mix (MixRGB of two constants) ---
bpy.ops.mesh.primitive_plane_add(size=6, location=(0, -2, 0))
plane = bpy.context.object
m3 = bpy.data.materials.new("baked_mix")
m3.use_nodes = True
t3 = m3.node_tree
p3 = t3.nodes["Principled BSDF"]
mixrgb = t3.nodes.new("ShaderNodeMix")
def _s(node, name, st):
    return next((s for s in node.inputs if s.name == name and s.type == st), None)
_s(mixrgb, "A", "RGBA").default_value = (1.0, 0.0, 0.0, 1)
_s(mixrgb, "B", "RGBA").default_value = (0.0, 0.0, 1.0, 1)
_s(mixrgb, "Factor", "VALUE").default_value = 0.5
res_sock = next(s for s in mixrgb.outputs if s.name == "Result" and s.type == "RGBA")
t3.links.new(res_sock, p3.inputs["Base Color"])
plane.data.materials.append(m3)

# --- torus with noise-driven base color ---
bpy.ops.mesh.primitive_torus_add(location=(-2, 2, 1))
torus = bpy.context.object
m4 = bpy.data.materials.new("noisy")
m4.use_nodes = True
t4 = m4.node_tree
p4 = t4.nodes["Principled BSDF"]
noise = t4.nodes.new("ShaderNodeTexNoise")
noise.inputs["Scale"].default_value = 4.0
noise.inputs["Detail"].default_value = 6.0
t4.links.new(noise.outputs["Color"], p4.inputs["Base Color"])
torus.data.materials.append(m4)

bpy.ops.object.camera_add(location=(6, -6, 4))
cam = bpy.context.object
cam.rotation_euler = (1.2, 0, 0.8)
scene.camera = cam

dg = bpy.context.evaluated_depsgraph_get()
rdla = exporter.export_scene(scene, dg, FakeSettings(), FakePrefs(), "/tmp/mat_test.exr")
text = open(rdla).read()
checks = {
    "ImageNormalMap": "ImageNormalMap(" in text,
    "input_normal bind": '["input_normal"] = bind(ImageNormalMap(' in text,
    "normal dial 0.8": '["input_normal_dial"] = 0.8' in text,
    "DwaMixMaterial": "DwaMixMaterial(" in text,
    '["material"] ref': '["material"] = mat_' in text,
    '["mix"] = 0.35': '["mix"] = 0.349' in text,
    "static MixRGB baked 0.5,0,0.5": 'Rgb(0.5, 0, 0.5)' in text,
    "glossy roughness": '["roughness"] = 0.15' in text,
    "NoiseMap_v2": "NoiseMap_v2(" in text,
    "mapping offset": '["offset"] = Vec2(0.25, 0.75)' in text,
    "mapping scale": '["scale"] = Vec2(2, 3)' in text,
}
ok = True
for k, v in checks.items():
    print("CHECK %-30s: %s" % (k, "OK" if v else "MISSING"))
    ok = ok and v
print("RDLA bytes:", os.path.getsize(rdla))
sys.exit(0 if ok else 1)
