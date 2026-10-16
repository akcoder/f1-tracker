#!/usr/bin/env python3
"""The toucan: its art (smooth, antialiased) and a Python copy of the flight's timing, so the render
tool draws exactly what the device does. gen_toucan.py turns the art into the firmware's data header;
the timing is f1_toucanlogic.h's twin (the host tests pin both to the same numbers).
"""
import math
from PIL import Image
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from critter_common import Rng, next_gap_ms, GAP_MIN_MS, GAP_MAX_MS, raster as _raster

BLACK = (0x2A, 0x2F, 0x45); WING = (0x4A, 0x52, 0x75); BIB = (0xFF, 0xE9, 0x7A); WHITE = (0xF6, 0xF3, 0xE6)
BEAK = (0xF5, 0xA3, 0x1A); BEAK_TOP = (0xFF, 0xD2, 0x3F); BEAK_LO = (0xE0, 0x7B, 0x16); TIP = (0x2A, 0x1A, 0x10)
RED = (0xD8, 0x32, 0x1E); EYE_RING = (0x3F, 0xA9, 0xE0); EYE = (0x10, 0x14, 0x20)
FOOT = (0x6C, 0x8F, 0xB5); BRANCH = (0x7A, 0x52, 0x2E); LEAF = (0x4E, 0xA8, 0x44)
GW, GH = 36, 28
SC = 5
SW, SH = GW * SC, GH * SC         # 144 x 112
SCREEN_W = 480

# ---- poses: 0..3 fly (wings up, mid, down, mid), 4 perched (beak shut), 5 beak half open, 6 beak open
FLY0, PERCH, BEAK_HALF, BEAK_OPEN, N_POSES = 0, 4, 5, 6, 7


def _hx(c):
    return "#%02X%02X%02X" % c


def _f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


def _beak(x, y, jaw):
    """The beak from its root at (x, y): upper mandible, then the lower one hinged below it by `jaw` degrees."""
    o = []
    o.append(f'<g transform="rotate({_f(jaw)} {_f(x)} {_f(y + 3.2)})">'
             f'<path d="M{_f(x)} {_f(y + 2.6)} L{_f(x + 10)} {_f(y + 3)} Q{_f(x + 11.6)} {_f(y + 3.4)} {_f(x + 10.6)} {_f(y + 4.6)} '
             f'Q{_f(x + 6)} {_f(y + 6.4)} {_f(x)} {_f(y + 5.6)}Z" fill="{_hx(BEAK_LO)}"/></g>')           # lower
    o.append(f'<path d="M{_f(x)} {_f(y - 1.6)} C{_f(x + 5)} {_f(y - 2.6)} {_f(x + 10)} {_f(y - 1.4)} {_f(x + 11.4)} {_f(y + 2.4)} '
             f'Q{_f(x + 11.6)} {_f(y + 3.2)} {_f(x + 10.8)} {_f(y + 3.1)} L{_f(x)} {_f(y + 2.8)}Z" fill="{_hx(BEAK)}"/>')  # upper
    o.append(f'<path d="M{_f(x + 0.4)} {_f(y - 1.4)} C{_f(x + 5)} {_f(y - 2.3)} {_f(x + 9)} {_f(y - 1.4)} {_f(x + 10.6)} {_f(y + 0.8)}" '
             f'fill="none" stroke="{_hx(BEAK_TOP)}" stroke-width="1" stroke-linecap="round"/>')               # ridge
    o.append(f'<path d="M{_f(x + 9.4)} {_f(y - 0.4)} Q{_f(x + 11.7)} {_f(y + 0.6)} {_f(x + 11.2)} {_f(y + 2.6)} '
             f'L{_f(x + 10.4)} {_f(y + 3.1)}Z" fill="{_hx(TIP)}"/>')                                         # dark tip
    o.append(f'<path d="M{_f(x)} {_f(y - 1.6)} L{_f(x)} {_f(y + 3.4)}" stroke="{_hx(RED)}" stroke-width="0.9"/>')  # red root
    return "".join(o)


