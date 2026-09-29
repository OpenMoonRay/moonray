# MoonRay Blender Integration (macOS)

This branch adds a Blender integration to the MoonRay render engine, plus
the system-compatibility fixes needed to build MoonRay on current macOS
(macOS 27 / Apple Silicon / AppleClang 21).

## What's here

| Path | Content |
|------|---------|
| `blender_addon/` | The Blender add-on (render engine integration) |
| `COMPATIBILITY.md` | All macOS compatibility fixes applied (10 items) |
| `patches/` | The openmoonray superbuild/preset changes used for the build |
| `install_addon.sh` | Symlinks the add-on into Blender |
| `build_moonray.sh` | Configures + builds MoonRay itself (`macos-release-ninja` preset) |
| `verify_moonray.sh` | Renders the official sphere test scene with the built binary |
| `finish_build_and_test.sh` | One-shot: wait for deps → build → verify → Blender E2E |
| `moonray_env.sh` | Terminal environment for running moonray/denoise directly |

## Build (macOS, Apple Silicon)

The engine repo itself uses DreamWorks' internal build system; the public
build lives in the [`OpenMoonRay/openmoonray`](https://github.com/OpenMoonRay/openmoonray)
superproject (this repo is its `moonray/moonray` submodule). Steps:

```bash
# 1. clone the superproject next to this checkout
git clone --recurse-submodules https://github.com/OpenMoonRay/openmoonray.git

# 2. build dependencies (patches/CMakeUserPresets.json + the superbuild
#    changes in patches/openmoonray-building-macOS.patch are applied to it)
mkdir -p installs/{bin,lib,include} build-deps
cmake -DSKIP_QT=ON ../openmoonray/building/macOS   # in build-deps/
cmake --build .                                     # ~2-4 h, serial chain

# 3. build MoonRay
#    (copy patches/CMakeUserPresets.json into openmoonray/ first)
cd openmoonray && cmake --preset macos-release-ninja
cmake --build --preset macos-release-ninja
```

See `COMPATIBILITY.md` for the reasoning behind each patch.

## Blender add-on

- Registers **MoonRay** as a render engine (F12 / Render Image button /
  animation rendering).
- Compiles Blender shader-node graphs to MoonRay Dwa materials (Principled,
  Diffuse, Glossy, Glass, Transparent, Emission, Mix/Add Shader, image
  textures, normal maps, procedural noise, static baking of color/scalar
  subgraphs, texture Mapping nodes).
- Exports meshes (UVs/normals), instancing, lights, camera (DOF, shift),
  world (constant or HDRI), optional motion blur, optional OIDN denoise.
- The intermediate `.rdla` scene is temporary by default and kept only when
  **Save RDLA Scene** is enabled.

Install and test:

```bash
./install_addon.sh
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
    --python blender_addon/tests/test_render.py -- /tmp/render.png
```

Full test instructions in `blender_addon/README.md`.
