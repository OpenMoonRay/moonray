"""Add-on preferences and per-scene MoonRay render settings."""

import os

import bpy
from bpy.props import (
    BoolProperty,
    EnumProperty,
    FloatProperty,
    IntProperty,
    PointerProperty,
    StringProperty,
)

from . import renderer

ADDON_ID = __package__.split(".")[0]


def auto_detect_candidates():
    """Candidate MoonRay installation roots, best guess first."""
    out = [_default_moonray_root(),
           "/Applications/MoonRay/installs/openmoonray",
           os.path.expanduser("~/moonray/installs/openmoonray")]
    env_root = os.environ.get("MOONRAY_ROOT")
    if env_root:
        out.insert(0, env_root)
    seen = set()
    return [c for c in out if c and not (c in seen or seen.add(c))]


def _default_moonray_root():
    # Where this add-on source tree lives inside the moonray workspace:
    #   <workspace>/blender_addon  ->  <workspace>/../installs/openmoonray
    # realpath() resolves the add-on symlink so this works even when the
    # add-on is installed as a symlink into Blender's add-ons directory.
    try:
        here = os.path.dirname(os.path.realpath(__file__))
        candidate = os.path.normpath(os.path.join(here, "..", "..", "installs", "openmoonray"))
        if os.path.isdir(candidate):
            return candidate
    except Exception:
        pass
    return "/Applications/MoonRay/installs/openmoonray"


class MoonRayAddonPreferences(bpy.types.AddonPreferences):
    bl_idname = ADDON_ID

    moonray_root: StringProperty(
        name="MoonRay Installation",
        description="Root of an installed MoonRay build (the directory that "
                    "contains bin/, lib/, rdl2dso/, sessions/, ...)",
        subtype="DIR_PATH",
        default=_default_moonray_root(),
    )
    installs_root: StringProperty(
        name="Dependencies Install Root",
        description="Directory that contains the MoonRay third-party "
                    "dependencies (lib/, include/, ...). Usually the parent "
                    "of the MoonRay installation root",
        subtype="DIR_PATH",
        default="",
    )
    light_scale: FloatProperty(
        name="Light Intensity Scale",
        description="Global multiplier applied to every exported light "
                    "intensity (defaults map Blender watts/energy to MoonRay "
                    "radiance approximately)",
        default=1.0,
        min=0.0,
        soft_max=100.0,
        precision=3,
    )
    debug_keep_files: BoolProperty(
        name="Keep Export Files",
        description="Do not delete the generated .rdla scene and temporary "
                    "render output (useful for debugging the exporter)",
        default=False,
    )
    auto_detect: BoolProperty(
        name="Auto-detect Installation",
        description="Try to locate the MoonRay installation automatically",
        default=True,
    )

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "moonray_root")
        layout.prop(self, "installs_root")
        layout.prop(self, "light_scale")
        layout.prop(self, "debug_keep_files")
        box = layout.box()
        box.label(text="MoonRay binary status:")
        root, err = renderer.resolve_moonray_root(self.moonray_root)
        if err:
            box.label(text="Not found: %s" % err, icon="ERROR")
        else:
            box.label(text=os.path.join(root, "bin", "moonray"), icon="CHECKMARK")
        box.operator("moonray.open_moonray_root", text="Open Installation Folder")


class MoonRayRenderSettings(bpy.types.PropertyGroup):
    pixel_samples: IntProperty(
        name="Pixel Samples",
        description="Square root of the number of samples per pixel "
                    "(MoonRay 'pixel_samples': 8 means 64 spp)",
        default=8,
        min=1,
        max=256,
    )
    min_adaptive_samples: IntProperty(
        name="Min Adaptive Samples",
        description="Minimum adaptive samples per pixel",
        default=16,
        min=1,
        max=4096,
    )
    max_adaptive_samples: IntProperty(
        name="Max Adaptive Samples",
        description="Maximum adaptive samples per pixel",
        default=4096,
        min=1,
        max=262144,
    )
    threads: IntProperty(
        name="Render Threads",
        description="Number of CPU threads used by moonray (0 = auto)",
        default=0,
        min=0,
        max=1024,
    )
    use_progressive_tiles: BoolProperty(
        name="Progressive Tile Order",
        description="Use MoonRay's progressive tile ordering",
        default=False,
    )
    pixel_filter: EnumProperty(
        name="Pixel Filter",
        description="MoonRay pixel reconstruction filter",
        items=[
            ("DEFAULT", "Default (Cubic B-Spline)", "MoonRay default filter"),
            ("BOX", "Box", "Box filter"),
            ("CUBIC", "Cubic B-Spline", "Cubic B-spline filter"),
            ("QUADRATIC", "Quadratic B-Spline", "Quadratic B-spline filter"),
        ],
        default="DEFAULT",
    )
    pixel_filter_width: FloatProperty(
        name="Pixel Filter Width",
        description="Width of the pixel filter",
        default=3.0,
        min=0.5,
        soft_max=6.0,
    )
    use_denoise: BoolProperty(
        name="Denoise",
        description="Denoise the finished render with MoonRay's built-in "
                    "OpenImageDenoise tool (denoise -mode oidn_cpu)",
        default=False,
    )
    use_progressive: BoolProperty(
        name="Progressive Preview",
        description="Write MoonRay progress checkpoints and update the "
                    "render result as they arrive (real-time preview)",
        default=True,
    )
    export_only: BoolProperty(
        name="Export Only",
        description="Only export the .rdla scene and skip rendering "
                    "(useful for debugging)",
        default=False,
    )
    keep_rdla: BoolProperty(
        name="Save RDLA Scene",
        description="Keep the intermediate .rdla scene file after rendering "
                    "(next to the render output, or at the path below). "
                    "Off: the scene is written to a temporary file and "
                    "deleted automatically",
        default=False,
    )
    use_motion_blur: BoolProperty(
        name="Motion Blur",
        description="Export vertex velocities and camera motion blur "
                    "(shutter: one Blender frame)",
        default=False,
    )
    rdla_path: StringProperty(
        name="RDLA Path",
        description="Optional path for the kept .rdla scene. Empty uses "
                    "the render output path with an .rdla extension",
        subtype="FILE_PATH",
        default="",
    )


def register():
    bpy.types.Scene.moonray = PointerProperty(type=MoonRayRenderSettings)


def unregister():
    del bpy.types.Scene.moonray
