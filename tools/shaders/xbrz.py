#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 OpenLF2 contributors

"""Generator for OpenLF2's xBRZ-style upscaling fragment shader.

The algorithm is written once, as a graph of scalar float/bool expressions, and emitted as
  * GLSL source (OpenGL 2.1 / GLSL 1.10 and OpenGL ES 2.0 compatible: no arrays, no loops, no
    dynamic indexing, every node is a scalar), and
  * SPIR-V 1.0 for SDL's GPU renderer (Vulkan), matching the interface of SDL's own fragment
    shaders: input location 0 = vertex color, location 1 = texture coordinate, output location 0,
    the source texture at set 2 binding 0 and one uniform block at set 3 binding 0.
It can also evaluate the graph on the CPU (`--preview`) to inspect the result and to compare with
what a GPU produced.

Usage:
  xbrz.py --write src/adapters/sdl/xbrz_shader.inc     regenerate the checked-in shader data
  xbrz.py --check src/adapters/sdl/xbrz_shader.inc     fail when the file is out of date
  xbrz.py --preview IN.png OUT.png [scale]             evaluate on the CPU

The scaler is an original implementation of the published *idea* of xBRZ (Zenju): edge
direction is estimated per pixel corner from weighted YCbCr color distances over a 4x4
neighbourhood, classified as none/normal/dominant, and the corner is then blended along a line
(shallow, steep, both, diagonal) or as a small rounded corner. The thresholds are xBRZ's
defaults. The blend shapes here are continuous geometry (anti-aliased half-planes), so the
output does not depend on an integer scale factor. It is not a port of the reference code and
will not reproduce its output pixel for pixel.
"""
import math
import struct
import sys

# --------------------------------------------------------------------------------------------
# Expression graph (hash-consed; scalar float 'f' and bool 'b' values, vec4 'v' for texel reads)
# --------------------------------------------------------------------------------------------


class Node:
    __slots__ = ("op", "args", "val", "ty", "id", "uses", "cell")

    def __init__(self, op, args, val, ty):
        self.op, self.args, self.val, self.ty = op, args, val, ty
        self.id = -1
        self.uses = 0
        self.cell = False  # constant over one source pixel (used by the CPU evaluator's cache)

    def __add__(self, o): return binop("add", self, o)
    def __radd__(self, o): return binop("add", o, self)
    def __sub__(self, o): return binop("sub", self, o)
    def __rsub__(self, o): return binop("sub", o, self)
    def __mul__(self, o): return binop("mul", self, o)
    def __rmul__(self, o): return binop("mul", o, self)
    def __truediv__(self, o): return binop("div", self, o)
    def __rtruediv__(self, o): return binop("div", o, self)
    def __neg__(self): return binop("sub", 0.0, self)
    def __lt__(self, o): return cmp("lt", self, o)
    def __le__(self, o): return cmp("le", self, o)
    def __gt__(self, o): return cmp("gt", self, o)
    def __ge__(self, o): return cmp("ge", self, o)
    def __and__(self, o): return logic("and", self, o)
    def __or__(self, o): return logic("or", self, o)
    def __invert__(self): return logic("not", self)
    __hash__ = object.__hash__


_table = {}
_nodes = []


def make(op, args, val=None, ty="f"):
    key = (op, tuple(a.id for a in args), val, ty)
    node = _table.get(key)
    if node is None:
        node = Node(op, args, val, ty)
        node.id = len(_nodes)
        _nodes.append(node)
        _table[key] = node
    return node


def const(value):
    if isinstance(value, Node):
        return value
    if isinstance(value, bool):
        return make("bconst", (), value, "b")
    return make("const", (), float(value), "f")


def is_const(n, v=None):
    return n.op == "const" and (v is None or n.val == v)


