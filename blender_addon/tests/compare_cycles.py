"""Compare a MoonRay render against the Cycles render of the same scene.

Defines a multi-term loss so the difference between the two renderers is
quantified instead of eyeballed. Both images are compared in linear RGB
(Blender's default "Standard" view transform, so the EXRs are linear).

Loss terms (all computed on the full-resolution float pixels):
  L_mse   : mean squared error over linear RGB (penalizes any difference).
  L_mae   : mean absolute error over linear RGB (robust to outliers).
  L_luma  : MSE over luminance (brightness match, ignoring chroma).
  L_chroma: mean angular error of normalized RGB (color-only match).

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --factory-startup --python blender_addon/tests/compare_cycles.py -- \
      <ref.exr> <candidate.exr>
"""

import math
import sys

import bpy


def _to_linear(pixel):
    # Blender stores EXR pixels already linear; keep as-is.
    return pixel


def compute_loss(ref_px, cand_px, w, h):
    n = w * h
    assert len(ref_px) == n * 4 and len(cand_px) == n * 4, \
        "resolution mismatch: ref=%d cand=%d" % (len(ref_px), len(cand_px))

    se = [0.0, 0.0, 0.0]
    ae = [0.0, 0.0, 0.0]
    lse = 0.0
    chroma = 0.0
    for i in range(n):
        r0, g0, b0 = ref_px[4 * i], ref_px[4 * i + 1], ref_px[4 * i + 2]
        r1, g1, b1 = cand_px[4 * i], cand_px[4 * i + 1], cand_px[4 * i + 2]
        d = (r0 - r1, g0 - g1, b0 - b1)
        se[0] += d[0] * d[0]
        se[1] += d[1] * d[1]
        se[2] += d[2] * d[2]
        ae[0] += abs(d[0])
        ae[1] += abs(d[1])
        ae[2] += abs(d[2])
        l0 = 0.2126 * r0 + 0.7152 * g0 + 0.0722 * b0
        l1 = 0.2126 * r1 + 0.7152 * g1 + 0.0722 * b1
        lse += (l0 - l1) ** 2
        # angular error between normalized colors (guards zero-vectors)
        n0 = math.sqrt(r0 * r0 + g0 * g0 + b0 * b0)
        n1 = math.sqrt(r1 * r1 + g1 * g1 + b1 * b1)
        if n0 > 1e-6 and n1 > 1e-6:
            dot = (r0 * r1 + g0 * g1 + b0 * b1) / (n0 * n1)
            dot = max(-1.0, min(1.0, dot))
            chroma += math.acos(dot)

    mse = sum(se) / (3 * n)
    mae = sum(ae) / (3 * n)
    luma = lse / n
    chroma = chroma / n
    return {
        "mse": mse,
        "mae": mae,
        "luma_mse": luma,
        "chroma_rad": chroma,
        "chroma_deg": math.degrees(chroma),
        "per_channel_mae": [x / n for x in ae],
    }


def load(path):
    img = bpy.data.images.load(path)
    px = list(img.pixels)
    w, h = img.size[0], img.size[1]
    bpy.data.images.remove(img)
    return px, w, h


def main(ref_path, cand_path):
    ref_px, w, h = load(ref_path)
    cand_px, cw, ch = load(cand_path)
    if (w, h) != (cw, ch):
        print("WARNING: size mismatch ref=%dx%d cand=%dx%d"
              % (w, h, cw, ch))
        # downsample comparison to the smaller size
        w = h = min(w, h, cw, ch)
        ref_px = ref_px[: w * h * 4]
        cand_px = cand_px[: w * h * 4]
    loss = compute_loss(ref_px, cand_px, w, h)
    print("=== LOSS (linear RGB, %dx%d) ===" % (w, h))
    print("L_mse    = %.6f" % loss["mse"])
    print("L_mae    = %.6f" % loss["mae"])
    print("L_luma   = %.6f" % loss["luma_mse"])
    print("L_chroma = %.6f rad (%.3f deg)" % (loss["chroma_rad"],
                                               loss["chroma_deg"]))
    print("per-channel MAE R/G/B = %.4f / %.4f / %.4f"
          % tuple(loss["per_channel_mae"]))
    return loss


if __name__ == "__main__":
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    if len(argv) < 2:
        print("usage: compare_cycles.py -- <ref.exr> <candidate.exr>")
        sys.exit(2)
    main(argv[0], argv[1])
