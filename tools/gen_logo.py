#!/usr/bin/env python3
"""Generate the F1 Tracker mark, at every size the project needs.

The mark is the Monza trace - the same int16 geometry f1_circuits.h carries and
the device draws - on a dark disc, with the orange start/finish tick the circuit
cards use. It is deliberately made FROM the project's own data rather than
drawn: it is a picture of what the device does, it uses no F1 or team trademark
(decision 44), and it stays in step if the traces are ever regenerated.

Monza because its outline is the most recognisable of the 40 at icon size: two
long straights and a distinctive kink, where most circuits become a blob.

Outputs:
  reference/logo/f1-tracker-512.png   master
  reference/logo/boot-200.png         the device's boot screen (RGB565 via ESPHome)
  reference/logo/web-64.png           the web UI
  reference/logo/favicon-32.png       fallback raster icon
  f1-tracker/f1_logo.h                the SVG favicon, inlined into the web page

  python3 tools/gen_logo.py
"""
import math, os, re, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "reference", "logo")

NAVY = (0x12, 0x1C, 0x33)
WHITE = (0xF2, 0xF5, 0xFC)
ORANGE = (0xFF, 0x8A, 0x1F)
# Pure black, so the square canvas is invisible against the device's black page
# and the disc is the whole mark. The alpha variant exists for anywhere that
# wants it on a different ground.
DEEP = (0x00, 0x00, 0x00)
TRACE = "it-1922"          # Monza
PROJECT_URL = "https://github.com/akcoder/f1-tracker"
SS = 4                     # supersample then box-filter: smooth edges without a rasteriser


def trace_points(tid):
    src = open(os.path.join(ROOT, "f1-tracker", "f1_circuits.h")).read()
    pts = [int(v) for v in re.findall(
        r"-?\d+", re.search(r"PTS\[\d+\] = \{(.*?)\};", src, re.S).group(1))]
    for cid, first, n, hw, hh in re.findall(
            r'\{"([^"]+)", "[^"]*", "[^"]*", (\d+), (\d+), (-?\d+), (-?\d+),', src):
        if cid == tid:
            f, n, hw, hh = int(first), int(n), int(hw), int(hh)
            return [(pts[f * 2 + i * 2], pts[f * 2 + i * 2 + 1]) for i in range(n)], hw, hh
    raise KeyError(tid)


def layout(size, pad=0.16):
    ps, hw, hh = trace_points(TRACE)
    k = min((size - 2 * pad * size) / (2 * max(hw, 1)),
            (size - 2 * pad * size) / (2 * max(hh, 1)))
    return [(size / 2 + x * k, size / 2 - y * k) for x, y in ps]


def render(S, disc=True, transparent=False):
    s = S * SS
    mode = "RGBA" if transparent else "RGB"
    img = Image.new(mode, (s, s), (0, 0, 0, 0) if transparent else DEEP)
    d = ImageDraw.Draw(img)
    if disc:
        d.ellipse([0, 0, s - 1, s - 1], fill=NAVY + ((255,) if transparent else ()))
    p = layout(s)
    w = max(2, int(s * 0.062))
    d.line(p + [p[0]], fill=WHITE + ((255,) if transparent else ()), width=w, joint="curve")
    ax, ay = p[0]; bx, by = p[1]
    dx, dy = bx - ax, by - ay
    m = math.hypot(dx, dy) or 1.0
    px, py = -dy / m, dx / m
    L = s * 0.062
    d.line([(ax - px * L, ay - py * L), (ax + px * L, ay + py * L)],
           fill=ORANGE + ((255,) if transparent else ()), width=int(w * 1.15))
    return img.resize((S, S), Image.LANCZOS)


def svg(size=64):
    """A vector favicon. Tiny, crisp at any size, and inlinable as a data URI -
    which is what the web page needs, since ESPHome's bundled index ships an
    empty icon and offers no option to replace it."""
    p = layout(100.0)
    pathd = "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in p) + " Z"
    ax, ay = p[0]; bx, by = p[1]
    dx, dy = bx - ax, by - ay
    m = math.hypot(dx, dy) or 1.0
    px, py = -dy / m, dx / m
    L = 6.5
    tick = (f"M{ax - px * L:.1f} {ay - py * L:.1f} L{ax + px * L:.1f} {ay + py * L:.1f}")
    return ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'>"
            "<circle cx='50' cy='50' r='50' fill='%23121C33'/>"
            f"<path d='{pathd}' fill='none' stroke='%23F2F5FC' stroke-width='6.2' "
            "stroke-linejoin='round' stroke-linecap='round'/>"
            f"<path d='{tick}' stroke='%23FF8A1F' stroke-width='7.1' stroke-linecap='butt'/>"
            "</svg>")


