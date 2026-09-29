# ##### BEGIN GPL LICENSE BLOCK #####
#
#  MoonRay for Blender
#  Integrates the DreamWorks MoonRay production path tracer into Blender.
#  Copyright (C) 2026  MoonRay Blender contributors
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# ##### END GPL LICENSE BLOCK #####

bl_info = {
    "name": "MoonRay Render",
    "author": "MoonRay Blender contributors",
    "version": (0, 2, 0),
    "blender": (4, 0, 0),
    "location": "Render Properties > Render Engine",
    "description": "Render with the DreamWorks MoonRay production path tracer "
                   "(scene export to MoonRay RDLA + moonray CLI)",
    "category": "Render",
    "support": "COMMUNITY",
}

import bpy

from . import properties
from . import operators
from . import engine
from . import ui


classes = (
    properties.MoonRayAddonPreferences,
    properties.MoonRayRenderSettings,
    operators.MOONRAY_OT_export_scene,
    operators.MOONRAY_OT_render,
    operators.MOONRAY_OT_open_moonray_root,
    ui.MOONRAY_PT_render_panel,
    engine.MoonRayRenderEngine,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    properties.register()


def unregister():
    properties.unregister()
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
