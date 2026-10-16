#!/usr/bin/env python3
"""The walking dinosaur: its art (smooth, antialiased) and a Python copy of the walk's timing, so a
project's render tool draws exactly what the device does. gen_dino.py turns the art into the
firmware's data header; the timing is f1_dinologic.h's twin (the host tests pin both to the
same numbers).
"""
import math
from PIL import Image
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from critter_common import Rng, next_gap_ms, GAP_MIN_MS, GAP_MAX_MS, raster as _raster

BODY = (0x5D, 0xB7, 0x4B); DARK = (0x3E, 0x8A, 0x35); BELLY = (0xA8, 0xD8, 0x8A)
EYE = (0x10, 0x14, 0x20); SPOT = (0x4A, 0xA0, 0x3E); MOUTH = (0x7A, 0x2E, 0x3A)
SS, SC = 8, 3                     # supersample; final pixels per grid pixel
GW, GH = 48, 32
SW, SH = GW * SC, GH * SC         # 144 x 96
SCREEN_W = 480

# ---- poses: 0..3 walk, 4 standing (mouth shut), 5 half open, 6 open
WALK0, STAND, ROAR_HALF, ROAR_OPEN, N_POSES = 0, 4, 5, 6, 7


def svg(pose):
    """The dinosaur facing right as vector art: an SVG on a 48 x 32 grid (art/dino/pose-N.svg)."""
    walking = pose < 4
    roar = {4: 0.0, 5: 0.5, 6: 1.0}.get(pose, 0.0)
    bob = [0, -1, 0, -1][pose] * 0.8 if walking else 0.0
    ph = pose / 4 * 2 * math.pi if walking else 0.0
    hx = lambda c: "#%02X%02X%02X" % c
    f = lambda v: f"{v:.2f}".rstrip("0").rstrip(".")

    def leg(x0, phase, col):
        sn = math.sin(ph + phase) if walking else 0.0
        cs = math.cos(ph + phase) if walking else 1.0
        hip = (x0, 19 + bob); knee = (x0 + 3 * sn, 24); foot = (x0 + 5 * sn, 29 if cs > -0.3 else 27)
        return (f'<path d="M{f(hip[0])} {f(hip[1])} L{f(knee[0])} {f(knee[1])} L{f(foot[0])} {f(foot[1])}" '
                f'fill="none" stroke="{hx(col)}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>'
                f'<rect x="{f(foot[0] - 1)}" y="{f(foot[1] - 0.2)}" width="4.4" height="2.2" rx="1.1" fill="{hx(col)}"/>')

    tw = 2 * math.sin(ph) if walking else 0.0
    lift = -1.2 * roar
    jaw = roar * 3.2
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {GW} {GH}" width="{SW}" height="{SH}">']
    o.append(leg(21, math.pi, DARK))                                                       # far hind leg
    o.append(f'<path d="M0 {f(13 + bob + tw)} C6 {f(11.5 + bob + tw * 0.5)} 11 {f(10 + bob)} 16 {f(10 + bob)} '
             f'L22 {f(12 + bob)} L22 {f(20 + bob)} C18 {f(19.5 + bob)} 12 {f(17.5 + bob + tw * 0.3)} 0 {f(13 + bob + tw)}Z" '
             f'fill="{hx(BODY)}"/>')                                                      # tail
    o.append(f'<ellipse cx="25" cy="{f(17 + bob)}" rx="11" ry="8" fill="{hx(BODY)}"/>')    # body
    o.append(f'<ellipse cx="26" cy="{f(21.6 + bob)}" rx="8" ry="4.3" fill="{hx(BELLY)}"/>')  # belly
    for sx, sy in ((20.8, 12.8), (25.8, 11.8), (30.8, 13.8)):
        o.append(f'<circle cx="{sx}" cy="{f(sy + bob)}" r="0.85" fill="{hx(SPOT)}"/>')
    o.append(f'<path d="M30.5 {f(14 + bob)} C32.5 {f(10 + bob)} 33.5 {f(7.5 + bob + lift)} 35 {f(6 + bob + lift)} '
             f'L40 {f(6 + bob + lift)} C40.5 {f(10 + bob)} 39.5 {f(14 + bob)} 38.5 {f(16.5 + bob)}Z" fill="{hx(BODY)}"/>')  # neck
    o.append(f'<rect x="34" y="{f(2 + bob + lift)}" width="13" height="8.2" rx="2.6" fill="{hx(BODY)}"/>')               # head
    o.append(f'<path d="M40 {f(8.6 + bob + lift)} L47 {f(8.6 + bob + lift)} L47 {f(10.4 + bob + jaw)} '
             f'Q47 {f(11 + bob + jaw)} 46 {f(11 + bob + jaw)} L41 {f(11 + bob + jaw)} Q40 {f(11 + bob + jaw)} 40 {f(10.4 + bob + jaw)}Z" '
             f'fill="{hx(BELLY)}"/>')                                                                                    # jaw
    if roar > 0.15:
        o.append(f'<path d="M40.6 {f(8.8 + bob + lift)} L46.6 {f(8.8 + bob + lift)} L46.6 {f(9.4 + bob + jaw)} '
                 f'L40.6 {f(9.4 + bob + jaw)}Z" fill="{hx(MOUTH)}"/>')
    else:
        o.append(f'<path d="M41 {f(8.6 + bob + lift)} L46.6 {f(8.6 + bob + lift)}" stroke="{hx(DARK)}" stroke-width="0.7" stroke-linecap="round"/>')
    o.append(f'<circle cx="40.9" cy="{f(4.9 + bob + lift)}" r="0.95" fill="{hx(EYE)}"/>')
    o.append(f'<circle cx="45.4" cy="{f(4.6 + bob + lift)}" r="0.42" fill="{hx(DARK)}"/>')
    # the little arm swings a little in step with the walk, and lifts while it roars
    if walking:
        hand = (36.5 + 1.4 * math.sin(ph + math.pi / 2), 19.5 + bob + 0.9 * math.cos(ph + math.pi / 2) - 0.3)
    else:
        hand = (36.5 + 0.9 * roar, 19.5 + bob - 2.0 * roar)
    o.append(f'<path d="M34 {f(16 + bob)} L{f(hand[0])} {f(hand[1])}" stroke="{hx(DARK)}" stroke-width="2" stroke-linecap="round"/>')  # arm
    o.append(leg(26, 0, BODY))                                                             # near hind leg
    o.append('</svg>')
    return "".join(o)


