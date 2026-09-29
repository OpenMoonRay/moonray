"""Locate and drive the moonray command-line renderer."""

import os
import re
import subprocess
import threading

MOONRAY_BIN = "moonray"
DENOISE_BIN = "denoise"


def resolve_moonray_root(moonray_root):
    """Return (root, error). Root is the directory containing bin/moonray."""
    candidates = []
    if moonray_root:
        candidates.append(moonray_root)
    if os.environ.get("MOONRAY_ROOT"):
        candidates.append(os.environ["MOONRAY_ROOT"])
    for root in candidates:
        root = os.path.expanduser(root)
        if os.path.isfile(os.path.join(root, "bin", MOONRAY_BIN)):
            return root, None
        if os.path.isfile(os.path.join(root, MOONRAY_BIN)):
            return root, None
    return (os.path.expanduser(moonray_root or "") or "",
            "bin/moonray not found (tried: %s)" % ", ".join(candidates)
            if candidates else "no MoonRay installation path configured")


def build_env(moonray_root, installs_root=""):
    """Environment needed by moonray at runtime (mirrors scripts/setup.sh)."""
    env = os.environ.copy()
    root = moonray_root

    env["PATH"] = os.path.join(root, "bin") + os.pathsep + env.get("PATH", "")
    env["RDL2_DSO_PATH"] = os.path.join(root, "rdl2dso")
    env["REZ_MOONRAY_ROOT"] = root
    env["ARRAS_SESSION_PATH"] = os.path.join(root, "sessions")
    env["MOONRAY_CLASS_PATH"] = os.path.join(root, "shader_json")
    env["PXR_PLUGINPATH_NAME"] = os.path.join(root, "plugin", "pxr")
    env["PXR_PLUGIN_PATH"] = os.path.join(root, "plugin", "pxr")

    # python modules (USD bindings etc.)
    py_paths = []
    if installs_root:
        py_paths += [os.path.join(installs_root, "lib", "python"),
                     os.path.join(installs_root, "lib64", "python3.9",
                                  "site-packages")]
    py_paths.append(os.path.join(root, "lib", "python"))
    for p in py_paths:
        if os.path.isdir(p):
            env["PYTHONPATH"] = p + os.pathsep + env.get("PYTHONPATH", "")

    # dynamic libraries (dependencies in installs/lib, moonray libs)
    lib_dirs = []
    if installs_root:
        lib_dirs.append(os.path.join(installs_root, "lib"))
    lib_dirs.append(os.path.join(root, "lib"))
    existing = [d for d in lib_dirs if os.path.isdir(d)]
    if existing:
        env["DYLD_LIBRARY_PATH"] = os.pathsep.join(existing) + os.pathsep + \
            env.get("DYLD_LIBRARY_PATH", "")
    return env


class MoonRayProcess:
    """Runs moonray and streams progress."""

    _PROGRESS_RE = re.compile(r"Rendering\s+\[\s*(\d+)%\]")

    def __init__(self, moonray_root, installs_root):
        self.root = moonray_root
        self.installs_root = installs_root
        self.proc = None
        self.error_lines = []
        self.progress = 0.0
        self._stdout_thread = None
        self._stderr_thread = None

    @property
    def moonray_bin(self):
        return os.path.join(self.root, "bin", MOONRAY_BIN)

    @property
    def denoise_bin(self):
        return os.path.join(self.root, "bin", DENOISE_BIN)

    def launch(self, args, progress_cb=None):
        env = build_env(self.root, self.installs_root)
        cmd = [self.moonray_bin] + list(args)
        self.proc = subprocess.Popen(
            cmd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._stdout_thread = threading.Thread(
            target=self._pump, args=(self.proc.stdout, True, progress_cb),
            daemon=True)
        self._stderr_thread = threading.Thread(
            target=self._pump, args=(self.proc.stderr, False, None),
            daemon=True)
        self._stdout_thread.start()
        self._stderr_thread.start()

    def _pump(self, stream, is_stdout, progress_cb):
        """Read raw chunks; moonray prints progress with \\r, not \\n."""
        buf = b""
        try:
            while True:
                chunk = stream.read(4096)
                if not chunk:
                    break
                buf += chunk
                if len(buf) > 1 << 16:
                    buf = buf[-4096:]
                text = buf.decode("utf-8", errors="replace")
                if is_stdout and progress_cb is not None:
                    for m in self._PROGRESS_RE.finditer(text):
                        pct = int(m.group(1))
                        if 0 <= pct <= 100:
                            self.progress = pct
                            progress_cb(pct)
                elif not is_stdout:
                    for line in text.splitlines():
                        line = line.strip()
                        if line and not line.startswith("Rendering"):
                            self.error_lines.append(line)
                            if len(self.error_lines) > 200:
                                self.error_lines.pop(0)
        except (ValueError, OSError):
            pass

    def wait(self):
        return self.proc.wait()

    def kill(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()

    def run_denoise(self, in_path, out_path):
        env = build_env(self.root, self.installs_root)
        cmd = [self.denoise_bin, "-in", in_path, "-out", out_path,
               "-mode", "oidn_cpu"]
        proc = subprocess.Popen(
            cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            universal_newlines=True)
        _out, err = proc.communicate()
        if proc.returncode != 0:
            raise RuntimeError("denoise failed: %s" % err.strip())
        return out_path
