"""Blender shader-node graph -> MoonRay material compilation.

Supported surface shaders are converted to MoonRay Dwa materials; simple
color/scalar subgraphs are statically evaluated (baked) when their inputs are
constants. Image textures become ImageMap binds, normal maps become
ImageNormalMap binds.

Unsupported node graphs fall back to the material's base color with a
warning, so export never fails.
"""

import math

import bpy

try:
    from .exporter import (
        fmt_rgb,
        fmt_string,
        sanitize_name,
    )
except ImportError:
    from exporter import (  # standalone (non-package) usage in tests
        fmt_rgb,
        fmt_string,
        sanitize_name,
    )


# ---------------------------------------------------------------------------
# Static value evaluation

_RGB = "rgb"
_FLOAT = "float"
_IMG = "img"          # ("img", image, colorspace)
_NORMAL = "normal"    # ("normal", image, strength)
_MAP = "map"          # ("map", rdl2_class, {attr: expr})


def _const_rgb(c):
    return (_RGB, tuple(float(x) for x in c[:3]))


def _const_float(v):
    return (_FLOAT, float(v))


def _linked_value(sock):
    if sock is None or not sock.is_linked:
        return None
    return sock.links[0].from_node, sock.links[0].from_socket


def _texture_image_value(node):
    """ShaderNodeTexImage -> ("img", image, mapping) when usable."""
    img = getattr(node, "image", None)
    if img is None or not img.filepath:
        return None
    mapping = None
    vec_in = node.inputs.get("Vector")
    if vec_in is not None and vec_in.is_linked:
        src = vec_in.links[0].from_node
        if src.type == "MAPPING":
            mapping = _eval_mapping_node(src)
    return (_IMG, img, mapping)


def _eval_mapping_node(node):
    """ShaderNodeMapping -> dict of MoonRay ImageMap transform attributes.

    The exporter flips V of the geometry UVs (Blender bottom-left origin ->
    MoonRay top-left origin), so the mapping's Y components are mirrored.
    Rotation is only approximate (the V flip is a mirror that MoonRay's
    UV transform cannot represent together with a rotation).
    """
    try:
        loc = node.inputs["Location"].default_value
        rot = node.inputs["Rotation"].default_value
        scl = node.inputs["Scale"].default_value
    except Exception:
        return None
    mapping = {
        "offset": (loc[0], 1.0 - loc[1]),
        "scale": (scl[0], scl[1]),
    }
    if abs(rot[2]) > 1e-6:
        mapping["rotation_angle"] = -math.degrees(rot[2])
        mapping["rotation_center"] = (0.0, 1.0)
    return mapping


def _mapping_lines(mapping):
    """RDLA attribute lines for an ImageMap/ImageNormalMap transform."""
    lines = ['["offset"] = Vec2(%s, %s),' % (
        "%.9g" % mapping["offset"][0], "%.9g" % mapping["offset"][1]),
        '["scale"] = Vec2(%s, %s),' % (
            "%.9g" % mapping["scale"][0], "%.9g" % mapping["scale"][1])]
    if "rotation_angle" in mapping:
        lines.append('["rotation_angle"] = %.9g,' % mapping["rotation_angle"])
        lines.append('["rotation_center"] = Vec2(%s, %s),' % (
            "%.9g" % mapping["rotation_center"][0],
            "%.9g" % mapping["rotation_center"][1]))
    return lines


# math ops shared by ShaderNodeMath
_MATH_OPS = {
    "ADD": lambda a, b: a + b,
    "SUBTRACT": lambda a, b: a - b,
    "MULTIPLY": lambda a, b: a * b,
    "DIVIDE": lambda a, b: a / b if b != 0 else 0.0,
    "POWER": lambda a, b: math.pow(abs(a), b) if a >= 0 else 0.0,
    "LOGARITHM": lambda a, b: math.log(max(a, 1e-30)) / math.log(max(b, 1e-30)),
    "SQRT": lambda a, b: math.sqrt(max(a, 0.0)),
    "INV_SQRT": lambda a, b: 1.0 / math.sqrt(max(a, 1e-30)),
    "ABSOLUTE": lambda a, b: abs(a),
    "EXPONENT": lambda a, b: math.exp(a),
    "MINIMUM": lambda a, b: min(a, b),
    "MAXIMUM": lambda a, b: max(a, b),
    "LESS_THAN": lambda a, b: 1.0 if a < b else 0.0,
    "GREATER_THAN": lambda a, b: 1.0 if a > b else 0.0,
    "MODULO": lambda a, b: a % b if b != 0 else 0.0,
    "FLOOR": lambda a, b: math.floor(a),
    "CEIL": lambda a, b: math.ceil(a),
    "SINE": lambda a, b: math.sin(a),
    "COSINE": lambda a, b: math.cos(a),
    "TANGENT": lambda a, b: math.tan(a),
    "ARCSINE": lambda a, b: math.asin(max(-1.0, min(1.0, a))),
    "ARCCOSINE": lambda a, b: math.acos(max(-1.0, min(1.0, a))),
    "ARCTANGENT": lambda a, b: math.atan(a),
    "ROUND": lambda a, b: round(a),
    "TRUNC": lambda a, b: math.trunc(a),
    "SIGN": lambda a, b: 1.0 if a > 0 else (-1.0 if a < 0 else 0.0),
    "COMPARE": lambda a, b: 1.0 if abs(a - b) < 0.5 else 0.0,
}


