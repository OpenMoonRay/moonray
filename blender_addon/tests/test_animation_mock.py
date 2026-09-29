import os
import shutil
import sys
import tempfile

import bpy

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
tmp = tempfile.mkdtemp(prefix="moonray_anim_")
pkg = os.path.join(tmp, "moonray_blender")
shutil.copytree(HERE, pkg, ignore=shutil.ignore_patterns("tests", "__pycache__"))
sys.path.insert(0, tmp)
bpy.ops.preferences.addon_enable(module="moonray_blender")

root = os.path.join(tmp, "mock_install")
bin_dir = os.path.join(root, "bin")
os.makedirs(bin_dir)
mock = os.path.join(bin_dir, "moonray")
with open(mock, "w") as f:
    f.write("#!/usr/bin/env python3\n")
    f.write("import sys\n")
    f.write("sys.path.insert(0, %r)\n" % os.path.join(HERE, "tests"))
    f.write("from mock_moonray import main\n")
    f.write("sys.exit(main())\n")
os.chmod(mock, 0o755)

prefs = bpy.context.preferences.addons["moonray_blender"].preferences
prefs.moonray_root = root

scene = bpy.context.scene
scene.render.engine = "MOONRAY_RENDER"
scene.render.resolution_x = 96
scene.render.resolution_y = 64
scene.render.resolution_percentage = 100
scene.frame_start = 1
scene.frame_end = 3
scene.render.filepath = os.path.join(tmp, "anim_")
scene.render.image_settings.file_format = "PNG"

# animate the cube so frames differ
cube = bpy.context.scene.objects.get("Cube")
if cube is None:
    bpy.ops.mesh.primitive_cube_add()
    cube = bpy.context.object
cube.keyframe_insert("location", frame=1)
cube.location = (2, 0, 0)
cube.keyframe_insert("location", frame=3)

settings = scene.moonray
settings.threads = 2

bpy.ops.render.render(animation=True)
frames = sorted(f for f in os.listdir(tmp) if f.startswith("anim_") and f.endswith(".png"))
print("ANIM FRAMES:", len(frames), frames)
ok = len(frames) == 3 and all(os.path.getsize(os.path.join(tmp, f)) > 500 for f in frames)
print("ANIMATION E2E:", "OK" if ok else "FAIL")
bpy.ops.preferences.addon_disable(module="moonray_blender")
sys.exit(0 if ok else 1)