def svg(pose):
    """The toucan facing right as vector art: an SVG on a 36 x 28 grid (art/toucan/pose-N.svg)."""
    flying = pose < 4
    beak = {4: 0.0, 5: 9.0, 6: 20.0}.get(pose, 0.0)
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {GW} {GH}" width="{SW}" height="{SH}">']
    if flying:
        up = (0, 1, 2, 1)[pose]                 # 0 wings up, 1 mid, 2 down
        bob = (0.6, 0.0, -0.8, 0.0)[pose]       # the body rides up on the down stroke
        ang = (-38, 30, 150, 30)[pose]          # wing angle, clockwise from straight up
        by = 12.5 + bob
        # far wing, a little behind, darker
        o.append(f'<g transform="translate(14 {_f(by - 1.5)}) rotate({ang - 12})"><path d="M0 0 C-3 -3 -3.4 -9 -0.6 -13 C2.4 -9 3 -3 0 0Z" fill="{_hx(WING)}" opacity="0.6"/></g>')
        o.append(f'<path d="M3 {_f(by + 3.2)} L11 {_f(by + 0.2)} L12 {_f(by + 4.6)} Z" fill="{_hx(BLACK)}"/>')           # tail
        o.append(f'<path d="M2.4 {_f(by + 4.4)} L4 {_f(by + 2.6)} L6 {_f(by + 5.4)}Z" fill="{_hx(RED)}"/>')             # red under tail
        o.append(f'<ellipse cx="15" cy="{_f(by)}" rx="8.4" ry="4.6" fill="{_hx(BLACK)}" transform="rotate(8 15 {_f(by)})"/>')  # body
        o.append(f'<ellipse cx="21.5" cy="{_f(by + 1.6)}" rx="3.4" ry="2.8" fill="{_hx(BIB)}"/>')                         # bib
        o.append(f'<circle cx="23" cy="{_f(by - 2.4)}" r="3.6" fill="{_hx(BLACK)}"/>')                                     # head
        o.append(f'<path d="M20.6 {_f(by - 0.8)} Q23.4 {_f(by + 1.2)} 26 {_f(by - 0.6)} L25.6 {_f(by + 0.6)} Q23 {_f(by + 2.4)} 20.8 {_f(by + 0.8)}Z" fill="{_hx(WHITE)}"/>')
        o.append(f'<circle cx="24.4" cy="{_f(by - 3.2)}" r="1.5" fill="{_hx(EYE_RING)}"/><circle cx="24.6" cy="{_f(by - 3.2)}" r="0.7" fill="{_hx(EYE)}"/>')
        o.append(_beak(25.8, by - 2.2, 0.0))
        o.append(f'<path d="M7 {_f(by + 3.6)} l-3 1.4 M8.4 {_f(by + 3.8)} l-3 2" stroke="{_hx(FOOT)}" stroke-width="1.1" stroke-linecap="round"/>')  # feet trail
        o.append(f'<g transform="translate(14 {_f(by - 1.5)}) rotate({ang})"><path d="M0 0 C-3.6 -3 -4 -10 -0.6 -14 C3 -10 3.6 -3 0 0Z" fill="{_hx(WING)}"/>'
                 f'<path d="M0 -2 C-1.6 -5 -1.6 -9 -0.4 -12" fill="none" stroke="{_hx(BLACK)}" stroke-width="0.8" stroke-linecap="round"/></g>')
    else:
        # perched upright on a branch, tail down behind
        o.append(f'<path d="M1.5 24.4 Q18 22.6 34.5 24" fill="none" stroke="{_hx(BRANCH)}" stroke-width="2.6" stroke-linecap="round"/>')
        o.append(f'<ellipse cx="6" cy="22.4" rx="2.6" ry="1.2" fill="{_hx(LEAF)}" transform="rotate(-30 6 22.4)"/>')
        o.append(f'<ellipse cx="31" cy="22.8" rx="2.4" ry="1.1" fill="{_hx(LEAF)}" transform="rotate(25 31 22.8)"/>')
        o.append(f'<path d="M8.4 14 L13.4 19 L8.4 23 L5.4 22 Z" fill="{_hx(BLACK)}"/>')                                  # tail
        o.append(f'<path d="M5.8 22 L8.6 20.6 L9 23.2Z" fill="{_hx(RED)}"/>')
        o.append(f'<ellipse cx="14.5" cy="15.5" rx="5.6" ry="7.6" fill="{_hx(BLACK)}" transform="rotate(-14 14.5 15.5)"/>')  # body
        o.append(f'<ellipse cx="18.4" cy="15" rx="3" ry="4.4" fill="{_hx(BIB)}"/>')                                        # bib
        o.append(f'<path d="M10.4 11 C9 15 10.4 19.6 13.6 21.4 L14.4 19.8 C12.6 17.4 12.4 13.6 13.6 10.4Z" fill="{_hx(WING)}"/>')  # folded wing
        o.append(f'<path d="M15.6 21.4 l0.6 2.4 M17.6 21.4 l0.4 2.4" stroke="{_hx(FOOT)}" stroke-width="1.3" stroke-linecap="round"/>')
        o.append(f'<circle cx="19.2" cy="8.2" r="4.2" fill="{_hx(BLACK)}"/>')                                              # head
        o.append(f'<path d="M16.6 10.4 Q19.6 13 22.8 10.6 L22.2 11.8 Q19.6 14.2 16.8 11.8Z" fill="{_hx(WHITE)}"/>')
        o.append(f'<circle cx="20.8" cy="7.2" r="1.6" fill="{_hx(EYE_RING)}"/><circle cx="21" cy="7.2" r="0.75" fill="{_hx(EYE)}"/>')
        o.append(_beak(22.6, 6.6, beak))
    o.append('</svg>')
    return "".join(o)


