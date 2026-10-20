#!/usr/bin/env python3
"""docs/renders/carl-bite.png: Carl at a driver's face, with the bites already on it (MOTION-8).

The wounds are drawn by the firmware's own code (tests/wound_preview.cpp includes f1_wound.h), so this is
what the board paints, not a second drawing of it. The sprite is carl_art's.

  python3 tools/render_carl.py [driver_id] [seed]      # default: leclerc, 5
  python3 tools/render_carl.py --gif [driver_id] [seed]   # docs/renders/carl-bite.gif: the whole visit (tests/carl_frames.cpp)
"""
import os, subprocess, sys, tempfile
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import carl_art, dino_art, gen_faces
import critter_common as cc
from PIL import ImageFont

TC = os.environ.get("TC", os.path.join(ROOT, "..", "tracker-common", "components", "tracker_common"))


def gif(did, seed, fps=15):
    """The whole visit: in from the right, the bites as they land and the blood running, his word, out again."""
    import json
    face = json.load(open(os.path.join(ROOT, "art", "faces.json")))["faces"][did]
    pic = dict(gen_faces.portraits())[did]
    PX, PY = 24, 56
    font = ImageFont.truetype(os.environ.get("FONT", "/tmp/RobotoMono.ttf"), 16)
    with tempfile.TemporaryDirectory() as t:
        exe = os.path.join(t, "cf")
        subprocess.check_call(["c++", "-std=c++17", "-O1", "-I" + TC, "-o", exe, os.path.join(ROOT, "tests", "carl_frames.cpp")])
        out = subprocess.check_output([exe, os.path.join(t, "ov.bin")] + [str(v) for v in face] + [str(seed), str(fps), str(PX), str(PY)]).decode().split("\n")
        nframes = int(out[0].split()[0])
        n = 2 + (seed >> 5) % 2
        info = [tuple(int(v) for v in l.split()) for l in out[1 + n:1 + n + nframes]]
        raw = open(os.path.join(t, "ov.bin"), "rb").read()
    bg = Image.new("RGBA", (480, 480), (0x0B, 0x0E, 0x1A, 255))
    bg.paste(pic.convert("RGBA"), (PX, PY))
    poses = [carl_art.sprite(p).transpose(Image.FLIP_LEFT_RIGHT) for p in range(carl_art.N_POSES)]
    bub = cc.bubble("Nice face.", font)
    tail = dino_art.tail_image(True)
    frames = []
    for i, (x, y, pose, bubble) in enumerate(info):
        im = bg.copy()
        im.alpha_composite(Image.frombytes("RGBA", (240, 320), raw[i * 240 * 320 * 4:(i + 1) * 240 * 320 * 4]), (PX, PY))
        im.alpha_composite(poses[pose], (x, y)) if -carl_art.SW < x < 480 else None
        if bubble:
            bx, by = max(4, x - 6), y - bub.height - 12 + 4
            im.alpha_composite(bub, (bx, by))
            im.alpha_composite(tail, (bx + 14, by + bub.height - 2))
        frames.append(im.convert("RGB"))
    # one palette for every frame (a small file). Sampled from several frames and from Carl blown up, so his colours get entries
    # of their own: a palette taken from the photograph alone loses the hat's green
    sheet = Image.new("RGB", (480, 480 * 5))
    for n, k in enumerate((0.3, 0.45, 0.6, 0.72)):
        sheet.paste(frames[int(len(frames) * k)], (0, n * 480))
    big = Image.new("RGBA", (480, 480), (0, 0, 0, 255))
    for n, p in enumerate((4, 6)):
        sp = carl_art.sprite(p).transpose(Image.FLIP_LEFT_RIGHT).resize((carl_art.SW * 2, carl_art.SH * 2), Image.LANCZOS)
        big.alpha_composite(sp, (96, n * 230))
    sheet.paste(big.convert("RGB"), (0, 480 * 4))
    pal = sheet.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    path = os.path.join(ROOT, "docs", "renders", "carl-bite.gif")
    q[0].save(path, save_all=True, append_images=q[1:], duration=int(1000 / fps), loop=0, optimize=False, disposal=1)
    print(f"docs/renders/carl-bite.gif  {len(q)} frames, {os.path.getsize(path) // 1024} KB")


def main():
    args = [a for a in sys.argv[1:] if a != "--gif"]
    did = args[0] if len(args) > 0 else "leclerc"
    seed = int(args[1]) if len(args) > 1 else 5
    if "--gif" in sys.argv:
        return gif(did, seed)
    import json
    face = json.load(open(os.path.join(ROOT, "art", "faces.json")))["faces"][did]
    pic = dict(gen_faces.portraits())[did]
    with tempfile.TemporaryDirectory() as t:
        exe = os.path.join(t, "wp")
        subprocess.check_call(["c++", "-std=c++17", "-O1", "-I" + TC, "-o", exe, os.path.join(ROOT, "tests", "wound_preview.cpp")])
        open(os.path.join(t, "in.rgb"), "wb").write(pic.tobytes())
        out = subprocess.check_output([exe, os.path.join(t, "in.rgb"), os.path.join(t, "out.ppm")] + [str(v) for v in face] + [str(seed)]).decode()
        bitten = Image.open(os.path.join(t, "out.ppm")).convert("RGB")
    pts = [tuple(int(v) for v in l.split()) for l in out.strip().splitlines()]
    PX, PY = 24, 56                                           # where the card shows the portrait
    im = Image.new("RGBA", (480, 480), (0x0B, 0x0E, 0x1A, 255))
    im.paste(bitten, (PX, PY))
    # Carl, facing left, his mouth at the last bite (the head lunges 14 px in), and his word
    k = len(pts) - 1
    mx, my = PX + pts[k][0], PY + pts[k][1]
    spr = carl_art.sprite(carl_art.ROAR_OPEN if hasattr(carl_art, "ROAR_OPEN") else 6).transpose(Image.FLIP_LEFT_RIGHT)
    left, top = mx + 2 - 14, my - carl_art.SH * 34 // 100 + 6
    im.alpha_composite(spr, (left, top))
    font = ImageFont.truetype(os.environ.get("FONT", "/tmp/RobotoMono.ttf"), 16)
    bub = cc.bubble("Nice face.", font)                       # over his head, its pointer toward it (f1_critter.h)
    bx, by = left - 6, top - bub.height - 12 + 4
    im.alpha_composite(bub, (bx, by))
    im.alpha_composite(dino_art.tail_image(True), (bx + 14, by + bub.height - 2))
    im.convert("RGB").save(os.path.join(ROOT, "docs", "renders", "carl-bite.png"))
    print("docs/renders/carl-bite.png", did, pts)


if __name__ == "__main__":
    main()
