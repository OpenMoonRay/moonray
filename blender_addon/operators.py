"""Operators for the MoonRay add-on."""

import os
import subprocess

import bpy

from . import exporter

ADDON_ID = __package__.split(".")[0]


class MOONRAY_OT_export_scene(bpy.types.Operator):
    bl_idname = "moonray.export_scene"
    bl_label = "Export MoonRay Scene"
    bl_description = "Export the current scene to a MoonRay .rdla file"

    filepath: bpy.props.StringProperty(
        name="File Path", subtype="FILE_PATH")

    def invoke(self, context, event):
        blend = context.blend_data.filepath
        base = os.path.splitext(blend)[0] if blend else "untitled"
        self.filepath = base + ".rdla"
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        scene = context.scene
        prefs = context.preferences.addons.get(ADDON_ID)
        prefs = prefs.preferences if prefs is not None else None
        settings = scene.moonray
        depsgraph = context.evaluated_depsgraph_get()
        rdla = exporter.export_scene(
            scene, depsgraph, settings, prefs, self.filepath,
            report=lambda msg: self.report({"WARNING"}, msg))
        self.report({"INFO"}, "Exported %s" % rdla)
        return {"FINISHED"}


class MOONRAY_OT_render(bpy.types.Operator):
    bl_idname = "moonray.render"
    bl_label = "Render with MoonRay"
    bl_description = "Render the current scene with MoonRay (same as F12)"

    def execute(self, context):
        bpy.ops.render.render("INVOKE_DEFAULT")
        return {"FINISHED"}


class MOONRAY_OT_open_moonray_root(bpy.types.Operator):
    bl_idname = "moonray.open_moonray_root"
    bl_label = "Open MoonRay Installation Folder"
    bl_description = "Reveal the MoonRay installation folder in Finder"

    def execute(self, context):
        addon = context.preferences.addons.get(ADDON_ID)
        root = addon.preferences.moonray_root if addon is not None else ""
        root = os.path.expanduser(root)
        if not os.path.isdir(root):
            self.report({"ERROR"}, "Installation folder does not exist: %s" % root)
            return {"CANCELLED"}
        subprocess.Popen(["open", root])
        return {"FINISHED"}