def binop(op, a, b):
    a, b = const(a), const(b)
    if a.op == "const" and b.op == "const":
        x, y = a.val, b.val
        return const({"add": x + y, "sub": x - y, "mul": x * y, "div": x / y if y else 0.0}[op])
    if op == "add":
        if is_const(a, 0.0): return b
        if is_const(b, 0.0): return a
    elif op == "sub":
        if is_const(b, 0.0): return a
    elif op == "mul":
        if is_const(a, 1.0): return b
        if is_const(b, 1.0): return a
        if is_const(a, 0.0) or is_const(b, 0.0): return const(0.0)
    elif op == "div":
        if is_const(b, 1.0): return a
    if op in ("add", "mul") and b.id < a.id:
        a, b = b, a  # commutative: one node per operand set
    return make(op, (a, b))


def cmp(op, a, b):
    return make(op, (const(a), const(b)), None, "b")


def logic(op, *args):
    args = tuple(const(a) for a in args)
    if op == "and":
        if args[0].op == "bconst": return args[1] if args[0].val else args[0]
        if args[1].op == "bconst": return args[0] if args[1].val else args[1]
    if op == "or":
        if args[0].op == "bconst": return args[0] if args[0].val else args[1]
        if args[1].op == "bconst": return args[1] if args[1].val else args[0]
    if op == "not" and args[0].op == "bconst":
        return const(not args[0].val)
    if op in ("and", "or") and args[1].id < args[0].id:
        args = (args[1], args[0])
    return make(op, args, None, "b")


def fn(op, *args):
    args = tuple(const(a) for a in args)
    if all(a.op == "const" for a in args):
        v = [a.val for a in args]
        return const({"abs": lambda: abs(v[0]), "sqrt": lambda: math.sqrt(v[0]), "floor": lambda: math.floor(v[0]),
                      "min": lambda: min(v), "max": lambda: max(v)}[op]())
    if op in ("min", "max") and args[1].id < args[0].id:
        args = (args[1], args[0])
    return make(op, args)


def sel(cond, a, b):
    cond, a, b = const(cond), const(a), const(b)
    if cond.op == "bconst":
        return a if cond.val else b
    if a is b:
        return a
    return make("sel", (cond, a, b))


def clamp01(x): return fn("min", 1.0, fn("max", 0.0, x))
def sqr(x): return x * x


def cell_level(node):
    """Marks a value that is constant across one source pixel (floor of a pixel coordinate)."""
    node.cell = True
    return node


# --------------------------------------------------------------------------------------------
# The shader: inputs, texel reads and the algorithm
# --------------------------------------------------------------------------------------------

# xBRZ's default configuration (luminance weight 1, equal-color tolerance 30 of 255, steep 2.2,
# dominant 3.6), distances normalised to 0..1.
EQUAL_TOLERANCE = 30.0 / 255.0
STEEP_THRESHOLD = 2.2
DOMINANT_THRESHOLD = 3.6
CORNER_RADIUS = 0.28
K_R, K_B = 0.2126, 0.0722


