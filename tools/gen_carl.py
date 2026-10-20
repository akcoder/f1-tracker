#!/usr/bin/env python3
"""Generate art/carl/*.svg (the vector art) and f1-tracker/f1_carl_data.h: Carl's seven poses (facing right,
the dinosaur's width, taller) as LVGL RGB565A8 images (an RGB565 plane then an 8-bit straight-alpha plane),
antialiased. The device mirrors them in memory when he walks left. His speech bubble's pointers are the
dinosaur's."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import carl_art as art
import critter_common as cc

OUT = os.path.join(HERE, "..", "f1-tracker", "f1_carl_data.h")


def main():
    d = os.path.join(HERE, "..", "art", "carl")
    os.makedirs(d, exist_ok=True)
    for p in range(art.N_POSES):
        open(os.path.join(d, f"pose-{p}.svg"), "w").write(art.svg(p) + "\n")
    lines = cc.write_header(OUT, [
        "Carl, the grey llama in the green stocking cap (Llamas with Hats), on four legs: seven poses (four steps, standing,",
        f"mouth half open, mouth open), facing right, {art.SW}x{art.SH}, as LVGL RGB565A8 images (an RGB565 plane, then an 8-bit alpha",
        "plane), antialiased. Drawn from scratch."], "carl",
        f"inline constexpr int DATA_W = {art.SW}, DATA_H = {art.SH};", art)
    lines += ["}  // namespace carl", "}  // namespace f1"]
    open(OUT, "w").write("\n".join(lines) + "\n")
    print(f"wrote {os.path.relpath(OUT)}  {os.path.getsize(OUT) // 1024} KB source")


if __name__ == "__main__":
    main()
