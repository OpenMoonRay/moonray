# MoonRay fork — Blender integration

This is a fork of [OpenMoonRay/moonray](https://github.com/OpenMoonRay/moonray)
(the DreamWorks / Academy Software Foundation production path tracer) adding a
**Blender add-on** that renders Blender scenes directly with the `moonray` CLI.

The engine code is unchanged from upstream; all additions live in
[`blender_addon/`](blender_addon/) on the `blender-addon` branch.

## The add-on

Registers **MoonRay** as a Blender render engine:

1. exports the Blender scene to MoonRay's RDLA format (meshes, UVs, normals,
   instancing, Cycles shader nodes, lights, camera, world),
2. runs `moonray`,
3. loads the EXR back into Blender's Render Result (F12 + animation),
4. optional OIDN denoise.

Feature highlights:

- Cycles-native node parity: Principled BSDF (IOR, clearcoat, sheen,
  subsurface, transmission, anisotropic, emission), Diffuse/Glossy/Glass/
  Refraction/Translucent/Anisotropic/Velvet/Toon/Subsurface Scattering,
  Blackbody, image + noise textures, normal maps.
- Lights: Point / Sun / Spot / Area (square/rect/disk/ellipse, spread,
  blackbody temperature).
- Emissive meshes become MoonRay MeshLights (real area lights).
- Progressive preview via MoonRay progress checkpoints.
- Multi-material meshes split per material slot.

Install: [`install_addon.sh`](blender_addon/) symlinks the add-on into
Blender's add-ons folder. Full docs in
[`blender_addon/README.md`](blender_addon/README.md).

## Building & installing MoonRay

Full build-from-source instructions: [`INSTALL.md`](INSTALL.md).
macOS build notes and patches: [`COMPATIBILITY.md`](COMPATIBILITY.md).

## Upstream

Governance, Code of Conduct, and Contribution policies live in the upstream
[OpenMoonRay/openmoonray](https://github.com/OpenMoonRay/openmoonray)
superproject.
