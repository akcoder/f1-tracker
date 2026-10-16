#!/usr/bin/env python3
"""Generate art/dino/*.svg (the vector art) and f1-tracker/f1_dino_data.h: the dinosaur's seven poses (facing right) and
the speech bubble's two pointers, as LVGL RGB565A8 images (an RGB565 plane then an 8-bit alpha plane,
straight alpha), antialiased. The device mirrors them in memory when the dinosaur walks left. The
bubble's pointers serve the toucan too."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dino_art as art
import critter_common as cc

OUT = os.path.join(HERE, "..", "f1-tracker", "f1_dino_data.h")


def main():
    d = os.path.join(HERE, "..", "art", "dino")
    os.makedirs(d, exist_ok=True)
    for p in range(art.N_POSES):
        open(os.path.join(d, f"pose-{p}.svg"), "w").write(art.svg(p) + "\n")
    for flip in (False, True):
        open(os.path.join(d, f"bubble-tail-{'left' if flip else 'right'}.svg"), "w").write(art.tail_svg(flip) + "\n")
    lines = cc.write_header(OUT, [
        "The walking dinosaur: seven poses (four steps, standing, mouth half open, mouth open), facing right,",
        f"{art.SW}x{art.SH}, and the speech bubble's two pointers, as LVGL RGB565A8 images (an RGB565 plane, then an",
        "8-bit alpha plane), antialiased. A generic green dinosaur: no F1, team or series mark."], "dino",
        f"inline constexpr int DATA_W = {art.SW}, DATA_H = {art.SH}, TAIL_W = 22, TAIL_H = 12;", art)
    for flip in (False, True):
        lines += [cc.arr("TAIL_" + ("L" if flip else "R"), cc.planes(art.tail_image(flip)), 22), ""]
    lines += ["}  // namespace dino", "}  // namespace f1"]
    open(OUT, "w").write("\n".join(lines) + "\n")
    print(f"wrote {os.path.relpath(OUT)}  {os.path.getsize(OUT) // 1024} KB source")


if __name__ == "__main__":
    main()