def build():
    """Returns the output color (r, g, b) as nodes. Uniform params: (source w, source h, pixel
    footprint in source pixels, unused)."""
    u = (make("in", (), "uv.x"), make("in", (), "uv.y"))
    params = [make("in", (), "params.%s" % c) for c in "xyzw"]
    width, height, footprint = params[0], params[1], params[2]
    inv_w, inv_h = 1.0 / width, 1.0 / height

    px, py = u[0] * width, u[1] * height
    cell_x = cell_level(fn("floor", px))
    cell_y = cell_level(fn("floor", py))
    frac_x, frac_y = px - cell_x, py - cell_y

    # 5x5 neighbourhood: texel (dx, dy) relative to the pixel, sampled at its centre (nearest).
    rgb, ycc = {}, {}
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            sx = cell_level((cell_x + (dx + 0.5)) * inv_w)
            sy = cell_level((cell_y + (dy + 0.5)) * inv_h)
            sample = make("sample", (sx, sy), None, "v")
            channels = [make("comp", (sample,), i) for i in range(3)]
            for c in channels:
                c.cell = True
            sample.cell = True
            rgb[dx, dy] = channels
            r, g, b = channels
            y = r * K_R + g * (1.0 - K_R - K_B) + b * K_B
            cb = (b - y) * (0.5 / (1.0 - K_B))
            cr = (r - y) * (0.5 / (1.0 - K_R))
            ycc[dx, dy] = (y, cb, cr)

    def dist(a, b):
        ya, cba, cra = ycc[a]
        yb, cbb, crb = ycc[b]
        return fn("sqrt", sqr(ya - yb) + sqr(cba - cbb) + sqr(cra - crb))

    def same(a, b):  # exactly equal 8-bit colors
        ra, rb = rgb[a], rgb[b]
        return ((fn("abs", ra[0] - rb[0]) + fn("abs", ra[1] - rb[1]) + fn("abs", ra[2] - rb[2])) < 0.002)

    def eq(a, b):  # equal within the tolerance
        return dist(a, b) < EQUAL_TOLERANCE

    # Corners in application order; (sx, sy) is the direction of the corner from the pixel.
    corners = [(1, 1), (-1, 1), (-1, -1), (1, -1)]

    def canonical(corner, cx, cy):
        return (cx * corner[0], cy * corner[1])

    def blend_level(corner):
        """0 none, 1 normal, 2 dominant for this corner of the pixel (xBRZ preProcessCorners)."""
        k = lambda cx, cy: canonical(corner, cx, cy)
        a4 = {"a": (-1, -1), "b": (0, -1), "c": (1, -1), "d": (2, -1), "e": (-1, 0), "f": (0, 0), "g": (1, 0),
              "h": (2, 0), "i": (-1, 1), "j": (0, 1), "k": (1, 1), "l": (2, 1), "m": (-1, 2), "n": (0, 2),
              "o": (1, 2), "p": (2, 2)}
        p = {name: k(*offset) for name, offset in a4.items()}
        skip = (same(p["f"], p["g"]) & same(p["j"], p["k"])) | (same(p["f"], p["j"]) & same(p["g"], p["k"]))
        jg = (dist(p["i"], p["f"]) + dist(p["f"], p["c"]) + dist(p["n"], p["k"]) + dist(p["k"], p["h"])
              + 4.0 * dist(p["j"], p["g"]))
        fk = (dist(p["e"], p["j"]) + dist(p["j"], p["o"]) + dist(p["b"], p["g"]) + dist(p["g"], p["l"])
              + 4.0 * dist(p["f"], p["k"]))
        applies = (jg < fk) & ~same(p["f"], p["g"]) & ~same(p["f"], p["j"]) & ~skip
        dominant = (DOMINANT_THRESHOLD * jg) < fk
        return sel(applies, sel(dominant, 2.0, 1.0), 0.0)

    level = {c: blend_level(c) for c in corners}
    center = (0, 0)
    color = list(rgb[center])
    for corner in corners:
        sx, sy = corner
        k = lambda cx, cy: canonical(corner, cx, cy)
        E, F, H, I = k(0, 0), k(1, 0), k(0, 1), k(1, 1)
        G, C, D, B = k(-1, 1), k(1, -1), k(-1, 0), k(0, -1)
        top_right, bottom_left = level[(sx, -sy)], level[(-sx, sy)]
        mine = level[corner]
        blends = mine > 0.5
        dominant = mine > 1.5
        # xBRZ doLineBlend
        line = dominant | ~(((top_right > 0.5) & ~eq(E, G)) | ((bottom_left > 0.5) & ~eq(E, C))
                            | (~eq(E, I) & eq(G, H) & eq(H, I) & eq(I, F) & eq(F, C)))
        fg, hc = dist(F, G), dist(H, C)
        shallow = ((STEEP_THRESHOLD * fg) <= hc) & ~same(E, G) & ~same(D, G)
        steep = ((STEEP_THRESHOLD * hc) <= fg) & ~same(E, C) & ~same(B, C)
        use_f = dist(E, F) <= dist(E, H)
        blend_color = [sel(use_f, rgb[F][i], rgb[H][i]) for i in range(3)]

        # position inside the pixel, with this corner at (1, 1); coverage ramps over one output pixel
        qx = frac_x if sx > 0 else 1.0 - frac_x
        qy = frac_y if sy > 0 else 1.0 - frac_y
        inv_width = 1.0 / footprint

        def coverage(distance):
            return clamp01(distance * inv_width + 0.5)

        norm = 1.0 / math.sqrt(1.25)
        cov_shallow = coverage((qy + 0.5 * qx - 1.0) * norm)
        cov_steep = coverage((qx + 0.5 * qy - 1.0) * norm)
        cov_diagonal = coverage((qx + qy - 1.5) * (1.0 / math.sqrt(2.0)))
        cov_corner = coverage(CORNER_RADIUS - fn("sqrt", sqr(1.0 - qx) + sqr(1.0 - qy)))
        zero = const(0.0)
        cov = fn("max", fn("max", sel(blends & line & shallow, cov_shallow, zero),
                           sel(blends & line & steep, cov_steep, zero)),
                 fn("max", sel(blends & line & ~shallow & ~steep, cov_diagonal, zero),
                    sel(blends & ~line, cov_corner, zero)))
        color = [color[i] + (blend_color[i] - color[i]) * cov for i in range(3)]

    tint = [make("in", (), "color.%s" % c) for c in "rgb"]
    return [color[i] * tint[i] for i in range(3)], make("in", (), "color.a")


