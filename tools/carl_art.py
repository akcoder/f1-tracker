#!/usr/bin/env python3
"""Carl (MOTION-8), the llama in the green hat from Llamas with Hats: seven poses facing right on a 48 x 32 grid,
drawn as vector art (art/carl/pose-N.svg) and rasterised antialiased. He is a tall, slim grey llama on four
long legs with white socks, a long neck held high, and a pale green striped stocking cap with a pom-pom. His walk is
the dinosaur's timing (f1_dinologic.h), so the canvas is the dinosaur's width and his muzzle is at its right edge.
gen_carl.py turns this into the firmware's data header.
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from critter_common import raster as _raster

COAT = (0xC6, 0xC9, 0xCF); SHADE = (0xA4, 0xA7, 0xB0); BELLY = (0xD9, 0xDB, 0xE0); LINE = (0x6A, 0x6C, 0x76)
SOCK = (0xF3, 0xF3, 0xF1); HOOF = (0x5A, 0x56, 0x52)
HAT = (0xA2, 0xD9, 0x6A); HAT_D = (0x72, 0xB4, 0x48); NOSE = (0x8E, 0x88, 0x8C)
EYE = (0x16, 0x16, 0x1C); MOUTH = (0x5A, 0x14, 0x20); TOOTH = (0xFA, 0xF6, 0xE8)
SC = 3
GW, GH = 48, 40
SW, SH = GW * SC, GH * SC           # 144 x 120: the dinosaur's width (its walk's timing), taller for the neck
WALK0, STAND, TALK_HALF, TALK_OPEN, N_POSES = 0, 4, 5, 6, 7


def svg(pose):
    """Carl facing right (art/carl/pose-N.svg). 0-3: the walk (a four-beat gait); 4: standing, mouth shut; 5, 6: mouth half, fully open."""
    walking = pose < 4
    talk = {4: 0.0, 5: 0.5, 6: 1.0}.get(pose, 0.0)
    bob = [0, -0.5, 0, -0.5][pose] if walking else 0.0
    ph = pose / 4 * 2 * math.pi if walking else 0.0
    hx = lambda c: "#%02X%02X%02X" % c
    f = lambda v: f"{v:.2f}".rstrip("0").rstrip(".")
    a = talk * 0.32
    DY = 7.0                                   # the body rides on long legs, the neck above it

    def leg(x0, phase, col, front):
        """Hip at (x0, 18.2) (the body's own coordinates, lowered by DY). The leg is filled from the hip, so it grows out of the
        body; its outline starts only where it leaves the body's underside. A white sock on the shin, a hoof."""
        sn = math.sin(ph + phase) if walking else 0.0
        cs = math.cos(ph + phase) if walking else 1.0
        sw = 3.0 * sn
        lift = max(0.0, -cs) * 1.6 if walking else 0.0
        hip = (x0, 18.2 + bob)
        knee = (x0 + sw * 0.35 + (0.5 if front else -0.9), 24.8 + bob)
        foot = (x0 + sw, 31.2 - lift)
        mid = (knee[0] + (foot[0] - knee[0]) * 0.45, knee[1] + (foot[1] - knee[1]) * 0.45)
        t = (20.4 - hip[1]) / (knee[1] - hip[1])                                   # where the underside of the body is
        top = (hip[0] + (knee[0] - hip[0]) * t, 20.4)
        fill = f'M{f(hip[0])} {f(hip[1])} L{f(knee[0])} {f(knee[1])} L{f(mid[0])} {f(mid[1])}'
        edge = f'M{f(top[0])} {f(top[1])} L{f(knee[0])} {f(knee[1])} L{f(mid[0])} {f(mid[1])}'
        sock = f'M{f(knee[0] + (foot[0] - knee[0]) * 0.4)} {f(knee[1] + (foot[1] - knee[1]) * 0.4)} L{f(foot[0])} {f(foot[1] - 0.8)}'
        return (f'<path d="{edge}" fill="none" stroke="{hx(LINE)}" stroke-width="3.0" stroke-linecap="butt" stroke-linejoin="round"/>'
                f'<path d="{fill}" fill="none" stroke="{hx(col)}" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>'
                f'<path d="{sock}" fill="none" stroke="{hx(LINE)}" stroke-width="2.4" stroke-linecap="round"/>'
                f'<path d="{sock}" fill="none" stroke="{hx(SOCK)}" stroke-width="1.8" stroke-linecap="round"/>'
                f'<ellipse cx="{f(foot[0] + 0.2)}" cy="{f(foot[1] + 0.2)}" rx="1.5" ry="0.65" fill="{hx(HOOF)}"/>')

    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {GW} {GH}" width="{SW}" height="{SH}">',
         '<g transform="translate(5.3 7.0) scale(0.86)">']                         # his muzzle at the canvas's right edge
    o.append(f'<g transform="translate(0 {DY})">')
    o.append(leg(15.2, math.pi, SHADE, False))                                     # far legs, darker, behind
    o.append(leg(29.4, 0.0, SHADE, True))
    o.append(f'<circle cx="11.2" cy="{f(13.6 + bob)}" r="1.6" fill="{hx(COAT)}" stroke="{hx(LINE)}" stroke-width="0.35"/>')   # the tail
    o.append('</g>')
    # body and neck are ONE shape: the back runs level, then sweeps up into the neck's back, round the head, down the front of the
    # neck into the chest, along the belly and up the rump. No seam anywhere.
    b = lambda y: f(y + bob)
    o.append(f'<path d="M11.2 {b(24.4)} C11.2 {b(21.4)} 12 {b(19.8)} 14.2 {b(19.4)} C19 {b(18.6)} 24 {b(18.8)} 27.6 {b(19.4)} '
             f'C30.2 {b(16)} 31.6 {b(11.4)} 32.6 {b(9.6)} C33.4 {b(7.8)} 34.3 {b(5.4)} 35.4 {b(3.4)} L39.4 {b(5.6)} '
             f'C38.8 {b(7.8)} 38.4 {b(9.6)} 38 {b(11.6)} C37.2 {b(15)} 36.4 {b(19.8)} 36 {b(23)} C36 {b(25.6)} 35.2 {b(27.2)} 33.4 {b(27.4)} '
             f'C27 {b(27.8)} 19 {b(27.8)} 12.8 {b(26.8)} C11.4 {b(25.8)} 11.2 {b(25)} 11.2 {b(24.4)}Z" '
             f'fill="{hx(COAT)}" stroke="{hx(LINE)}" stroke-width="0.4" stroke-linejoin="round"/>')
    o.append(f'<g transform="translate(0 {DY})">'
             f'<path d="M13.4 {b(18.6)} C18 {b(20.6)} 26 {b(20.4)} 31.6 {b(19.2)} C26 {b(21)} 18 {b(21)} 13.4 {b(18.6)}Z" fill="{hx(BELLY)}"/>'
             f'<path d="M13.6 {b(14.2)} C14.2 {b(17)} 15.6 {b(18.8)} 17.4 {b(19.4)}" stroke="{hx(SHADE)}" stroke-width="0.5" fill="none" stroke-linecap="round"/></g>')
    # the head: a long face with a domed skull, a straight nose and a small round muzzle with a chin. Shut, it is all one piece
    # with a flat mouth line; speaking, the lower jaw comes away and swings down on its hinge under the cheek.
    o.append('<g transform="translate(-4.44 -0.82) scale(1.12)">')                  # the head and what is on it, a bit larger: it is as long as the ears
    top = (f'M36.2 {b(7)} C35.6 {b(3.6)} 38 {b(1.6)} 40.6 {b(1.8)} C43.4 {b(2)} 45.8 {b(3.6)} 47.2 {b(5.2)} '
           f'C48 {b(6.1)} 47.9 {b(7.1)} 47.3 {b(7.6)}')
    if talk < 0.1:
        o.append(f'<path d="{top} C47.9 {b(8.6)} 47 {b(9.4)} 45.8 {b(9.5)} L43 {b(9.4)} C40.6 {b(9.2)} 37.6 {b(8.8)} 36.2 {b(7)}Z" '
                 f'fill="{hx(COAT)}" stroke="{hx(LINE)}" stroke-width="0.4" stroke-linejoin="round"/>')
        o.append(f'<path d="M44.2 {b(5.2)} C46.2 {b(4.8)} 47.8 {b(5.6)} 47.9 {b(7)} C47.9 {b(8.5)} 47 {b(9.3)} 45.8 {b(9.4)} L44.4 {b(9.3)} C43.4 {b(8.2)} 43.4 {b(6.4)} 44.2 {b(5.2)}Z" fill="{hx(BELLY)}"/>')
        o.append(f'<path d="M47.4 {b(6.4)} L47.2 {b(7.4)} C46 {b(7.9)} 44.8 {b(7.9)} 43.6 {b(7.7)}" fill="none" stroke="{hx(LINE)}" stroke-width="0.45" stroke-linecap="round" stroke-linejoin="round"/>')
    else:
        o.append(f'<path d="{top} L42.4 {b(7.8)} C40.2 {b(8.2)} 37.6 {b(8.2)} 36.2 {b(7)}Z" fill="{hx(COAT)}" stroke="{hx(LINE)}" stroke-width="0.4" stroke-linejoin="round"/>')
        o.append(f'<path d="M44.2 {b(5.2)} C46.2 {b(4.8)} 47.8 {b(5.6)} 47.9 {b(7)} L47.3 {b(7.6)} L44.4 {b(7.7)} C43.4 {b(6.8)} 43.4 {b(6)} 44.2 {b(5.2)}Z" fill="{hx(BELLY)}"/>')
        o.append(f'<path d="M41.6 {b(7.7)} L47.3 {b(7.6)} L{f(47.3 - 6 * (1 - math.cos(a)))} {b(7.7 + 6 * math.sin(a))}Z" fill="{hx(MOUTH)}"/>')
        o.append(f'<rect x="45.6" y="{b(7.7)}" width="1.1" height="0.8" rx="0.2" fill="{hx(TOOTH)}"/>')
        o.append(f'<g transform="rotate({f(a * 57.3)} 41.6 {b(7.7)})">'
                 f'<path d="M41.4 {b(7.6)} L47.3 {b(7.6)} C47.9 {b(8.6)} 47 {b(9.4)} 45.8 {b(9.5)} L43 {b(9.4)} C41.8 {b(9.2)} 41 {b(8.6)} 41.4 {b(7.6)}Z" '
                 f'fill="{hx(BELLY)}" stroke="{hx(LINE)}" stroke-width="0.4" stroke-linejoin="round"/>'
                 f'<rect x="45.6" y="{b(6.8)}" width="1.1" height="0.8" rx="0.2" fill="{hx(TOOTH)}"/></g>')
    o.append(f'<ellipse cx="47.1" cy="{b(5.9)}" rx="1" ry="0.8" fill="{hx(NOSE)}" transform="rotate(30 47.1 {b(5.9)})"/>')   # the nose pad
    o.append(f'<circle cx="46.8" cy="{b(5.7)}" r="0.24" fill="{hx(EYE)}"/><circle cx="47.5" cy="{b(6.2)}" r="0.24" fill="{hx(EYE)}"/>')   # the nostrils
    # the eye: a dark almond with a highlight, a heavy lid over it, a flat brow above: unimpressed
    o.append(f'<path d="M39.6 {b(4.9)} C40.6 {b(3.9)} 42.8 {b(3.9)} 43.8 {b(4.9)} C42.8 {b(5.9)} 40.6 {b(5.9)} 39.6 {b(4.9)}Z" fill="{hx(EYE)}"/>')
    o.append(f'<ellipse cx="40.5" cy="{b(5)}" rx="0.55" ry="0.45" fill="#EDEDE8"/>')                       # a sliver of white at the back of the eye
    o.append(f'<circle cx="42.3" cy="{b(4.7)}" r="0.22" fill="#FFFFFF"/>')
    o.append(f'<path d="M39.5 {b(4.8)} C40.8 {b(3.3)} 42.9 {b(3.4)} 44 {b(4.9)} L43.6 {b(4.5)} C42.4 {b(3.9)} 40.6 {b(3.9)} 39.8 {b(4.4)}Z" fill="{hx(COAT)}"/>')
    o.append(f'<path d="M39.5 {b(4.7)} C40.8 {b(3.5)} 42.9 {b(3.6)} 44 {b(4.9)}" fill="none" stroke="{hx(EYE)}" stroke-width="0.75" stroke-linecap="round"/>')   # the heavy lid
    o.append(f'<path d="M39.6 {b(3)} C41 {b(2.5)} 42.6 {b(2.6)} 43.6 {b(3.1)}" fill="none" stroke="{hx(LINE)}" stroke-width="0.45" stroke-linecap="round"/>')
    # the cap: a pale green stocking cap that stands up from the forehead as a tall cone, bends over at the top and droops
    # back, a pom-pom hanging from its tip; a darker band round the head and darker stripes across
    o.append(f'<path d="M42.6 {b(3.2)} C42.4 {b(0)} 41.4 {b(-2.6)} 40.2 {b(-4.4)} C39.6 {b(-5.4)} 38.6 {b(-6.2)} 37.2 {b(-6.2)} C35.4 {b(-6.2)} 33.6 {b(-5.2)} 32.6 {b(-3.4)} '
             f'L31.8 {b(-1.8)} L33.6 {b(-1.6)} C34.4 {b(-3)} 35.6 {b(-3.8)} 36.6 {b(-3.2)} C37 {b(-1.4)} 36.6 {b(2.2)} 36.6 {b(4.2)} C38.6 {b(3.9)} 40.8 {b(3.6)} 42.6 {b(3.2)}Z" '
             f'fill="{hx(HAT)}" stroke="{hx(HAT_D)}" stroke-width="0.4" stroke-linejoin="round"/>')
    for x0, y0, x1, y1 in ((42.2, 1.6, 37.0, 2.2), (41.6, -0.6, 36.9, -0.2), (40.8, -2.6, 36.8, -2.2), (39.6, -4.4, 36.9, -3.6), (34.4, -4.4, 34.6, -2.4)):
        o.append(f'<path d="M{x0} {b(y0)} L{x1} {b(y1)}" stroke="{hx(HAT_D)}" stroke-width="0.8" stroke-linecap="round"/>')
    o.append(f'<path d="M36.8 {b(4.2)} C38.8 {b(3.9)} 40.8 {b(3.6)} 42.6 {b(3.2)}" fill="none" stroke="{hx(HAT_D)}" stroke-width="1.3"/>')    # the band
    o.append(f'<circle cx="31.4" cy="{b(-0.6)}" r="1.4" fill="{hx(HAT)}" stroke="{hx(HAT_D)}" stroke-width="0.4"/>')                                  # the pom-pom
    # ears: tall and narrow, nearly parallel, at the top back of the head, in front of the cap's drooping tip
    o.append(f'<path d="M35.4 {b(4)} C34.8 {b(1.4)} 34.8 {b(-1.6)} 35.6 {b(-4.4)} C36.4 {b(-2)} 36.8 {b(1)} 36.8 {b(3.4)}Z" '
             f'fill="{hx(SHADE)}" stroke="{hx(LINE)}" stroke-width="0.35" stroke-linejoin="round"/>')
    o.append(f'<path d="M36.8 {b(3.8)} C36.4 {b(1)} 36.6 {b(-2)} 37.6 {b(-5)} C38.4 {b(-2.4)} 38.6 {b(0.6)} 38.4 {b(3)}Z" '
             f'fill="{hx(COAT)}" stroke="{hx(LINE)}" stroke-width="0.35" stroke-linejoin="round"/>')
    o.append('</g>')
    o.append(f'<g transform="translate(0 {DY})">')
    o.append(leg(16.6, 0.0 + math.pi * 0.5, COAT, False))                          # near hind leg
    o.append(leg(30.8, math.pi * 1.5, COAT, True))                                 # near fore leg
    o.append('</g></g></svg>')
    return "".join(o)


def sprite(pose):
    """Carl facing right, rasterised from his vector art (antialiased), RGBA, SW x SH."""
    return _raster(svg(pose), SW, SH)


if __name__ == "__main__":
    from PIL import Image
    sh = Image.new("RGBA", (SW * 4, SH * 2), (40, 44, 60, 255))
    for p in range(N_POSES):
        sh.alpha_composite(sprite(p), ((p % 4) * SW, (p // 4) * SH))
    sh.save(sys.argv[1] if len(sys.argv) > 1 else "carl.png")