def header_svg():
    """The web page's header badge. ESPHome's esp-logo element renders a fixed
    SVG string; NET-10 replaces it. Raw SVG here, not a data URI."""
    p = layout(40.0)
    pathd = "M" + " L".join(f"{x:.2f} {y:.2f}" for x, y in p) + " Z"
    ax, ay = p[0]; bx, by = p[1]
    dx, dy = bx - ax, by - ay
    m = math.hypot(dx, dy) or 1.0
    px, py = -dy / m, dx / m
    L = 2.6
    tick = f"M{ax - px * L:.2f} {ay - py * L:.2f} L{ax + px * L:.2f} {ay + py * L:.2f}"
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 40">'
           '<circle cx="20" cy="20" r="19" fill="#121C33"/>'
           f'<path d="{pathd}" fill="none" stroke="#F2F5FC" stroke-width="2.5" '
           'stroke-linejoin="round" stroke-linecap="round"/>'
           f'<path d="{tick}" stroke="#FF8A1F" stroke-width="2.9" stroke-linecap="butt"/>'
           '</svg>')
    # The badge links to the project page. The string lands inside the app's JS template
    # literal, so no backtick and no ${ may appear in it.
    return (f'<a href="{PROJECT_URL}" target="_blank" rel="noopener" '
            'title="F1 Tracker on GitHub" style="display:block;line-height:0">' + svg + '</a>')


def main():
    os.makedirs(OUT, exist_ok=True)
    sizes = [("f1-tracker-512.png", 512), ("boot-200.png", 200),
             ("web-64.png", 64), ("favicon-32.png", 32), ("favicon-16.png", 16)]
    for name, S in sizes:
        render(S).save(os.path.join(OUT, name))
        print(f"  {name:22} {S}x{S}")
    render(512, transparent=True).save(os.path.join(OUT, "f1-tracker-512-alpha.png"))

    s = svg()
    hdr = os.path.join(ROOT, "f1-tracker", "f1_logo.h")
    body = s.replace("\\", "\\\\").replace('"', '\\"')
    with open(hdr, "w") as f:
        f.write("#pragma once\n")
        f.write("// GENERATED by tools/gen_logo.py - do not edit.\n")
        f.write("//\n")
        f.write("// The mark is the Monza trace from f1_circuits.h - the same geometry the\n")
        f.write("// device draws - with the orange start/finish tick the circuit cards use.\n")
        f.write("// Made from the project's own data rather than drawn, and using no F1 or\n")
        f.write("// team trademark (decision 44).\n")
        f.write("//\n")
        f.write("// As an inline SVG data URI, because ESPHome's bundled web_server page ships\n")
        f.write("// an empty icon and offers no option to replace it (NET-10).\n")
        f.write("#pragma once\n" if False else "")
        f.write("\nnamespace f1 {\nnamespace logo {\n\n")
        f.write(f'inline constexpr const char *FAVICON_SVG =\n    "{body}";\n')
        f.write(f"inline constexpr int FAVICON_SVG_LEN = {len(s)};\n\n")
        h = header_svg()
        hb = h.replace("\\", "\\\\").replace('"', '\\"')
        f.write(f'inline constexpr const char *HEADER_SVG =\n    "{hb}";\n')
        f.write(f"inline constexpr int HEADER_SVG_LEN = {len(h)};\n")
        f.write("\n}  // namespace logo\n}  // namespace f1\n")
    print(f"  f1_logo.h              favicon {len(s)} B, header {len(header_svg())} B")

    # contact sheet so the mark can be judged at the sizes it will actually appear
    sheet = Image.new("RGB", (560, 300), (0x14, 0x16, 0x1A))
    sheet.paste(render(256), (12, 22))
    x = 290
    for S in (16, 32, 48, 64, 96):
        sheet.paste(render(S), (x, 22))
        x += S + 10
    x = 290
    for S in (16, 32, 48, 64, 96):
        sheet.paste(render(S).resize((S * 2, S * 2), Image.NEAREST), (x, 150))
        x += S * 2 + 8
    sheet.save(os.path.join(OUT, "logo-sheet.png"))
    print(f"\nwrote {OUT}")


if __name__ == "__main__":
    main()
