# MoonRay for Blender

Blender integration for the [MoonRay](https://github.com/OpenMoonRay/openmoonray)
production path tracer (DreamWorks / Academy Software Foundation).

The add-on registers **MoonRay** as a render engine in Blender:

1. exports the Blender scene to MoonRay's RDLA scene format
   (meshes, UVs, normals, instancing, materials, lights, camera, world),
2. runs the `moonray` command-line renderer,
3. loads the result back into the Render Result (F12 / animation rendering),
4. optionally denoises with MoonRay's OIDN `denoise` tool.

## Requirements

- macOS (Apple Silicon) with a working MoonRay installation built with the
  official `macos-release` CMake preset (see `openmoonray/building/macOS`).
  Linux installations (Rocky Linux 9) should work as well; the add-on itself
  only shells out to the `moonray` binary.
- Blender 4.0 or newer (tested with Blender 5.2 alpha).

## Installation

1. `./install_addon.sh` (symlinks the add-on into Blender's add-ons folder)
2. In Blender: *Edit → Preferences → Add-ons → Render → MoonRay Render*,
   enable it and set:
   - **MoonRay Installation** — the directory containing `bin/moonray`
     (e.g. `/Users/<you>/Documents/wave-tracer/installs/openmoonray`)
   - **Dependencies Install Root** — the directory containing the
     third-party `lib/` (e.g. `/Users/<you>/Documents/wave-tracer/installs`)

## Usage

1. Switch the render engine to **MoonRay** in *Render Properties*.
2. Tune samples (MoonRay `pixel_samples` is the square root of the spp),
   threads, denoise, etc. in the *MoonRay* panel.
3. Render with the **Render Image** button in the panel, the regular
   *Render → Render Image* menu item, or F12. The scene is exported to a
   temporary `.rdla`, rendered, and the EXR is loaded into the Render
   Result. Animation rendering (Ctrl+F12) is supported frame by frame.

The intermediate `.rdla` scene file is deleted automatically after the
render; enable **Save RDLA Scene** in the panel to keep it (next to the
render output or at a custom path).

## Supported Blender features

| Feature            | Status                                             |
|--------------------|----------------------------------------------------|
| Meshes (quads/ngons, triangulated) | ✔ with UVs and split normals           |
| Curves/surfaces/text (via to_mesh) | ✔                                  |
| Instancing (linked duplicates) | ✔ exported as RdlInstancerGeometry            |
| Shader nodes       | ✔ Principled BSDF (full: IOR, clearcoat, sheen, subsurface, transmission, anisotropic, emission), Diffuse / Glossy / Glass / Refraction / Translucent / Anisotropic / Velvet / Toon / Subsurface Scattering / Emission / Mix Shader / Add Shader |
| Color/scalar nodes | ✔ static baking: Mix, Math, Gamma, Bright/Contrast, Hue/Sat, Invert, RGB→BW, ColorRamp, Map Range, Clamp, Blackbody |
| Textures           | ✔ image textures (ImageMap) + procedural noise (NoiseMap_v2) |
| Normal maps        | ✔ ImageNormalMap via the Normal Map node            |
| Point / Sun / Spot / Area lights | ✔ energy-based intensity mapping; area disk/ellipse + spread + temperature |
| Emissive meshes    | ✔ exported as MoonRay MeshLights (real area lights with shadows) |
| Multi-material meshes | ✔ split per material slot (per-face assignment kept) |
| Progressive preview | ✔ MoonRay progress checkpoints streamed to the Render Result |
| World background   | ✔ constant color or HDRI (Environment Texture node) |
| Depth of field     | ✔ (camera DOF settings)                          |
| Motion blur       | camera shutter + vertex velocities when Blender provides the velocity attribute (Blender 4.x; Blender 5.x currently skips object MB) |
| Volumetrics        | not yet                                    |

## Notes

- MoonRay is Y-up while Blender is Z-up; the exporter applies the standard
  axis conversion (`x, z, -y`) to all transforms.
- Light intensities are converted from Blender watts to MoonRay radiance-ish
  units; use the global *Light Intensity Scale* in the add-on preferences to
  compensate for scene scale.
- Packed image textures (without a file on disk) fall back to the
  material's base color.
- Bump nodes are approximated via normal strength (`input_normal_dial`).

## Tests

Headless test suite (run from this directory):

```
# exporter: full feature coverage (17 checks)
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
    --python blender_addon/tests/test_full_scene.py -- /tmp/full.exr
# material node compiler (11 checks)
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
    --python blender_addon/tests/test_materials.py
# Cycles node/lights parity (22 checks)
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
    --python blender_addon/tests/test_cycles_parity.py
# renderer-vs-Cycles loss (MSE/MAE/chroma) between two EXRs
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
    --python blender_addon/tests/compare_cycles.py -- ref.exr cand.exr
# engine end-to-end with a mock moonray binary
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
    --python blender_addon/tests/test_engine_mock.py -- /tmp/mock.png
# renderer process plumbing unit test
python3 blender_addon/tests/test_renderer.py
# real end-to-end render (requires a working MoonRay install)
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
    --python blender_addon/tests/test_render.py -- /tmp/render.png
```
