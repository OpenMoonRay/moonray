#!/usr/bin/env python3
"""Unit test for renderer.py process plumbing using the mock moonray binary.

Run with the system python3 (renderer.py has no bpy dependency):
  python3 blender_addon/tests/test_renderer.py
"""

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(HERE)
sys.path.insert(0, ADDON_DIR)

from renderer import MoonRayProcess  # noqa: E402


def main():
    tmp = tempfile.mkdtemp(prefix="moonray_mock_")
    bin_dir = os.path.join(tmp, "bin")
    os.makedirs(bin_dir)
    mock = os.path.join(bin_dir, "moonray")
    with open(mock, "w") as f:
        f.write("#!/usr/bin/env python3\n")
        f.write("import sys\n")
        f.write("sys.path.insert(0, %r)\n" % os.path.join(ADDON_DIR, "tests"))
        f.write("from mock_moonray import main\n")
        f.write("sys.exit(main())\n")
    os.chmod(mock, 0o755)

    out_path = os.path.join(tmp, "render.exr")
    proc = MoonRayProcess(tmp, "")
    progress = []

    proc.launch(["-in", "scene.rdla", "-out", out_path],
                progress_cb=lambda p: progress.append(p))
    rc = proc.wait()
    print("exit code:", rc)
    print("progress updates:", len(progress),
          "last:", progress[-1] if progress else None)
    print("monotonic:", progress == sorted(progress) and progress)
    print("output written:", os.path.isfile(out_path),
          os.path.getsize(out_path) if os.path.isfile(out_path) else 0)
    ok = (rc == 0 and progress and progress[-1] == 100
          and os.path.isfile(out_path))
    print("RESULT:", "OK" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
