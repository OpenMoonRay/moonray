"""Blender RenderEngine integration: exports to RDLA and runs moonray.

NOTE: Blender 5.2 alpha's RenderEngine Python proxy raises
"ReferenceError: StructRNA ... has been removed" for two things:

1. accessing *custom Python methods* through ``self``
   (e.g. ``self._render_impl``), and
2. storing attributes on engine instances.

Only the built-in render methods (report/update_stats/test_break/
begin_result/end_result/...) work through ``self``. This engine therefore
keeps ALL state in local variables and all helper logic in module-level
functions that receive the engine instance explicitly.

IMPORTANT: the class must NOT define ``__init__``. A bare
``def __init__(self, *args): pass`` swallows Blender's struct-creation
call, leaving the engine unbound and every subsequent method call raising
ReferenceError. Let Blender's default constructor run instead.
"""

import os
import shutil
import tempfile
import time

import bpy

from . import exporter
from .renderer import MoonRayProcess, resolve_moonray_root

ADDON_ID = __package__.split(".")[0]


def _prefs():
    addon = bpy.context.preferences.addons.get(ADDON_ID)
    return addon.preferences if addon is not None else None


def _report_error(engine, msg):
    engine.report({"ERROR"}, msg)


def _to_combined_channels(exr_path, installs_root, moonray_root, engine,
                          channels=4, width=None, height=None):
    """Rename an EXR's channels to Combined.R/G/B/A so Blender can read it.

    Uses oiiotool from the dependency install when available; returns the
    input path unchanged otherwise (or if the conversion fails). oiiotool
    is searched in the dependencies install root and, as a fallback, next
    to the MoonRay install (installs/bin/oiiotool). Checkpoint images are
    3-channel (no alpha); pass channels=3 (plus width/height) to append a
    constant Combined.A so the composite does not warn about a missing
    alpha channel.
    """
    import subprocess as _sp
    candidates = []
    for base in (installs_root, os.path.dirname(moonray_root), moonray_root):
        if base:
            candidates.append(os.path.join(base, "bin", "oiiotool"))
    oiiotool = next((c for c in candidates if os.path.isfile(c)), None)
    if oiiotool is None:
        return exr_path
    dst = os.path.join(os.path.dirname(exr_path), "combined.exr")
    if channels >= 4:
        cmd = [oiiotool, exr_path, "--chnames",
               "Combined.R,Combined.G,Combined.B,Combined.A", "-o", dst]
    else:
        w = int(width or 1)
        h = int(height or 1)
        cmd = [oiiotool, exr_path, "--chnames",
               "Combined.R,Combined.G,Combined.B",
               "--pattern", "constant:color=1.0", "%dx%d" % (w, h), "1",
               "--chnames", "Combined.A", "--chappend", "-o", dst]
    try:
        proc = _sp.run(cmd, stdout=_sp.PIPE, stderr=_sp.PIPE)
        if proc.returncode == 0 and os.path.isfile(dst):
            return dst
    except OSError:
        pass
    engine.report({"WARNING"}, "Could not convert render channels to "
                               "Combined.*; result may be empty")
    return exr_path


def _keep_rdla(engine, rdla_path, scene, settings):
    """Copy the temporary RDLA scene to the user-chosen location."""
    if not settings.keep_rdla:
        return
    target = settings.rdla_path
    if not target:
        out = scene.render.filepath
        if not out:
            return
        target = os.path.splitext(out)[0] + ".rdla"
    try:
        target_dir = os.path.dirname(os.path.abspath(target))
        if target_dir and not os.path.isdir(target_dir):
            os.makedirs(target_dir, exist_ok=True)
        shutil.copyfile(rdla_path, target)
        engine.report({"INFO"}, "Saved RDLA scene to %s" % target)
    except Exception as e:
        engine.report({"WARNING"}, "Could not save RDLA scene: %s" % e)