def sprite(pose):
    """The toucan facing right, rasterised from its vector art (antialiased), RGBA, SW x SH."""
    return _raster(svg(pose), SW, SH)


# ---- the flight's timing: f1_toucanlogic.h's twin
WORDS = ["Just follow your nose!", "Squawk!", "Toucan do it!", "Got any fruit?", "Nice beak, huh?", "Rrrk! Rrrk!",
         "Just follow your nose!", "Fruit salad time!"]
LANE_MIN, LANE_MAX = 70, 320
STOP_MIN, STOP_MAX = 150, 300      # the whole bird stays on screen when it perches, flying either way
BASE_SPEED = 110.0
STEP_PX = 6.0                      # one of the four wing poses per this much distance: ~18 a second at 110 px/s
WAVE_PX, WAVE_AMP, WAVE_CALM = 150.0, 11.0, 70.0   # the swoops of its flight, and how near the perch they die away
BEAK_DELAY_MS, BEAK_STEP_MS = 300, 220


class Flight:
    """speed(t) = base * (1 + amp cos(2 pi t / period + phase)): always forward, always changing; the wings follow
    the distance. It swoops up and down on the way and settles to its perch at the stop."""
    def __init__(self, seed):
        r = Rng(seed)
        self.flip = r.next() < 0.5
        self.lane = LANE_MIN + int(r.next() * (LANE_MAX - LANE_MIN + 1))
        self.speed = BASE_SPEED * (0.7 + 0.6 * r.next())
        self.stop_x = STOP_MIN + int(r.next() * (STOP_MAX - STOP_MIN + 1))
        self.pause_ms = 2200 + int(r.next() * 1000)
        self.word = int(r.next() * len(WORDS)) % len(WORDS)
        self.amp = 0.25 + 0.25 * r.next()
        self.period = 1200.0 + 1800.0 * r.next()
        self.phase = 2 * math.pi * r.next()
        self._finish()

    def dist(self, tw_ms):
        w = 2 * math.pi / self.period
        return self.speed * (tw_ms / 1000.0 + self.amp / w * (math.sin(w * tw_ms + self.phase) - math.sin(self.phase)) / 1000.0)

    def _time_for(self, px):
        t = 0
        while self.dist(t) < px:
            t += 1
        return t

    def _finish(self):
        self.t_stop = self._time_for(self.stop_x + SW)
        self.t_end = self.t_stop + self.pause_ms + (self._time_for(SCREEN_W + SW) - self.t_stop)

    def swoop(self, d):
        """vertical offset at distance d: a sine that settles to nothing at the perch"""
        near = abs(d - (self.stop_x + SW)) / WAVE_CALM
        k = 1.0 if near >= 1 else near * near * (3 - 2 * near)
        return WAVE_AMP * math.sin(2 * math.pi * d / WAVE_PX) * k

    def at(self, t_ms):
        """-> (x left on screen, y top, pose, bubble shown)"""
        perched = False
        if t_ms < self.t_stop:
            tw = t_ms; bubble = False; pp = None
        elif t_ms < self.t_stop + self.pause_ms:
            tw = self.t_stop; u = t_ms - self.t_stop; perched = True
            bubble = u >= BEAK_DELAY_MS
            pp = PERCH if not bubble else (PERCH, BEAK_HALF, BEAK_OPEN, BEAK_HALF)[((u - BEAK_DELAY_MS) // BEAK_STEP_MS) % 4]
        else:
            tw = t_ms - self.pause_ms; bubble = False; pp = None
        d = self.dist(tw)
        pose = pp if pp is not None else int(d / STEP_PX) % 4
        x = -SW + d
        if self.flip:
            x = SCREEN_W - x - SW
        y = self.lane + int(math.floor(self.swoop(d) + 0.5))
        return int(math.floor(x + 0.5)), y, pose, bubble


if __name__ == "__main__":
    w = Flight(7)
    print("seed 7:", w.flip, w.lane, round(w.speed, 3), w.stop_x, w.pause_ms, w.word, round(w.amp, 4), round(w.period, 1), round(w.phase, 4), w.t_stop, w.t_end)
    for t in (0, 1000, 2500, w.t_stop, w.t_stop + 400, w.t_stop + 1000, w.t_end - 1):
        print(t, w.at(t))
    print("gap", next_gap_ms(7))