class NodeEvaluator:
    """Best-effort static evaluation of Blender shader node values."""

    def __init__(self):
        self._cache = {}

    def eval_socket(self, sock):
        """Evaluate a socket to a constant value, image ref, or None."""
        if sock is None:
            return None
        link = _linked_value(sock)
        if link is None:
            # unconnected: use the socket's own default
            try:
                if sock.type == "RGBA":
                    return _const_rgb(sock.default_value)
                if sock.type == "VALUE":
                    return _const_float(sock.default_value)
                if sock.type == "BOOLEAN":
                    return _const_float(1.0 if sock.default_value else 0.0)
            except Exception:
                pass
            return None
        node, from_sock = link
        value = self.eval_node(node)
        if value is None:
            return None
        if value[0] in (_RGB, _FLOAT, _IMG, _MAP):
            return value
        return None

    def eval_node(self, node):
        if node is None:
            return None
        key = id(node)
        if key in self._cache:
            return self._cache[key]

        value = None
        ntype = getattr(node, "type", "")
        try:
            if ntype == "RGB":
                value = _const_rgb(node.outputs["Color"].default_value)
            elif ntype == "VALUE":
                value = _const_float(node.outputs["Value"].default_value)
            elif ntype == "TEX_IMAGE":
                value = _texture_image_value(node)
            elif ntype == "TEX_NOISE":
                value = self._eval_noise(node)
            elif ntype == "MATH":
                value = self._eval_math(node)
            elif ntype == "MIX":
                value = self._eval_mix_rgb(node)
            elif ntype == "INVERT":
                c = self.eval_socket(node.inputs["Color"])
                if c and c[0] == _RGB:
                    value = _const_rgb(tuple(1.0 - x for x in c[1]))
            elif ntype == "BRIGHTCONTRAST":
                value = self._eval_brightcontrast(node)
            elif ntype == "GAMMA":
                c = self.eval_socket(node.inputs["Color"])
                g = self.eval_socket(node.inputs["Gamma"])
                if c and c[0] == _RGB and g and g[0] == _FLOAT:
                    value = _const_rgb(tuple(
                        math.pow(max(x, 0.0), 1.0 / max(g[1], 1e-6))
                        for x in c[1]))
            elif ntype == "HUE_SAT":
                value = self._eval_hue_sat(node)
            elif ntype == "RGBTOBW":
                c = self.eval_socket(node.inputs["Color"])
                if c and c[0] == _RGB:
                    lum = (0.2126 * c[1][0] + 0.7152 * c[1][1]
                           + 0.0722 * c[1][2])
                    value = _const_float(lum)
            elif ntype == "BLACKBODY":
                value = self._eval_blackbody(node)
            elif ntype == "VALTORGB":
                value = self._eval_colorramp(node)
            elif ntype == "CLAMP":
                v = self.eval_socket(node.inputs["Value"])
                mn = self.eval_socket(node.inputs["Min"])
                mx = self.eval_socket(node.inputs["Max"])
                if v and v[0] == _FLOAT:
                    lo = mn[1] if mn and mn[0] == _FLOAT else 0.0
                    hi = mx[1] if mx and mx[0] == _FLOAT else 1.0
                    value = _const_float(max(lo, min(hi, v[1])))
            elif ntype == "MAP_RANGE":
                value = self._eval_map_range(node)
        except Exception:
            value = None
        self._cache[key] = value
        return value

    def _f(self, sock):
        v = self.eval_socket(sock)
        if v and v[0] == _FLOAT:
            return v[1]
        return None

    def _eval_blackbody(self, node):
        """ShaderNodeBlackbody -> linear-ish RGB for the given temperature.

        Uses the same Planckian-locus approximation Blender/Cycles apply for
        the blackbody node, so a 6500K emitter comes out near-white.
        """
        t = self._f(node.inputs.get("Temperature"))
        if t is None or t <= 0.0:
            return None
        # Cycles blackbody: RGB in a wide-ish gamut, normalized to preserve
        # relative intensity; scaled here to roughly match the node output.
        # From Blender's implementation (Mitchell 1999 recursive 2020).
        t_k = t
        # piecewise approximation on the 2020 locus (simplified Kelvin->xyY)
        def _x(kk):
            if kk < 4000:
                return (-0.2661239e9 / kk**3
                        - 0.2343580e6 / kk**2
                        + 0.8776956e3 / kk
                        + 0.179910)
            return (-3.0258469e9 / kk**3
                    + 2.1070379e6 / kk**2
                    + 0.2226347e3 / kk
                    + 0.240390)

        def _y_from_x(x, kk):
            if kk < 2222:
                return (-1.1063814 * x**3 - 1.34811020 * x**2
                        + 2.18555832 * x - 0.20219683)
            if kk < 4000:
                return (-0.9549476 * x**3 - 1.37418593 * x**2
                        + 2.09137015 * x - 0.16748867)
            return (3.0817580 * x**3 - 5.87338670 * x**2
                    + 3.75112997 * x - 0.37001483)

        x = _x(t_k)
        y = _y_from_x(x, t_k)
        if y <= 0.0 or x <= 0.0:
            return _const_rgb((1.0, 1.0, 1.0))
        # convert xyY (Y=1) to XYZ
        z = 1.0 - x - y
        X = x / y
        Z = z / y
        # XYZ -> linear sRGB (D65)
        r = 3.2406 * X - 1.5372 * 1.0 - 0.4986 * Z
        g = -0.9689 * X + 1.8758 * 1.0 + 0.0415 * Z
        b = 0.0557 * X - 0.2040 * 1.0 + 1.0570 * Z
        # normalize to keep max channel ~ 1.0 (Cycles blackbody does not
        # clamp, but the emitter strength scales it anyway)
        mx = max(r, g, b)
        if mx <= 0.0:
            return _const_rgb((1.0, 1.0, 1.0))
        return _const_rgb((
            max(0.0, r / mx), max(0.0, g / mx), max(0.0, b / mx)))

    def _eval_noise(self, node):
        """ShaderNodeTexNoise -> NoiseMap_v2 (grayscale, color mode)."""
        scale = self._f(node.inputs.get("Scale")) or 1.0
        detail = self._f(node.inputs.get("Detail")) or 1.0
        distortion = self._f(node.inputs.get("Distortion")) or 0.0
        seed = int(self._f(node.inputs.get("W")) or 0.0)
        return (_MAP, "NoiseMap_v2", {
            "color": "true",
            "color_A": fmt_rgb((0.0, 0.0, 0.0)),
            "color_B": fmt_rgb((1.0, 1.0, 1.0)),
            "frequency_multiplier": "%.9g" % max(0.001, scale),
            "max_level": "%.9g" % max(1.0, detail),
            "distortion": "%.9g" % max(0.0, distortion),
            "seed": str(seed),
        })

    def _eval_math(self, node):
        op = node.operation
        fn = _MATH_OPS.get(op)
        if fn is None:
            return None
        a = self._f(node.inputs[0])
        if a is None:
            return None
        b = self._f(node.inputs[1]) if len(node.inputs) > 1 else 0.0
        if b is None:
            return None
        if node.use_clamp:
            a = max(0.0, min(1.0, a))
            if len(node.inputs) > 1:
                b = max(0.0, min(1.0, b))
        return _const_float(fn(a, b))

    def _eval_mix_rgb(self, node):
        # Blender >= 3.4 Mix node has typed sockets sharing names (A/B can be
        # VALUE, VECTOR or RGBA); select by (name, type). Older Blender used
        # Color1/Color2 + Fac.
        def _sock(name, types):
            for s in node.inputs:
                if s.name == name and s.type in types:
                    return s
            return None

        a_in = _sock("A", ("RGBA",)) or node.inputs.get("Color1")
        b_in = _sock("B", ("RGBA",)) or node.inputs.get("Color2")
        f_sock = (_sock("Factor", ("VALUE",))
                  or _sock("Fac", ("VALUE",))
                  or node.inputs.get("Fac"))
        a = self.eval_socket(a_in)
        b = self.eval_socket(b_in)
        f = self._f(f_sock)
        if a is None or b is None or f is None:
            return None
        if a[0] != _RGB or b[0] != _RGB:
            return None
        f = max(0.0, min(1.0, f))
        if node.blend_type == "MIX":
            return _const_rgb(tuple(a[1][i] * (1 - f) + b[1][i] * f
                                    for i in range(3)))
        if node.blend_type == "ADD":
            return _const_rgb(tuple(min(1.0, a[1][i] + b[1][i] * f)
                                    for i in range(3)))
        if node.blend_type == "MULTIPLY":
            return _const_rgb(tuple(a[1][i] * (1 - f)
                                    + a[1][i] * b[1][i] * f
                                    for i in range(3)))
        return None

    def _eval_brightcontrast(self, node):
        c = self.eval_socket(node.inputs["Color"])
        b = self._f(node.inputs["Bright"])
        k = self._f(node.inputs["Contrast"])
        if c is None or c[0] != _RGB or b is None or k is None:
            return None
        return _const_rgb(tuple(max(0.0, x * k + b) for x in c[1]))

    def _eval_hue_sat(self, node):
        c = self.eval_socket(node.inputs["Color"])
        h = self._f(node.inputs["Hue"])
        s = self._f(node.inputs["Saturation"])
        v = self._f(node.inputs["Value"])
        if c is None or c[0] != _RGB or None in (h, s, v):
            return None
        r, g, b = c[1]
        mx = max(r, g, b)
        mn = min(r, g, b)
        l = (mx + mn) / 2.0
        d = mx - mn
        if d == 0:
            hue = 0.0
        elif mx == r:
            hue = ((g - b) / d) % 6.0
        elif mx == g:
            hue = (b - r) / d + 2.0
        else:
            hue = (r - g) / d + 4.0
        hue = (hue / 6.0 + h) % 1.0
        sat = d / (1.0 - abs(2.0 * l - 1.0)) if (1.0 - abs(2.0 * l - 1.0)) > 1e-6 else 0.0
        sat = max(0.0, min(1.0, sat * s))
        val = l * v
        # hue/sat/val -> rgb
        if sat == 0:
            out = (val, val, val)
        else:
            q = val * (1 - sat) if val < 0.5 else val + sat - val * sat
            p = 2 * val - q

            def hue2rgb(t):
                t = t % 1.0
                if t < 1 / 6:
                    return p + (q - p) * 6 * t
                if t < 1 / 2:
                    return q
                if t < 2 / 3:
                    return p + (q - p) * (2 / 3 - t) * 6
                return p
            out = (hue2rgb(hue + 1 / 3), hue2rgb(hue), hue2rgb(hue - 1 / 3))
        return _const_rgb(out)

    def _eval_colorramp(self, node):
        f = self._f(node.inputs["Fac"])
        if f is None:
            return None
        ramp = node.color_ramp
        if not ramp.elements:
            return None
        elems = sorted(ramp.elements, key=lambda e: e.position)
        if f <= elems[0].position:
            return _const_rgb(elems[0].color)
        for e0, e1 in zip(elems, elems[1:]):
            if e0.position <= f <= e1.position:
                span = e1.position - e0.position
                t = 0.0 if span == 0 else (f - e0.position) / span
                return _const_rgb(tuple(
                    e0.color[i] * (1 - t) + e1.color[i] * t
                    for i in range(3)))
        return _const_rgb(elems[-1].color)

    def _eval_map_range(self, node):
        v = self._f(node.inputs["Value"])
        if v is None:
            return None
        mn = self._f(node.inputs["From Min"])
        mx = self._f(node.inputs["From Max"])
        tmn = self._f(node.inputs["To Min"])
        tmx = self._f(node.inputs["To Max"])
        if None in (mn, mx, tmn, tmx) or mx == mn:
            return None
        t = (v - mn) / (mx - mn)
        if node.clamp:
            t = max(0.0, min(1.0, t))
        return _const_float(tmn + t * (tmx - tmn))


