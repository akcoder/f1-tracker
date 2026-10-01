#!/usr/bin/env python3
"""Render the GENERATED circuit traces, using the device's own fit maths.

MAP-3/MAP-5 cannot be checked on hardware yet (section 14.1), but the geometry
can be checked here: this parses f1-tracker/f1_circuits.h - the actual bytes the
firmware carries - and draws them through the same fit_box() arithmetic
f1_map.h uses. A wrong rotation, a squashed aspect or a trace escaping its box
shows up immediately.

  python3 tools/preview_circuits.py              # contact sheet of all 40
  python3 tools/preview_circuits.py --id mc-1929 # one card at real size
"""
import argparse, os, re, math
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HDR = os.path.join(ROOT, "f1-tracker", "f1_circuits.h")
OUT = os.path.join(ROOT, "reference", "mockups")


def parse_header():
    src = open(HDR).read()
    coord_max = int(re.search(r"COORD_MAX = (\d+)", src).group(1))
    pts_blob = re.search(r"PTS\[\d+\] = \{(.*?)\};", src, re.S).group(1)
    pts = [int(v) for v in re.findall(r"-?\d+", pts_blob)]
    rows = re.findall(
        r'\{"([^"]+)", "([^"]*)", "([^"]*)", (\d+), (\d+), (-?\d+), (-?\d+), '
        r'(\d+), (-?\d+), (\d+), (\d+), (-?\d+), (true|false)\}', src)
    circuits = []
    for r in rows:
        circuits.append(dict(id=r[0], name=r[1], loc=r[2], first=int(r[3]), n=int(r[4]),
                             hw=int(r[5]), hh=int(r[6]), length=int(r[7]), alt=int(r[8]),
                             opened=int(r[9]), firstgp=int(r[10]), rot_cdeg=int(r[11]),
                             closed=r[12] == "true"))
    return coord_max, pts, circuits


def fit_box(c, w, h, pad=3.0):
    """Mirrors f1::map::fit_box exactly."""
    aw, ah = w - 2 * pad, h - 2 * pad
    hw = c["hw"] or 1
    hh = c["hh"] or 1
    return min(aw / (2 * hw), ah / (2 * hh)), w / 2.0, h / 2.0


def points(c, pts, scale, cx, cy):
    out = []
    for i in range(c["n"]):
        x = pts[c["first"] * 2 + i * 2]
        y = pts[c["first"] * 2 + i * 2 + 1]
        out.append((cx + x * scale, cy - y * scale))   # y negated, as on device
    if c["closed"]:
        out.append(out[0])
    return out


def draw_one(d, c, pts, ox, oy, w, h, font=None, label=True):
    s, cx, cy = fit_box(c, w, h)
    p = [(ox + x, oy + y) for x, y in points(c, pts, s, cx, cy)]
    d.rectangle([ox, oy, ox + w, oy + h], fill=(0x0B, 0x12, 0x20))
    d.line(p, fill=(0xE8, 0xED, 0xF7), width=max(1, int(h / 110)), joint="curve")
    # start/finish tick, perpendicular at point 0
    ax, ay = p[0]; bx, by = p[1]
    dx, dy = bx - ax, by - ay
    m = math.hypot(dx, dy) or 1.0
    px, py = -dy / m, dx / m
    L = h / 20.0
    d.line([(ax - px * L, ay - py * L), (ax + px * L, ay + py * L)],
           fill=(0xFF, 0x8A, 0x1F), width=max(1, int(h / 110)))
    # north arrow where the generator applied a rotation
    if c["rot_cdeg"] != 0:
        th = math.radians((-c["rot_cdeg"] / 100.0) % 360.0)
        nx, ny, NL = ox + w - h * 0.07, oy + h * 0.07, h * 0.05
        d.line([(nx + math.sin(th) * NL, ny + math.cos(th) * NL),
                (nx - math.sin(th) * NL, ny - math.cos(th) * NL)],
               fill=(0x7E, 0x8B, 0xB3), width=1)
    if label and font:
        d.text((ox + 3, oy + h - 13), c["id"], font=font, fill=(0x7E, 0x8B, 0xB3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/tmp/RobotoMono.ttf")
    ap.add_argument("--id")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    coord_max, pts, circuits = parse_header()
    os.makedirs(a.out, exist_ok=True)
    print(f"parsed {len(circuits)} circuits, {len(pts)//2} points, COORD_MAX={coord_max}")
    f9 = ImageFont.truetype(a.font, 9) if os.path.exists(a.font) else None
    f14 = ImageFont.truetype(a.font, 14) if os.path.exists(a.font) else None

    if a.id:
        c = next(x for x in circuits if x["id"] == a.id)
        img = Image.new("RGB", (480, 480), (0, 0, 0))
        d = ImageDraw.Draw(img)
        if f14:
            d.text((10, 6), "CIRCUIT", font=f14, fill=(0xFF, 0x8A, 0x1F))
            d.text((10, 26), c["name"][:40], font=f14, fill=(0xC9, 0xD3, 0xF2))
        draw_one(d, c, pts, 40, 56, 400, 320, label=False)       # the real card box
        if f14:
            d.text((12, 386), f"{c['loc']}\n{c['length']/1000:.3f} km   first GP {c['firstgp']}",
                   font=f14, fill=(0x7E, 0x8B, 0xB3))
        p = os.path.join(a.out, f"circuit-card-{a.id}.png")
        img.save(p); print("wrote", p)
        return

    cols, rows = 8, 5
    cw, ch = 120, 100
    img = Image.new("RGB", (cols * cw, rows * ch), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for i, c in enumerate(sorted(circuits, key=lambda x: x["id"])):
        ox, oy = (i % cols) * cw, (i // cols) * ch
        draw_one(d, c, pts, ox + 2, oy + 2, cw - 4, ch - 4, f9)
    p = os.path.join(a.out, "circuits-contact-sheet.png")
    img.save(p)
    print("wrote", p)
    rotated = sum(1 for c in circuits if c["rot_cdeg"] != 0)
    print(f"rotated for fill: {rotated}/{len(circuits)}  (north arrow shown on those)")


if __name__ == "__main__":
    main()