def _render_impl(engine, depsgraph):
    scene = depsgraph.scene_eval
    settings = scene.moonray
    prefs = _prefs()

    if prefs is None:
        _report_error(engine, "MoonRay add-on preferences not found")
        return

    root, err = resolve_moonray_root(prefs.moonray_root)
    if err and getattr(prefs, "auto_detect", False):
        from . import properties as _props
        for cand in _props.auto_detect_candidates():
            r, e2 = resolve_moonray_root(cand)
            if not e2:
                root, err = r, None
                break
    if err:
        _report_error(engine, "MoonRay not found (%s). Set the correct "
                              "installation path in the add-on preferences."
                              % err)
        return

    w = max(1, int(scene.render.resolution_x
                   * scene.render.resolution_percentage / 100.0))
    h = max(1, int(scene.render.resolution_y
                   * scene.render.resolution_percentage / 100.0))

    tmpdir = tempfile.mkdtemp(prefix="moonray_")
    out_exr = os.path.join(tmpdir,
                           "frame_%04d.exr" % scene.frame_current)

    def cleanup():
        if tmpdir and not (prefs.debug_keep_files):
            shutil.rmtree(tmpdir, ignore_errors=True)

    # 1. export the scene to RDLA
    engine.update_stats("Exporting", "MoonRay: writing scene")
    try:
        rdla_path = exporter.export_scene(
            scene, depsgraph, settings, prefs, out_exr,
            report=lambda msg: engine.report({"WARNING"}, msg))
    except Exception as e:
        _report_error(engine, "Export failed: %s" % e)
        cleanup()
        return

    if settings.export_only:
        engine.report({"INFO"}, "Exported scene to %s" % rdla_path)
        _keep_rdla(engine, rdla_path, scene, settings)
        cleanup()
        return

    # optionally persist the intermediate RDLA scene
    _keep_rdla(engine, rdla_path, scene, settings)

    # 2. render with the moonray CLI
    # derive the dependencies install root from the MoonRay root when the
    # user left it empty (MoonRay root is <installs>/openmoonray, so its
    # parent is <installs>); needed for DYLD_LIBRARY_PATH and oiiotool.
    installs_root = prefs.installs_root
    if not installs_root:
        parent = os.path.dirname(root)
        if os.path.isdir(os.path.join(parent, "lib")):
            installs_root = parent
    proc = MoonRayProcess(root, installs_root)
    # -info makes moonray emit "Rendering [ N%]" progress lines on stdout,
    # which on_progress() parses; without it the status bar would stay
    # stuck on "writing scene" for the whole render.
    args = ["-in", rdla_path, "-out", out_exr, "-info"]
    if getattr(settings, "use_progressive", False):
        args.append("-checkpoint")
    if settings.threads > 0:
        args += ["-threads", str(settings.threads)]

    def on_progress(pct):
        engine.update_progress(pct / 100.0)
        engine.update_stats("Rendering", "MoonRay: %d%%" % pct)

    try:
        proc.launch(args, progress_cb=on_progress)
    except OSError as e:
        _report_error(engine, "Could not launch moonray: %s" % e)
        cleanup()
        return

    engine.update_stats("Rendering", "MoonRay: 0%")

    # progressive preview: poll MoonRay's checkpoint file and push it into
    # the render result as it is overwritten (real-time feedback)
    checkpoint_path = out_exr + ".checkpoint.exr"
    last_sig = None
    prog_result = None

    def _push_checkpoint():
        nonlocal last_sig, prog_result
        if not os.path.isfile(checkpoint_path):
            return
        try:
            st = os.stat(checkpoint_path)
            sig = (st.st_size, st.st_mtime_ns)
        except OSError:
            return
        if sig == last_sig:
            return
        last_sig = sig
        combined = _to_combined_channels(
            checkpoint_path, installs_root, root, engine, channels=3,
            width=w, height=h)
        if prog_result is None:
            prog_result = engine.begin_result(0, 0, w, h)
        layer = prog_result.layers[0]
        try:
            layer.load_from_file(combined)
            engine.update_result(prog_result)
        except Exception:
            pass

    rc = 0
    try:
        while proc.proc.poll() is None:
            if engine.test_break():
                proc.kill()
                cleanup()
                return
            if getattr(settings, "use_progressive", False):
                _push_checkpoint()
            time.sleep(0.1)
        rc = proc.proc.returncode
    finally:
        pass

    if rc != 0:
        tail = "\n".join(proc.error_lines[-10:])
        _report_error(engine, "moonray failed (exit code %d).\n%s"
                              % (rc, tail))
        cleanup()
        return
    engine.update_progress(1.0)

    final = out_exr
    if settings.use_denoise and os.path.isfile(proc.denoise_bin):
        engine.update_stats("Denoising", "MoonRay: OIDN denoise")
        denoised = os.path.join(tmpdir, "denoised.exr")
        try:
            proc.run_denoise(out_exr, denoised)
            final = denoised
        except Exception as e:
            engine.report({"WARNING"}, "Denoise failed (%s); "
                                       "using raw render" % e)

    # MoonRay writes beauty channels as R/G/B/A, but Blender's
    # RenderLayer.load_from_file expects "Combined.R/G/B/A" (otherwise the
    # final composite is silently black). Rename the channels first.
    final = _to_combined_channels(final, installs_root, root, engine)

    # 3. load the result into the Render Result
    if prog_result is None:
        prog_result = engine.begin_result(0, 0, w, h)
    result = prog_result
    if not result.layers:
        _report_error(engine, "No render layers available for the result")
        engine.end_result(result)
        cleanup()
        return
    layer = result.layers[0]
    try:
        layer.load_from_file(final)
    except Exception as e:
        _report_error(engine, "Could not read render output: %s" % e)
    engine.end_result(result)
    cleanup()


class MoonRayRenderEngine(bpy.types.RenderEngine):
    bl_idname = "MOONRAY_RENDER"
    bl_label = "MoonRay"
    bl_use_preview = False
    bl_use_shading_nodes = True
    bl_use_shading_nodes_custom = False

    # -- RenderEngine API --------------------------------------------------
    def render(self, depsgraph):
        try:
            _render_impl(self, depsgraph)
        except ReferenceError:
            # Blender 5.2 alpha invokes render() a second time after the
            # engine struct has been released; nothing can be done then.
            pass
