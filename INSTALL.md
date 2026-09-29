# Installing MoonRay (macOS / Apple Silicon)

Build-from-source instructions for this fork. The fork adds a Blender add-on
and a handful of macOS build patches on top of
[OpenMoonRay/openmoonray](https://github.com/OpenMoonRay/openmoonray); the
render engine itself is upstream.

The paths below use `<WORKSPACE>` as a placeholder. The machine this fork was
developed on uses:

```
<WORKSPACE> = /Users/faputa/Documents/wave-tracer
```

## Layout

```
<WORKSPACE>/
  moonray/                  # this fork (blender_addon/, patches/, scripts)
    openmoonray/            # OpenMoonRay superproject (nested checkout)
  installs/                 # dependencies + final moonray install
  build/                    # main build tree
  build-deps/               # dependency superbuild tree
```

## Requirements

- Apple M-series Mac, macOS (tested on macOS 27 / Tahoe, Xcode 26.6 CLT).
- Xcode Command Line Tools (`xcode-select --install`).
  - Full-Xcode-only Metal toolchain is NOT needed — this fork builds with
    `MOONRAY_USE_METAL=OFF`.
- CMake 4.x (Ninja generator).
- Git (Git LFS for upstream test assets is optional).
- Blender 4.0+ (tested 5.2 alpha) for the add-on.
- ~17 GB disk, 24 GB RAM recommended.

## Step 1 — Check out

```bash
mkdir -p <WORKSPACE>/installs/{bin,lib,include}
cd <WORKSPACE>
git clone --recurse-submodules https://github.com/OpenMoonRay/openmoonray.git \
    moonray/openmoonray
# replace the engine submodule with this fork (branch blender-addon)
git clone https://github.com/SakuraEntropia/moonray.git moonray
```

If `openmoonray` was cloned first, point its `moonray/moonray` submodule at
this fork's `blender-addon` branch, or simply keep the two checkouts side by
side as shown above and let the superproject reference the fork.

## Step 2 — Apply the patches

This fork fixes several macOS/clang-21/CMake-4 build issues. Copy the presets
and apply every patch in `moonray/patches/`:

```bash
cd moonray/openmoonray
cp ../patches/CMakeUserPresets.json CMakeUserPresets.json
for p in ../patches/*.patch; do
  git apply "$p"
done
```

What they do (details in [`COMPATIBILITY.md`](COMPATIBILITY.md)):

- `openmoonray-building-macOS.patch` — `SKIP_QT` option (Qt 5.12 is
  unbuildable on clang 21 and unused), memory-bounded parallelism.
- `openmoonray-moonray-CMakeLists.patch` — skip unit-test subdirectory.
- `*-ispc-ninja.patch` — custom ISPC command path for the Ninja generator
  (MoonRay's built-in ISPC language support does not emit the required stub
  headers).
- `openmoonray-ninja-duplicate-output.patch` — drop a duplicate BYPRODUCTS
  line in MoonrayDso.cmake.
- `openmoonray-codesign-ninja.patch` — codesign the `rdl2_ispc_util` target by
  its real path instead of a broken glob.
- `CMakeUserPresets.json` — adds `macos-release-ninja` (Ninja, no Qt,
  `BUILD_TESTING=OFF`, `MOONRAY_USE_METAL=OFF`, `DEPS_ROOT`/`TBB_ROOT`
  pointing at `<WORKSPACE>/installs`). Edit `DEPS_ROOT`/`BUILD_DIR` if your
  workspace differs.

## Step 3 — Build dependencies

```bash
mkdir -p <WORKSPACE>/build-deps
cd <WORKSPACE>/build-deps
cmake ../moonray/openmoonray/building/macOS -DSKIP_QT=ON
cmake --build .
```

This compiles Boost, USD, OpenEXR, TBB, OpenSubdiv, OpenVDB, OIIO and friends
into `<WORKSPACE>/installs`. Takes a long while (hours). Keep Anaconda/conda
off `PATH` and `CMAKE_PREFIX_PATH` — see the OpenColorIO note in
[`COMPATIBILITY.md`](COMPATIBILITY.md).

## Step 4 — Build MoonRay

```bash
cd <WORKSPACE>/moonray
./build_moonray.sh
```

This configures `macos-release-ninja` and builds/installs `moonray` into
`<WORKSPACE>/installs/openmoonray`.

## Step 5 — Verify

```bash
cd <WORKSPACE>/moonray
./verify_moonray.sh
```

Renders the official `sphere.rdla` test scene. "Wrote …/sphere.exr" and
exit 0 means the install is good.

## Step 6 — Run from the shell (optional)

```bash
source <WORKSPACE>/moonray/moonray_env.sh
moonray -in <scene>.rdla -out <out>.exr
```

`moonray_env.sh` sets `PATH`, `RDL2_DSO_PATH`, `PYTHONPATH` and
`DYLD_LIBRARY_PATH`.

## Step 7 — Install the Blender add-on

```bash
cd <WORKSPACE>/moonray
./install_addon.sh
```

Then in Blender: *Edit → Preferences → Add-ons → Render → MoonRay Render*,
enable it, and set **MoonRay Installation** to
`<WORKSPACE>/installs/openmoonray` and **Dependencies Install Root** to
`<WORKSPACE>/installs`. See
[`blender_addon/README.md`](blender_addon/README.md) for usage.

Alternatively install the prebuilt add-on zip
`moonray_blender-v0.2.0.zip` via *Edit → Preferences → Add-ons → Install…*.

## Known issues

All build fixes and gotchas are documented in
[`COMPATIBILITY.md`](COMPATIBILITY.md): generator mismatch, Qt, Anaconda PATH
pollution, TBB discovery, Metal toolchain, ISPC stubs, libc++ warnings.
