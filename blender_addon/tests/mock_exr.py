"""Minimal uncompressed RGBA EXR writer (pure python, no dependencies).

Just enough for Blender's RenderLayer.load_from_file() to accept the file.
"""

import struct


def write_exr(path, width, height, rgba_float_rows):
    """rgba_float_rows: list of rows, each row = list of [r, g, b, a] floats.
    Row 0 is the TOP scanline (y = height-1)."""
    out = bytearray()

    # magic + version
    out += struct.pack("<II", 0x01312f76, 2)

    # header attributes
    def attr(name, typ, data):
        nonlocal out
        out += name.encode() + b"\0" + typ.encode() + b"\0"
        out += struct.pack("<I", len(data)) + data

    channels = b""
    for ch in (b"B", b"G", b"R", b"A"):
        channels += ch + b"\0" + struct.pack("<I", 2)  # FLOAT
        channels += struct.pack("<BBBBii", 0, 0, 0, 0, 1, 1)
    channels += b"\0"
    attr("channels", "chlist", channels)
    attr("compression", "compression", struct.pack("<B", 0))
    attr("dataWindow", "box2i",
         struct.pack("<iiii", 0, 0, width - 1, height - 1))
    attr("displayWindow", "box2i",
         struct.pack("<iiii", 0, 0, width - 1, height - 1))
    attr("lineOrder", "lineOrder", struct.pack("<B", 0))
    attr("pixelAspectRatio", "float", struct.pack("<f", 1.0))
    attr("screenWindowCenter", "v2f", struct.pack("<ff", 0.0, 0.0))
    attr("screenWindowWidth", "float", struct.pack("<f", 1.0))
    out += b"\0"

    # offset table placeholder (uint64 per scanline)
    offset_table_pos = len(out)
    out += b"\0" * (8 * height)

    scanline_offsets = []
    for y in range(height):  # line order 0: top (y=height-1) first
        scanline_offsets.append(len(out))
        out += struct.pack("<ii", height - 1 - y, width * 4 * 4)
        for px in rgba_float_rows[y]:
            out += struct.pack("<ffff", px[2], px[1], px[0], px[3])

    for i, off in enumerate(scanline_offsets):
        struct.pack_into("<Q", out, offset_table_pos + 8 * i, off)

    with open(path, "wb") as f:
        f.write(out)