def tail_svg(flip):
    """The speech bubble's pointer: tip down, toward the dinosaur's head (22 x 12)."""
    pts = "2,0 20,0 4,11" if not flip else "2,0 20,0 18,11"
    tip = (4, 11) if not flip else (18, 11)
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 22 12" width="22" height="12">'
            f'<polygon points="{pts}" fill="#F2F5FC"/>'
            f'<path d="M2 0.4 L{tip[0]} {tip[1]} L20 0.4" fill="none" stroke="#FF8A1F" stroke-width="1.6" stroke-linejoin="round"/>'
            '<rect x="3.2" y="0" width="15.6" height="1.8" fill="#F2F5FC"/></svg>')


def sprite(pose):
    """The dinosaur facing right, rasterised from its vector art (antialiased), RGBA, SW x SH."""
    return _raster(svg(pose), SW, SH)


def tail_image(flip):
    return _raster(tail_svg(flip), 22, 12)


# ---- the walk's timing: f1_dinologic.h's twin
WORDS = ["Rawr!", "RAWR!", "Grrr!", "Roarrr!", "Raaawr!", "Rawr rawr!", "Chomp!", "Grrrawr!", "Rrraaah!", "Rarrr!"]
LANE_MIN, LANE_MAX = 60, 340
STOP_MIN, STOP_MAX = 150, 330
STEP_PX = 9.75                     # one of the four walk poses per this much distance: 8 steps a second at 78 px/s
ROAR_DELAY_MS, ROAR_STEP_MS = 300, 250


class Walk:
    """speed(t) = base * (1 + amp cos(2 pi t / period + phase)): always forward, always changing, so it
    hurries and dawdles; the legs follow the distance, so they hurry and dawdle with it."""
    def __init__(self, seed):
        r = Rng(seed)
        self.flip = r.next() < 0.5
        self.lane = LANE_MIN + int(r.next() * (LANE_MAX - LANE_MIN + 1))
        self.speed = 78.0 * (0.7 + 0.6 * r.next())
        self.stop_x = STOP_MIN + int(r.next() * (STOP_MAX - STOP_MIN + 1))
        self.pause_ms = 2000 + int(r.next() * 1000)
        self.word = int(r.next() * len(WORDS)) % len(WORDS)
        self.amp = 0.25 + 0.25 * r.next()
        self.period = 1200.0 + 1800.0 * r.next()
        self.phase = 2 * math.pi * r.next()
        self._finish()

    def dist(self, tw_ms):
        """pixels walked after tw_ms of walking (the pause does not count)"""
        w = 2 * math.pi / self.period
        return self.speed * (tw_ms / 1000.0 + self.amp / w * (math.sin(w * tw_ms + self.phase) - math.sin(self.phase)) / 1000.0)

    def _time_for(self, px):
        t = 0
        while self.dist(t) < px:
            t += 1
        return t

    def _finish(self):
        self.t_walk = self._time_for(self.stop_x + SW)                       # walking time to the stop
        self.t_stop = self.t_walk
        self.t_tail = self._time_for(SCREEN_W + SW) - self.t_walk            # walking time after it
        self.t_end = self.t_stop + self.pause_ms + self.t_tail

    def at(self, t_ms):
        """-> (x left on screen, y top, pose, bubble shown)"""
        if t_ms < self.t_stop:
            tw = t_ms; bubble = False; roarpose = None
        elif t_ms < self.t_stop + self.pause_ms:
            tw = self.t_stop; u = t_ms - self.t_stop
            bubble = u >= ROAR_DELAY_MS
            roarpose = STAND if not bubble else (STAND, ROAR_HALF, ROAR_OPEN, ROAR_HALF)[((u - ROAR_DELAY_MS) // ROAR_STEP_MS) % 4]
        else:
            tw = t_ms - self.pause_ms; bubble = False; roarpose = None
        d = self.dist(tw)
        pose = roarpose if roarpose is not None else int(d / STEP_PX) % 4
        x = -SW + d
        if self.flip:
            x = SCREEN_W - x - SW
        return int(math.floor(x + 0.5)), self.lane, pose, bubble


if __name__ == "__main__":
    w = Walk(7)
    print("seed 7:", w.flip, w.lane, round(w.speed, 3), w.stop_x, w.pause_ms, w.word, round(w.amp, 4), round(w.period, 1), round(w.phase, 4), w.t_stop, w.t_end)
    for t in (0, 1000, 2500, w.t_stop, w.t_stop + 400, w.t_stop + 1000, w.t_end - 1):
        print(t, w.at(t))
    print("gap", next_gap_ms(7))
