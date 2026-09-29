"""Render panel UI for the MoonRay add-on."""

import bpy


class MOONRAY_PT_render_panel(bpy.types.Panel):
    bl_label = "MoonRay"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "render"
    COMPAT_ENGINES = {"MOONRAY_RENDER"}

    @classmethod
    def poll(cls, context):
        return context.engine in cls.COMPAT_ENGINES

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        settings = context.scene.moonray

        col = layout.column(align=True)
        col.prop(settings, "pixel_samples")
        col.prop(settings, "min_adaptive_samples")
        col.prop(settings, "max_adaptive_samples")

        layout.separator()
        layout.prop(settings, "threads")
        layout.prop(settings, "use_progressive_tiles")

        layout.separator()
        col = layout.column(align=True)
        col.prop(settings, "pixel_filter")
        if settings.pixel_filter != "DEFAULT":
            col.prop(settings, "pixel_filter_width")

        layout.separator()
        col = layout.column(align=True)
        col.prop(settings, "use_denoise")
        col.prop(settings, "use_motion_blur")

        layout.separator()
        col = layout.column(align=True)
        col.prop(settings, "keep_rdla")
        if settings.keep_rdla:
            col.prop(settings, "rdla_path")
        col.prop(settings, "export_only")

        layout.separator()
        layout.operator("moonray.export_scene", text="Export .rdla Scene")

        layout.separator()
        row = layout.row(align=True)
        row.scale_y = 1.6
        row.operator("moonray.render", text="Render Image",
                     icon="RENDER_STILL")


def register():
    pass


def unregister():
    pass
