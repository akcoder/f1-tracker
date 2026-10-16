#!/usr/bin/env python3
"""Generate art/toucan/*.svg (the vector art) and f1-tracker/f1_toucan_data.h: the toucan's seven poses
(facing right) as LVGL RGB565A8 images (an RGB565 plane then an 8-bit alpha plane, straight alpha),
antialiased. The device mirrors them in memory when the toucan flies left. The speech bubble's pointers
are the dinosaur's (gen_dino.py)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import toucan_art as art
import critter_common as cc

OUT = os.path.join(HERE, "..", "f1-tracker", "f1_toucan_data.h")


def main():
    d = os.path.join(HERE, "..", "art", "toucan")
    os.makedirs(d, exist_ok=True)
    for p in range(art.N_POSES):
        open(os.path.join(d, f"pose-{p}.svg"), "w").write(art.svg(p) + "\n")
    lines = cc.write_header(OUT, [
        "The toucan: seven poses (four wing beats, perched, beak half open, beak open), facing right,",
        f"{art.SW}x{art.SH}, as LVGL RGB565A8 images (an RGB565 plane, then an 8-bit alpha plane), antialiased.",
        "A generic toucan: no F1, team or series mark."], "toucan",
        f"inline constexpr int DATA_W = {art.SW}, DATA_H = {art.SH};", art)
    lines += ["}  // namespace toucan", "}  // namespace f1"]
    open(OUT, "w").write("\n".join(lines) + "\n")
    print(f"wrote {os.path.relpath(OUT)}  {os.path.getsize(OUT) // 1024} KB source")


if __name__ == "__main__":
    main()