# ---------------------------------------------------------------------------
# Surface shader -> Dwa material parameters

class MaterialCompiler:
    """Compiles a Blender material into MoonRay RDLA blocks."""

    def __init__(self, exporter):
        self.exporter = exporter
        self.evaluator = NodeEvaluator()
        self._mat_index = exporter.mat_count  # reuse counter via exporter
        self.emit_emission = True
        self.emission = None

    # -- utilities ---------------------------------------------------------
    def _unique(self, base):
        self.exporter.mat_count += 1
        return "%s_%d" % (base, self.exporter.mat_count)

    def _emit_image_map(self, img, mapping=None):
        name = self._unique("tex_" + sanitize_name(img.name, "tex"))
        self.exporter.block('ImageMap("%s")' % name)
        self.exporter.out('["texture"] = %s,'
                          % fmt_string(bpy.path.abspath(img.filepath)))
        if mapping:
            for expr in _mapping_lines(mapping):
                self.exporter.out("    " + expr)
        self.exporter.end_block()
        return name

    def _resolve_rgb(self, value, fallback=(1.0, 1.0, 1.0)):
        """value -> (expr, needs_bind) where expr is an RDLA expression."""
        if value is None:
            return fmt_rgb(fallback), False
        kind = value[0]
        if kind == _RGB:
            return fmt_rgb(value[1]), False
        if kind == _IMG:
            name = self._emit_image_map(value[1], value[2])
            return 'bind(ImageMap("%s"))' % name, True
        if kind == _MAP:
            cls, attrs = value[1], value[2]
            name = self._unique("procmap")
            self.exporter.block('%s("%s")' % (cls, name))
            for attr, expr in attrs.items():
                self.exporter.out('["%s"] = %s,' % (attr, expr))
            self.exporter.end_block()
            return 'bind(%s("%s"))' % (cls, name), True
        return fmt_rgb(fallback), False

    def _resolve_float(self, value, fallback=0.0):
        if value is None:
            return fallback
        if value[0] == _FLOAT:
            return value[1]
        return fallback

    # -- shader node -> material params ------------------------------------
    def _principled_params(self, node):
        ev = self.evaluator
        params = {
            "albedo": (None, (1.0, 1.0, 1.0)),
            "roughness": 0.5,
            "diffuse_roughness": 0.0,
            "metallic": 0.0,
            "specular": 0.5,
            "refractive_index": 1.5,
            "emission": None,
            "emission_strength": 0.0,
            "alpha": 1.0,
            "thin_geometry": False,
            "transmission": 0.0,
            "transmission_color": (1.0, 1.0, 1.0),
            "anisotropy": 0.0,
            "anisotropy_rotation": 0.0,
            "clearcoat": 0.0,
            "clearcoat_roughness": 0.03,
            "clearcoat_ior": 1.5,
            "clearcoat_color": (1.0, 1.0, 1.0),
            "fuzz": 0.0,
            "fuzz_roughness": 0.5,
            "fuzz_albedo": (1.0, 1.0, 1.0),
            "subsurface": 0,
            "scattering_color": (1.0, 1.0, 1.0),
            "scattering_radius": 0.0,
            "normal": None,      # ("normal", image, strength)
            "input_normal_dial": 0.0,
        }
        base = ev.eval_socket(node.inputs["Base Color"])
        params["albedo"] = (base, (1.0, 1.0, 1.0))
        base_rgb = base[1] if (base and base[0] == _RGB) else (1.0, 1.0, 1.0)

        params["roughness"] = self._resolve_float(
            ev.eval_socket(node.inputs["Roughness"]), 0.5)
        params["metallic"] = self._resolve_float(
            ev.eval_socket(node.inputs["Metallic"]), 0.0)
        params["refractive_index"] = self._resolve_float(
            ev.eval_socket(node.inputs["IOR"]), 1.5)
        params["alpha"] = self._resolve_float(
            ev.eval_socket(node.inputs["Alpha"]), 1.0)
        params["thin_geometry"] = self._resolve_float(
            ev.eval_socket(node.inputs.get("Thin Wall")), 0.0) > 0.5
        params["diffuse_roughness"] = self._resolve_float(
            ev.eval_socket(node.inputs.get("Diffuse Roughness")), 0.0)

        spec = ev.eval_socket(node.inputs.get("Specular IOR Level"))
        if spec is None:
            spec = ev.eval_socket(node.inputs.get("Specular"))
        params["specular"] = self._resolve_float(spec, 0.5)

        trans = ev.eval_socket(node.inputs.get("Transmission Weight"))
        params["transmission"] = self._resolve_float(trans, 0.0)
        # Blender >= 4.0 tints transmission through Base Color
        params["transmission_color"] = base_rgb

        params["anisotropy"] = self._resolve_float(
            ev.eval_socket(node.inputs.get("Anisotropic")), 0.0)
        params["anisotropy_rotation"] = self._resolve_float(
            ev.eval_socket(node.inputs.get("Anisotropic Rotation")), 0.0)

        # clearcoat (Coat) -> Dwa clearcoat lobe
        coat = self._resolve_float(
            ev.eval_socket(node.inputs.get("Coat Weight")), 0.0)
        params["clearcoat"] = coat
        params["clearcoat_roughness"] = self._resolve_float(
            ev.eval_socket(node.inputs.get("Coat Roughness")), 0.03)
        params["clearcoat_ior"] = self._resolve_float(
            ev.eval_socket(node.inputs.get("Coat IOR")), 1.5)
        coat_tint = ev.eval_socket(node.inputs.get("Coat Tint"))
        if coat_tint and coat_tint[0] == _RGB:
            params["clearcoat_color"] = coat_tint[1]

        # sheen -> Dwa fuzz lobe
        sheen = self._resolve_float(
            ev.eval_socket(node.inputs.get("Sheen Weight")), 0.0)
        params["fuzz"] = sheen
        params["fuzz_roughness"] = self._resolve_float(
            ev.eval_socket(node.inputs.get("Sheen Roughness")), 0.5)
        sheen_tint = ev.eval_socket(node.inputs.get("Sheen Tint"))
        if sheen_tint and sheen_tint[0] == _RGB:
            params["fuzz_albedo"] = sheen_tint[1]

        # subsurface -> Dwa bssrdf lobe
        sss_weight = self._resolve_float(
            ev.eval_socket(node.inputs.get("Subsurface Weight")), 0.0)
        if sss_weight > 0.0:
            method = getattr(node, "subsurface_method", "RANDOM_WALK")
            params["subsurface"] = {"BURLEY": 1, "RANDOM_WALK_SKIN": 2,
                                    "RANDOM_WALK": 2}.get(method, 2)
            radius = ev.eval_socket(node.inputs.get("Subsurface Radius"))
            if radius and radius[0] == _RGB:
                params["scattering_color"] = radius[1]
            params["scattering_radius"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Subsurface Scale")), 0.05)

        em_c = ev.eval_socket(node.inputs["Emission Color"])
        em_s = ev.eval_socket(node.inputs["Emission Strength"])
        params["emission"] = em_c if em_c and em_c[0] in (_RGB, _IMG, _MAP) else None
        params["emission_strength"] = self._resolve_float(em_s, 0.0)

        # normal input
        normal_in = node.inputs.get("Normal")
        if normal_in is not None and normal_in.is_linked:
            src = normal_in.links[0].from_node
            if src.type == "NORMAL_MAP":
                img_val = ev.eval_socket(src.inputs["Color"])
                strength = self._resolve_float(
                    ev.eval_socket(src.inputs.get("Strength")), 1.0)
                if img_val and img_val[0] == _IMG:
                    params["normal"] = ("normal", img_val[1], strength,
                                        img_val[2])
            elif src.type == "BUMP":
                strength = self._resolve_float(
                    ev.eval_socket(src.inputs.get("Strength")), 1.0)
                params["input_normal_dial"] = strength
                self.exporter.report(
                    "WARNING: Bump node approximated via normal strength")
        return params

    def _simple_params(self, node):
        """Diffuse/Glossy/Glass/Transparent/Emission/Anisotropic/Refraction/
        Translucent/Velvet/Toon/Subsurface shaders."""
        ev = self.evaluator
        ntype = node.type
        params = {
            "albedo": (None, (1.0, 1.0, 1.0)),
            "roughness": 0.5,
            "metallic": 0.0,
            "specular": 1.0,
            "refractive_index": 1.5,
            "emission": None,
            "emission_strength": 0.0,
            "alpha": 1.0,
            "transmission": 0.0,
            "transmission_color": (1.0, 1.0, 1.0),
            "anisotropy": 0.0,
            "anisotropy_rotation": 0.0,
            "normal": None,
            "input_normal_dial": 0.0,
        }
        if ntype == "BSDF_DIFFUSE":
            params["albedo"] = (ev.eval_socket(node.inputs["Color"]),
                                (1.0, 1.0, 1.0))
            params["roughness"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Roughness")), 1.0)
            params["specular"] = 0.0
        elif ntype == "BSDF_GLOSSY":
            params["albedo"] = (ev.eval_socket(node.inputs["Color"]),
                                (1.0, 1.0, 1.0))
            params["roughness"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Roughness")), 0.1)
            params["specular"] = 1.0
            # Blender >= 5.x merges the Anisotropic BSDF into Glossy
            params["anisotropy"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Anisotropy")), 0.0)
            params["anisotropy_rotation"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Rotation")), 0.0)
        elif ntype == "BSDF_GLASS":
            params["albedo"] = (ev.eval_socket(node.inputs["Color"]),
                                (1.0, 1.0, 1.0))
            params["transmission"] = 1.0
            params["transmission_color"] = (
                ev.eval_socket(node.inputs["Color"])[1]
                if ev.eval_socket(node.inputs["Color"])
                and ev.eval_socket(node.inputs["Color"])[0] == _RGB
                else (1.0, 1.0, 1.0))
            params["roughness"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Roughness")), 0.0)
            params["refractive_index"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("IOR")), 1.5)
        elif ntype == "BSDF_TRANSPARENT":
            params["alpha"] = 0.0
        elif ntype == "BSDF_REFRACTION":
            color = ev.eval_socket(node.inputs["Color"])
            params["albedo"] = (color, (1.0, 1.0, 1.0))
            params["transmission"] = 1.0
            if color and color[0] == _RGB:
                params["transmission_color"] = color[1]
            params["roughness"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Roughness")), 0.0)
            params["refractive_index"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("IOR")), 1.45)
        elif ntype == "BSDF_TRANSLUCENT":
            params["albedo"] = (ev.eval_socket(node.inputs["Color"]),
                                (1.0, 1.0, 1.0))
            params["transmission"] = 1.0
            params["specular"] = 0.0
        elif ntype == "BSDF_ANISOTROPIC":
            params["albedo"] = (ev.eval_socket(node.inputs["Color"]),
                                (1.0, 1.0, 1.0))
            params["roughness"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Roughness")), 0.2)
            params["specular"] = 1.0
            params["anisotropy"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Anisotropy")), 0.0)
            params["anisotropy_rotation"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Rotation")), 0.0)
        elif ntype == "BSDF_VELVET":
            params["albedo"] = (ev.eval_socket(node.inputs["Color"]),
                                (1.0, 1.0, 1.0))
            params["specular"] = 0.0
            params["fuzz"] = 1.0
            params["fuzz_roughness"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Sigma")), 1.0)
            params["fuzz_albedo"] = (1.0, 1.0, 1.0)
        elif ntype == "BSDF_TOON":
            params["albedo"] = (ev.eval_socket(node.inputs["Color"]),
                                (1.0, 1.0, 1.0))
            params["roughness"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Size")), 1.0)
            params["specular"] = 0.0
        elif ntype == "SUBSURFACE_SCATTERING":
            color = ev.eval_socket(node.inputs["Color"])
            params["albedo"] = (color, (1.0, 1.0, 1.0))
            params["specular"] = 0.0
            params["subsurface"] = 2
            if color and color[0] == _RGB:
                params["scattering_color"] = color[1]
            params["scattering_radius"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Scale")), 1.0)
        elif ntype == "EMISSION":
            params["emission"] = ev.eval_socket(node.inputs["Color"])
            params["emission_strength"] = self._resolve_float(
                ev.eval_socket(node.inputs.get("Strength")), 1.0)
            params["albedo"] = (None, (0.0, 0.0, 0.0))
        return params

    def _emit_dwa(self, name, params, cls="DwaBaseMaterial", extra_lines=None):
        out = self.exporter.out
        out('%s = %s("%s") {' % (name, cls, name))
        expr, _bind = self._resolve_rgb(params["albedo"][0],
                                        params["albedo"][1])
        out('    ["albedo"] = %s,' % expr)
        out('    ["roughness"] = %.9g,' % max(1e-4, params["roughness"]))
        out('    ["metallic"] = %.9g,' % params["metallic"])
        out('    ["specular"] = %.9g,' % params["specular"])
        out('    ["refractive_index"] = %.9g,' % params["refractive_index"])
        if params.get("diffuse_roughness", 0.0) > 0.0:
            out('    ["diffuse_roughness"] = %.9g,'
                % params["diffuse_roughness"])
        if params.get("thin_geometry", False):
            out('    ["thin_geometry"] = true,')
        if params["transmission"] > 0.0:
            out('    ["show_transmission"] = true,')
            out('    ["transmission"] = %.9g,' % params["transmission"])
            tc = params["transmission_color"]
            out('    ["transmission_color"] = %s,' % fmt_rgb(tc))
        if params.get("anisotropy", 0.0) > 0.0:
            out('    ["anisotropy"] = %.9g,' % params["anisotropy"])
            rot = params.get("anisotropy_rotation", 0.0)
            tx, ty = math.cos(rot), math.sin(rot)
            out('    ["shading_tangent"] = Vec2(%.9g, %.9g),' % (tx, ty))
        if params.get("clearcoat", 0.0) > 0.0:
            out('    ["show_clearcoat"] = true,')
            out('    ["clearcoat"] = %.9g,' % params["clearcoat"])
            out('    ["clearcoat_roughness"] = %.9g,'
                % params["clearcoat_roughness"])
            out('    ["clearcoat_refractive_index"] = %.9g,'
                % params["clearcoat_ior"])
            cc = params.get("clearcoat_color", (1.0, 1.0, 1.0))
            out('    ["clearcoat_attenuation_color"] = %s,' % fmt_rgb(cc))
        if params.get("fuzz", 0.0) > 0.0:
            out('    ["show_fuzz"] = true,')
            out('    ["fuzz"] = %.9g,' % params["fuzz"])
            out('    ["fuzz_roughness"] = %.9g,' % params["fuzz_roughness"])
            fa = params.get("fuzz_albedo", (1.0, 1.0, 1.0))
            out('    ["fuzz_albedo"] = %s,' % fmt_rgb(fa))
        if params.get("subsurface", 0) > 0:
            out('    ["bssrdf"] = %d,' % params["subsurface"])
            sc = params.get("scattering_color", (1.0, 1.0, 1.0))
            out('    ["scattering_color"] = %s,' % fmt_rgb(sc))
            out('    ["scattering_radius"] = %.9g,'
                % max(1e-6, params.get("scattering_radius", 0.05)))
        if params["alpha"] < 0.999:
            out('    ["presence"] = %.9g,' % max(0.0, params["alpha"]))
        if params["emission"] is not None and params["emission_strength"] > 0:
            em = params["emission"]
            strength = params["emission_strength"]
            if em[0] == _RGB:
                expr = fmt_rgb(tuple(
                    min(1e9, c * strength) for c in em[1]))
                # record (base_color, strength) so the exporter can also
                # emit a MeshLight for this emissive geometry
                self.emission = (em[1], strength)
            else:
                expr, _b = self._resolve_rgb(em, (0, 0, 0))
            if self.emit_emission:
                out('    ["emission"] = %s,' % expr)
                out('    ["show_emission"] = true,')
        if params["normal"] is not None:
            nrm = params["normal"]
            kind, img, strength = nrm[0], nrm[1], nrm[2]
            mapping = nrm[3] if len(nrm) > 3 else None
            if kind == "normal" and img is not None:
                nm_name = self._unique(
                    "normal_" + sanitize_name(img.name, "nm"))
                out('    ["input_normal"] = bind(ImageNormalMap("%s")),'
                    % nm_name)
                out('    ["input_normal_dial"] = %.9g,'
                    % max(0.0, strength))
                # emit the ImageNormalMap block AFTER the material block
                self._pending_normal_maps.append((nm_name, img, mapping))
            elif params["input_normal_dial"] > 0.0:
                out('    ["input_normal_dial"] = %.9g,'
                    % params["input_normal_dial"])
        elif params["input_normal_dial"] > 0.0:
            out('    ["input_normal_dial"] = %.9g,'
                % params["input_normal_dial"])
        if extra_lines:
            for line in extra_lines:
                out("    " + line)
        self.exporter.end_block()

    def _emit_normal_map_block(self, nm_name, img, mapping=None):
        self.exporter.block('ImageNormalMap("%s")' % nm_name)
        self.exporter.out('["tangent_space_normal_texture"] = %s,'
                          % fmt_string(bpy.path.abspath(img.filepath)))
        if mapping:
            for expr in _mapping_lines(mapping):
                self.exporter.out("    " + expr)
        self.exporter.end_block()

    # -- entry points ------------------------------------------------------
    def compile_material(self, material, name, emit_emission=True):
        """Write RDLA blocks for the material; return the material ref name
        used in the Layer entry."""
        self._pending_normal_maps = []
        self.emission = None
        self.emit_emission = emit_emission
        ev = self.evaluator

        if material is None or not material.use_nodes:
            self._emit_dwa(name, self._principled_defaults())
            self._flush_normal_maps()
            return name

        tree = material.node_tree
        output = next((n for n in tree.nodes
                       if n.type == "OUTPUT_MATERIAL"), None)
        surface = None
        if output is not None and output.inputs["Surface"].is_linked:
            surface = output.inputs["Surface"].links[0].from_node

        if surface is None:
            self._emit_dwa(name, self._principled_defaults())
            self._flush_normal_maps()
            return name

        if surface.type == "BSDF_PRINCIPLED":
            self._emit_dwa(name, self._principled_params(surface))
            self._flush_normal_maps()
            return name

        if surface.type in ("BSDF_DIFFUSE", "BSDF_GLOSSY", "BSDF_GLASS",
                            "BSDF_TRANSPARENT", "BSDF_REFRACTION",
                            "BSDF_TRANSLUCENT", "BSDF_ANISOTROPIC",
                            "BSDF_VELVET", "BSDF_TOON",
                            "SUBSURFACE_SCATTERING", "EMISSION"):
            self._emit_dwa(name, self._simple_params(surface))
            self._flush_normal_maps()
            return name

        if surface.type == "MIX_SHADER":
            a_node = surface.inputs[1].links[0].from_node \
                if surface.inputs[1].is_linked else None
            b_node = surface.inputs[2].links[0].from_node \
                if surface.inputs[2].is_linked else None
            fac = self._resolve_float(ev.eval_socket(surface.inputs["Fac"]),
                                      0.5)
            self._compile_mix(a_node, b_node, fac, name)
            self._flush_normal_maps()
            return name

        if surface.type == "ADD_SHADER":
            a_node = surface.inputs[0].links[0].from_node \
                if surface.inputs[0].is_linked else None
            b_node = surface.inputs[1].links[0].from_node \
                if surface.inputs[1].is_linked else None
            self._compile_mix(a_node, b_node, 0.5, name)
            self._flush_normal_maps()
            return name

        self.exporter.report(
            "WARNING: unsupported surface shader %s - using default material"
            % surface.type)
        self._emit_dwa(name, self._principled_defaults())
        self._flush_normal_maps()
        return name

    def _compile_mix(self, a_node, b_node, fac, name):
        """MIX_SHADER / ADD_SHADER via DwaMixMaterial."""
        pa = self._params_for(a_node)
        pb = self._params_for(b_node)
        if fac <= 0.01:
            self._emit_dwa(name, pb)
            return
        if fac >= 0.99:
            self._emit_dwa(name, pa)
            return
        b_name = name + "_B"
        self._emit_dwa(b_name, pb)
        self._emit_dwa(
            name, pa, cls="DwaMixMaterial",
            extra_lines=[
                '["material"] = %s,' % b_name,
                '["mix"] = %.9g,' % fac,
            ])

    def _flush_normal_maps(self):
        for nm_name, img, mapping in getattr(self, "_pending_normal_maps",
                                             []):
            self._emit_normal_map_block(nm_name, img, mapping)
        self._pending_normal_maps = []

    def _params_for(self, node):
        if node is None:
            return self._principled_defaults()
        if node.type == "BSDF_PRINCIPLED":
            return self._principled_params(node)
        if node.type in ("BSDF_DIFFUSE", "BSDF_GLOSSY", "BSDF_GLASS",
                         "BSDF_TRANSPARENT", "BSDF_REFRACTION",
                         "BSDF_TRANSLUCENT", "BSDF_ANISOTROPIC",
                         "BSDF_VELVET", "BSDF_TOON",
                         "SUBSURFACE_SCATTERING", "EMISSION"):
            return self._simple_params(node)
        return self._principled_defaults()

    def _principled_defaults(self):
        return {
            "albedo": (None, (1.0, 1.0, 1.0)),
            "roughness": 0.5,
            "metallic": 0.0,
            "specular": 0.5,
            "refractive_index": 1.5,
            "emission": None,
            "emission_strength": 0.0,
            "alpha": 1.0,
            "transmission": 0.0,
            "transmission_color": (1.0, 1.0, 1.0),
            "normal": None,
            "input_normal_dial": 0.0,
        }