# --------------------------------------------------------------------------------------------
# Analysis
# --------------------------------------------------------------------------------------------


def topo(roots):
    order, seen = [], set()
    stack = [(r, False) for r in reversed(roots)]
    while stack:
        node, done = stack.pop()
        if done:
            order.append(node)
            continue
        if node.id in seen:
            continue
        seen.add(node.id)
        stack.append((node, True))
        for a in reversed(node.args):
            stack.append((a, False))
    for n in order:
        n.uses = 0
    for n in order:
        for a in n.args:
            a.uses += 1
    for r in roots:
        r.uses += 1
    return order


# --------------------------------------------------------------------------------------------
# GLSL
# --------------------------------------------------------------------------------------------


def number_text(v):
    text = repr(float(v))
    if "e" in text or "inf" in text or "nan" in text:
        text = "%.9e" % v
    return text


# Source dialects of the same scalar program. `vec4` is the type of a texel read; `uv`, `color`
# and `params` say how the shader's inputs are spelled; `sample` builds a nearest read at (x, y).
DIALECTS = {
    "glsl": dict(vec4="vec4", uv="v_uv.%s", color="v_color.%s", params="u_params.%s",
                 sample="texture2D(u_texture, vec2(%s, %s))"),
    "hlsl": dict(vec4="float4", uv="input.v_uv.%s", color="input.v_color.%s", params="u_params.%s",
                 sample="u_texture.SampleLevel(u_sampler, float2(%s, %s), 0.0)"),
    "msl": dict(vec4="float4", uv="in.v_uv.%s", color="in.v_color.%s", params="Constants.u_params.%s",
                sample="u_texture.sample(u_sampler, float2(%s, %s), level(0.0))"),
}


def emit_text(roots, dialect):
    """Statements of the program and the four output expressions, as source text."""
    d = DIALECTS[dialect]
    order = topo(roots)
    names, lines = {}, []
    temp = [0]
    for n in order:
        op = n.op
        a = [names[x.id] for x in n.args]
        if op == "const": expr = number_text(n.val)
        elif op == "bconst": expr = "true" if n.val else "false"
        elif op == "in":
            kind, _, component = n.val.partition(".")
            expr = d[{"uv": "uv", "color": "color", "params": "params"}[kind]] % component
        elif op == "sample": expr = d["sample"] % (a[0], a[1])
        elif op == "comp": expr = "%s.%s" % (a[0], "xyzw"[n.val])
        elif op in ("add", "sub", "mul", "div"): expr = "(%s %s %s)" % (a[0], {"add": "+", "sub": "-", "mul": "*", "div": "/"}[op], a[1])
        elif op in ("lt", "le", "gt", "ge"): expr = "(%s %s %s)" % (a[0], {"lt": "<", "le": "<=", "gt": ">", "ge": ">="}[op], a[1])
        elif op == "and": expr = "(%s && %s)" % (a[0], a[1])
        elif op == "or": expr = "(%s || %s)" % (a[0], a[1])
        elif op == "not": expr = "(!%s)" % a[0]
        elif op in ("abs", "sqrt", "floor"): expr = "%s(%s)" % (op, a[0])
        elif op in ("min", "max"): expr = "%s(%s, %s)" % (op, a[0], a[1])
        elif op == "sel": expr = "(%s ? %s : %s)" % (a[0], a[1], a[2])
        else: raise ValueError(op)
        cheap = op in ("const", "bconst", "in", "comp")
        if cheap or n.uses <= 1 and len(expr) < 120:
            names[n.id] = expr
        else:
            temp[0] += 1
            name = "t%d" % temp[0]
            ty = {"f": "float", "b": "bool", "v": d["vec4"]}[n.ty]
            lines.append("    %s %s = %s;" % (ty, name, expr))
            names[n.id] = name
    return lines, [names[r.id] for r in roots]


