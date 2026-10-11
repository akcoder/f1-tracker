#!/usr/bin/env python3
"""Generate the F1 Tracker mark, at every size the project needs.

The mark is the Monza outline on a dark disc, with the orange start/finish tick the
circuit cards use. The outline is reference/monza-smoothed.svg - the traced Monza
(f1_circuits.h, OSM-derived) redrawn by hand to take out the jogs that read as bumps at
logo size - resampled to 128 even steps and written to f1-tracker/f1_logomark.h, so the
cars lapping the logo on the device follow the very same line. It uses no F1 or team
trademark (decision 44).

Monza because its outline is the most recognisable of the 40 at icon size: two
long straights and a distinctive kink, where most circuits become a blob.

Outputs:
  reference/logo/f1-tracker-512.png   master
  reference/logo/boot-200.png         the device's boot screen (RGB565 via ESPHome)
  reference/logo/web-64.png           the web UI
  reference/logo/favicon-32.png       fallback raster icon
  f1-tracker/f1_logo.h                the SVG favicon, inlined into the web page
  f1-tracker/f1_logomark.h            the outline's points, for the cars on the device

  python3 tools/gen_logo.py
"""
import math, os, re, subprocess, sys
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
TRACE = "it-1922"          # Monza: the traced circuit, used for the start line and direction
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


MARK_SVG = os.path.join(ROOT, "reference", "monza-smoothed.svg")
MARK_N = 128           # points round the lap (f1_animlogic.h MAX_PTS is 160)
MARK_SCALE = 10000     # int16 units for the larger half-extent, as f1_circuits.h


def mark_points():
    """The outline from the hand-smoothed SVG: (points, half_w, half_h) in int16 units, y up,
    centred, `MARK_N` even steps in arc length, in the traced circuit's direction round the lap,
    starting where the traced circuit's start/finish line is."""
    sys.path.insert(0, HERE)
    import tracker_common
    sys.path.insert(0, os.path.dirname(tracker_common.find("tools/svgpath.py")))
    import svgpath
    raw = svgpath.flatten(svgpath.path_d(open(MARK_SVG).read()), per_curve=40)
    cum = [0.0]
    for a, b in zip(raw, raw[1:]):
        cum.append(cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = cum[-1]

    def at(dist):
        dist %= total
        lo, hi = 0, len(cum) - 1
        while hi - lo > 1:
            m = (lo + hi) // 2
            if cum[m] <= dist: lo = m
            else: hi = m
        u = (dist - cum[lo]) / ((cum[hi] - cum[lo]) or 1.0)
        return (raw[lo][0] + (raw[hi][0] - raw[lo][0]) * u, raw[lo][1] + (raw[hi][1] - raw[lo][1]) * u)

    res = [at(total * i / MARK_N) for i in range(MARK_N)]
    xs = [p[0] for p in res]; ys = [p[1] for p in res]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    half = max(max(xs) - min(xs), max(ys) - min(ys)) / 2
    pts = [((x - cx) / half, -(y - cy) / half) for x, y in res]            # unit, y up

    def area(p):
        return sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p))) / 2

    old, _, _ = trace_points(TRACE)
    if (area(pts) < 0) != (area(old) < 0):                                # the traced direction round the lap
        pts.reverse()
    o0 = (old[0][0] / MARK_SCALE, old[0][1] / MARK_SCALE)                  # where the traced start/finish line is
    k = min(range(MARK_N), key=lambda i: (pts[i][0] - o0[0]) ** 2 + (pts[i][1] - o0[1]) ** 2)
    pts = pts[k:] + pts[:k]
    ip = [(round(x * MARK_SCALE), round(y * MARK_SCALE)) for x, y in pts]
    return ip, max(abs(x) for x, _ in ip), max(abs(y) for _, y in ip)


def layout(size, pad=0.16):
    ps, hw, hh = mark_points()
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
    ink = WHITE + ((255,) if transparent else ())
    d.line(p + [p[0]], fill=ink, width=w, joint="curve")
    for x, y in p:                       # a round pen: no notch where two segments meet
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=ink)
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
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 40">'
            '<circle cx="20" cy="20" r="19" fill="#121C33"/>'
            f'<path d="{pathd}" fill="none" stroke="#F2F5FC" stroke-width="2.5" '
            'stroke-linejoin="round" stroke-linecap="round"/>'
            f'<path d="{tick}" stroke="#FF8A1F" stroke-width="2.9" stroke-linecap="butt"/>'
            '</svg>')


WORDMARK_H = 36        # px; the title font is 34 px


def wordmark():
    """The F1 logo, as the title's first word on the boot and About pages: reference/logo/
    f1-formula-1.svg (downloaded from logowik.com; a registered trademark of Formula One
    Licensing B.V., used here by the project owner's choice - decision 44), cropped to its
    ink and scaled to WORDMARK_H on black, the page colour. Needs rsvg-convert."""
    src = os.path.join(OUT, "f1-formula-1.svg")
    big = os.path.join("/tmp", "f1-formula-1-big.png")
    try:
        subprocess.run(["rsvg-convert", "-z", "4", "-b", "black", "-o", big, src], check=True)
    except (OSError, subprocess.CalledProcessError):
        print("  f1-wordmark.png        skipped (rsvg-convert not found)")
        return
    im = Image.open(big).convert("RGB")
    im = im.crop(im.getbbox())
    w = round(im.width * WORDMARK_H / im.height)
    im.resize((w, WORDMARK_H), Image.LANCZOS).save(os.path.join(OUT, "f1-wordmark.png"))
    print(f"  f1-wordmark.png        {w}x{WORDMARK_H}")


def write_logomark():
    ps, hw, hh = mark_points()
    path = os.path.join(ROOT, "f1-tracker", "f1_logomark.h")
    rows = ",\n    ".join(", ".join(f"{{{x}, {y}}}" for x, y in ps[i:i + 6]) for i in range(0, len(ps), 6))
    with open(path, "w") as f:
        f.write("#pragma once\n// GENERATED by tools/gen_logo.py from reference/monza-smoothed.svg - do not edit.\n"
                "//\n// The logo's outline: the hand-smoothed Monza, MARK_N even steps round the lap in the\n"
                "// traced circuit's direction, from the start/finish line, int16 units (y up, centred,\n"
                "// larger half-extent 10000). The cars on the boot and About pages follow these points.\n"
                "#include <cstdint>\n\nnamespace f1 {\nnamespace logomark {\n\n"
                f"inline constexpr int N = {len(ps)};\n"
                f"inline constexpr int HALF_W = {hw}, HALF_H = {hh};\n"
                f"inline constexpr int16_t PTS[N][2] = {{\n    {rows}}};\n\n"
                "}  // namespace logomark\n}  // namespace f1\n")
    print(f"  f1_logomark.h          {len(ps)} points")


def main():
    os.makedirs(OUT, exist_ok=True)
    write_logomark()
    wordmark()
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
