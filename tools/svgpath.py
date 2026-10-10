"""A small SVG path reader: M L H V C S Q Z, absolute and relative, flattened to points.
Just enough for the project's own marks (tools/gen_logo.py); not a general SVG library."""
import re

_TOK = re.compile(r"([MmLlHhVvCcSsQqZz])|(-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)")


def _bez(p0, p1, p2, p3, n):
    out = []
    for i in range(1, n + 1):
        t = i / n; u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


def flatten(d, per_curve=24):
    """The first subpath of path data `d` as a list of (x, y)."""
    toks = [(m.group(1), m.group(2)) for m in _TOK.finditer(d)]
    i, cmd = 0, None
    cur = start = (0.0, 0.0)
    last_c = None
    pts = []

    def num():
        nonlocal i
        v = float(toks[i][1]); i += 1
        return v

    while i < len(toks):
        if toks[i][0]:
            cmd = toks[i][0]; i += 1
            if cmd in "Zz":
                cur = start; last_c = None
                break
            continue
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = cur if rel else (0.0, 0.0)
        if c == "M":
            cur = (ox + num(), oy + num()); start = cur; pts.append(cur)
            cmd = "l" if rel else "L"; last_c = None
        elif c == "L":
            cur = (ox + num(), oy + num()); pts.append(cur); last_c = None
        elif c == "H":
            cur = (ox + num() if rel else num(), cur[1]); pts.append(cur); last_c = None
        elif c == "V":
            cur = (cur[0], oy + num() if rel else num()); pts.append(cur); last_c = None
        elif c == "C":
            p1 = (ox + num(), oy + num()); p2 = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            pts += _bez(cur, p1, p2, p3, per_curve); cur = p3; last_c = p2
        elif c == "S":
            p1 = (2 * cur[0] - last_c[0], 2 * cur[1] - last_c[1]) if last_c else cur
            p2 = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            pts += _bez(cur, p1, p2, p3, per_curve); cur = p3; last_c = p2
        elif c == "Q":
            q = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            p1 = (cur[0] + 2 / 3 * (q[0] - cur[0]), cur[1] + 2 / 3 * (q[1] - cur[1]))
            p2 = (p3[0] + 2 / 3 * (q[0] - p3[0]), p3[1] + 2 / 3 * (q[1] - p3[1]))
            pts += _bez(cur, p1, p2, p3, per_curve); cur = p3; last_c = None
        else:
            raise ValueError(f"unsupported path command {cmd}")
    return pts


def path_d(svg_text):
    m = re.search(r'<path[^>]*?\sd="([^"]+)"', svg_text, re.S)
    if not m:
        raise ValueError("no path in the SVG")
    return m.group(1)