def hlsl_source(roots):
    """Complete pixel shader for SDL's GPU renderer on Direct3D 12 (shader model 5.1 DXBC, compiled
    at run time): fragment uniforms in space 3, the frame texture and sampler in space 2."""
    lines, outs = emit_text(roots, "hlsl")
    return "\n".join([
        "cbuffer Constants : register(b0, space3) { float4 u_params; };",
        "Texture2D u_texture : register(t0, space2);",
        "SamplerState u_sampler : register(s0, space2);",
        "struct PSInput { float4 v_color : COLOR0; float2 v_uv : TEXCOORD0; };",
        "float4 main(PSInput input) : SV_Target {"] + lines + [
        "    return float4(%s, %s, %s, %s);" % tuple(outs), "}", ""])


def msl_source(roots):
    """Complete fragment function for SDL's GPU renderer on Metal (compiled by Metal at run time)."""
    lines, outs = emit_text(roots, "msl")
    return "\n".join([
        "#include <metal_stdlib>",
        "using namespace metal;",
        "struct type_Constants { float4 u_params; };",
        "struct main0_out { float4 out_color [[color(0)]]; };",
        "struct main0_in { float4 v_color [[user(locn0)]]; float2 v_uv [[user(locn1)]]; };",
        "fragment main0_out main0(main0_in in [[stage_in]], constant type_Constants& Constants [[buffer(0)]], "
        "texture2d<float> u_texture [[texture(0)]], sampler u_sampler [[sampler(0)]]) {",
        "    main0_out out = {};"] + lines + [
        "    out.out_color = float4(%s, %s, %s, %s);" % tuple(outs), "    return out;", "}", ""])


# --------------------------------------------------------------------------------------------
# SPIR-V 1.0
# --------------------------------------------------------------------------------------------

GLSL_STD = {"abs": 4, "floor": 8, "sqrt": 31, "min": 37, "max": 40}


class Spirv:
    def __init__(self):
        self.next_id = 1
        self.annotations, self.globals, self.code = [], [], []
        self.cache = {}

    def new_id(self):
        self.next_id += 1
        return self.next_id - 1

    @staticmethod
    def add(section, opcode, *operands):
        ws = []
        for o in operands:
            ws.extend(o if isinstance(o, list) else [o])
        section.append(((len(ws) + 1) << 16) | opcode)
        section.extend(ws)

    @staticmethod
    def string(text):
        b = text.encode() + b"\0"
        b += b"\0" * (-len(b) % 4)
        return list(struct.unpack("<%dI" % (len(b) // 4), b))

    def declare(self, key, opcode, *operands):
        """A type, constant or variable, declared once, in dependency order."""
        if key not in self.cache:
            rid = self.new_id()
            if opcode in (41, 42, 43, 59):  # constants and variables: result type, then result id
                self.add(self.globals, opcode, operands[0], rid, *operands[1:])
            else:  # types: result id first
                self.add(self.globals, opcode, rid, *operands)
            self.cache[key] = rid
        return self.cache[key]

    # types
    def void(self): return self.declare("void", 19)
    def boolean(self): return self.declare("bool", 20)
    def float(self): return self.declare("float", 22, 32)
    def uint(self): return self.declare("uint", 21, 32, 0)
    def vec(self, n): return self.declare(("vec", n), 23, self.float(), n)
    def pointer(self, storage, base): return self.declare(("ptr", storage, base), 32, storage, base)
    def constant(self, value):
        bits = struct.unpack("<I", struct.pack("<f", value))[0]
        return self.declare(("c", bits), 43, self.float(), bits)
    def uint_constant(self, value): return self.declare(("u", value), 43, self.uint(), value)
    def bool_constant(self, value): return self.declare(("b", value), 41 if value else 42, self.boolean())


def emit_spirv(roots):
    """Fragment shader module for SDL's GPU renderer (Vulkan)."""
    order = topo(roots)
    m = Spirv()
    add = Spirv.add
    ext, main, label = m.new_id(), m.new_id(), m.new_id()
    f, v2, v4, boolean = m.float(), m.vec(2), m.vec(4), m.boolean()

    # interface variables
    v_color = m.declare("v_color", 59, m.pointer(1, v4), 1)
    v_uv = m.declare("v_uv", 59, m.pointer(1, v2), 1)
    v_out = m.declare("v_out", 59, m.pointer(3, v4), 3)
    block = m.declare("block", 30, v4)
    v_params = m.declare("params", 59, m.pointer(2, block), 2)
    image = m.declare("image", 25, f, 1, 0, 0, 0, 1, 0)
    sampled = m.declare("sampled", 27, image)
    v_tex = m.declare("tex", 59, m.pointer(0, sampled), 0)
    add(m.annotations, 71, v_color, 30, 0)
    add(m.annotations, 71, v_uv, 30, 1)
    add(m.annotations, 71, v_out, 30, 0)
    add(m.annotations, 71, block, 2)
    add(m.annotations, 72, block, 0, 35, 0)
    add(m.annotations, 71, v_params, 34, 3)
    add(m.annotations, 71, v_params, 33, 0)
    add(m.annotations, 71, v_tex, 34, 2)
    add(m.annotations, 71, v_tex, 33, 0)

    void = m.void()
    fn_type = m.declare("fn", 33, void)
    add(m.code, 54, void, main, 0, fn_type)
    add(m.code, 248, label)

    inputs = {}

    def load(key, pointer, ty):
        if key not in inputs:
            inputs[key] = m.new_id()
            add(m.code, 61, ty, inputs[key], pointer)
        return inputs[key]

    def extract(vec, index):
        key = ("x", vec, index)
        if key not in inputs:
            inputs[key] = m.new_id()
            add(m.code, 81, f, inputs[key], vec, index)
        return inputs[key]

    ids = {}
    for n in order:
        op = n.op
        a = [ids[x.id] for x in n.args]
        if op == "const":
            ids[n.id] = m.constant(n.val)
            continue
        if op == "bconst":
            ids[n.id] = m.bool_constant(n.val)
            continue
        if op == "in":
            name = n.val
            if name.startswith("uv."):
                ids[n.id] = extract(load("uv", v_uv, v2), "xy".index(name[-1]))
            elif name.startswith("color."):
                ids[n.id] = extract(load("color", v_color, v4), "rgba".index(name[-1]))
            else:
                if "params" not in inputs:
                    chain = m.new_id()
                    add(m.code, 65, m.pointer(2, v4), chain, v_params, m.uint_constant(0))  # OpAccessChain
                    inputs["params"] = m.new_id()
                    add(m.code, 61, v4, inputs["params"], chain)
                ids[n.id] = extract(inputs["params"], "xyzw".index(name[-1]))
            continue
        rid = m.new_id()
        ids[n.id] = rid
        if op == "sample":
            coord, texture = m.new_id(), m.new_id()
            add(m.code, 80, v2, coord, a[0], a[1])
            add(m.code, 61, sampled, texture, v_tex)
            add(m.code, 88, v4, rid, texture, coord, 2, m.constant(0.0))  # OpImageSampleExplicitLod, Lod
        elif op == "comp":
            add(m.code, 81, f, rid, a[0], n.val)
        elif op in ("add", "sub", "mul", "div"):
            add(m.code, {"add": 129, "sub": 131, "mul": 133, "div": 136}[op], f, rid, a[0], a[1])
        elif op in ("lt", "le", "gt", "ge"):
            add(m.code, {"lt": 184, "gt": 186, "le": 188, "ge": 190}[op], boolean, rid, a[0], a[1])
        elif op in ("and", "or"):
            add(m.code, 167 if op == "and" else 166, boolean, rid, a[0], a[1])
        elif op == "not":
            add(m.code, 168, boolean, rid, a[0])
        elif op in GLSL_STD:
            add(m.code, 12, f, rid, ext, GLSL_STD[op], *a)  # OpExtInst
        elif op == "sel":
            add(m.code, 169, f, rid, a[0], a[1], a[2])
        else:
            raise ValueError(op)
    out = m.new_id()
    add(m.code, 80, v4, out, *[ids[r.id] for r in roots])
    add(m.code, 62, v_out, out)
    add(m.code, 253)
    add(m.code, 56)

    module = [0x07230203, 0x00010000, 0, m.next_id, 0]
    add(module, 17, 1)  # Capability Shader
    add(module, 11, ext, Spirv.string("GLSL.std.450"))
    add(module, 14, 0, 1)  # Logical, GLSL450
    add(module, 15, 4, main, Spirv.string("main"), v_color, v_uv, v_out)  # Fragment
    add(module, 16, main, 7)  # OriginUpperLeft
    module += m.annotations + m.globals + m.code
    return module


# --------------------------------------------------------------------------------------------
# Generated file
# --------------------------------------------------------------------------------------------


def shader_outputs():
    rgb, alpha = build()
    return list(rgb) + [alpha]


def split_parts(text):
    """Pieces under MSVC's string literal limit, cut at line ends."""
    parts, chunk = [], ""
    for line in text.splitlines(True):
        if len(chunk) + len(line) > 12000:
            parts.append(chunk)
            chunk = ""
        chunk += line
    parts.append(chunk)
    return parts


def generated_source():
    lines, outs = emit_text(shader_outputs(), "glsl")
    glsl = "\n".join(lines) + "\n    xbrz_out = vec4(%s, %s, %s, %s);\n" % tuple(outs)
    module = emit_spirv(shader_outputs())
    out = ["// SPDX-License-Identifier: MIT", "// Copyright (c) 2026 OpenLF2 contributors", "",
           "// Generated by tools/shaders/xbrz.py -- do not edit; regenerate with `tools/shaders/xbrz.py --write`.",
           "// One shader program in four forms: the body of a GLSL fragment shader (OpenGL 2.1 / OpenGL ES 2.0),",
           "// SPIR-V 1.0 (Vulkan), HLSL (Direct3D 12, compiled at run time) and MSL (Metal, compiled at run time)",
           "// for SDL's GPU renderer.",
           "#pragma once", "#include <array>", "#include <cstdint>", "#include <string_view>", "",
           "namespace openlf2::xbrz_shader {", ""]
    for name, text, note in (("glsl_body", glsl, "the statements of the GLSL fragment shader body"),
                             ("hlsl", hlsl_source(shader_outputs()), "the complete HLSL pixel shader"),
                             ("msl", msl_source(shader_outputs()), "the complete Metal fragment function")):
        parts = split_parts(text)
        out.append("// %s; pieces under MSVC's string literal limit, to be concatenated." % note)
        out.append("inline constexpr std::array<std::string_view, %d> %s{" % (len(parts), name))
        for part in parts:
            out.append('R"SHADER(%s)SHADER",' % part)
        out.append("};")
        out.append("")
    out.append("inline constexpr std::array<std::uint32_t, %d> spirv{" % len(module))
    for i in range(0, len(module), 8):
        out.append("    " + ", ".join("0x%08x" % w for w in module[i:i + 8]) + ",")
    out.append("};")
    out.append("}")
    out.append("")
    return "\n".join(out)


# --------------------------------------------------------------------------------------------
# CPU evaluation (previews and comparisons)
# --------------------------------------------------------------------------------------------


def evaluate_image(image, scale):
    """image: list of rows of (r, g, b) 0..255 tuples. Returns the scaled rows (integer scale)."""
    roots = shader_outputs()
    order = topo(roots)
    depends = {}
    for n in order:
        if n.cell:
            depends[n.id] = False
        else:
            depends[n.id] = (n.op == "in" and n.val.startswith("uv.")) or any(depends[a.id] for a in n.args)
    # A cell-level value (floor of the pixel coordinate) is computed from pixel-level ones, so the
    # once-per-cell pass also runs their ancestors (with the cell's first pixel).
    needed = set()
    stack = [n for n in order if not depends[n.id]]
    while stack:
        n = stack.pop()
        if n.id not in needed:
            needed.add(n.id)
            stack.extend(n.args)
    cell_nodes = [n for n in order if n.id in needed]
    pixel_nodes = [n for n in order if depends[n.id]]
    height, width = len(image), len(image[0])

    def step(nodes, values, uv):
        for n in nodes:
            op, a = n.op, [values[x.id] for x in n.args]
            if op == "const" or op == "bconst": v = n.val
            elif op == "in":
                v = {"uv.x": uv[0], "uv.y": uv[1], "params.x": float(width), "params.y": float(height),
                     "params.z": 1.0 / scale, "params.w": 0.0}.get(n.val, 1.0)
            elif op == "sample":
                x = min(max(int(math.floor(a[0] * width)), 0), width - 1)
                y = min(max(int(math.floor(a[1] * height)), 0), height - 1)
                v = tuple(c / 255.0 for c in image[y][x]) + (1.0,)
            elif op == "comp": v = a[0][n.val]
            elif op == "add": v = a[0] + a[1]
            elif op == "sub": v = a[0] - a[1]
            elif op == "mul": v = a[0] * a[1]
            elif op == "div": v = a[0] / a[1]
            elif op == "lt": v = a[0] < a[1]
            elif op == "le": v = a[0] <= a[1]
            elif op == "gt": v = a[0] > a[1]
            elif op == "ge": v = a[0] >= a[1]
            elif op == "and": v = a[0] and a[1]
            elif op == "or": v = a[0] or a[1]
            elif op == "not": v = not a[0]
            elif op == "abs": v = abs(a[0])
            elif op == "sqrt": v = math.sqrt(a[0])
            elif op == "floor": v = float(math.floor(a[0]))
            elif op == "min": v = min(a)
            elif op == "max": v = max(a)
            elif op == "sel": v = a[1] if a[0] else a[2]
            else: raise ValueError(op)
            values[n.id] = v

    out = []
    cached = {"key": None, "values": {}}  # cell-level values, shared by the pixels of one source pixel
    for oy in range(height * scale):
        row = []
        for ox in range(width * scale):
            uv = ((ox + 0.5) / (width * scale), (oy + 0.5) / (height * scale))
            key = (ox // scale, oy // scale)
            if key != cached["key"]:
                cached["values"] = {}
                step(cell_nodes, cached["values"], uv)
                cached["key"] = key
            values = dict(cached["values"])
            step(pixel_nodes, values, uv)
            row.append(tuple(int(round(min(max(values[r.id], 0.0), 1.0) * 255)) for r in roots[:3]))
        out.append(row)
    return out



def main(argv):
    if len(argv) >= 3 and argv[1] in ("--write", "--check"):
        text = generated_source()
        if argv[1] == "--write":
            open(argv[2], "w").write(text)
            print("wrote", argv[2], len(text), "bytes")
            return 0
        if open(argv[2]).read() != text:
            print("%s is out of date; run tools/shaders/xbrz.py --write %s" % (argv[2], argv[2]), file=sys.stderr)
            return 1
        return 0
    if len(argv) >= 4 and argv[1] == "--preview":
        from PIL import Image
        scale = int(argv[4]) if len(argv) > 4 else 3
        source = Image.open(argv[2]).convert("RGB")
        w, h = source.size
        pixels = list(source.getdata())
        image = [pixels[y * w:(y + 1) * w] for y in range(h)]
        result = evaluate_image(image, scale)
        out = Image.new("RGB", (w * scale, h * scale))
        out.putdata([p for row in result for p in row])
        out.save(argv[3])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
