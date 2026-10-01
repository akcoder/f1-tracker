#!/usr/bin/env python3
"""Emit the shared font glyph set (REQUIREMENTS.md decision 35, UI-40a/b).

ASCII printable + Latin-1 Supplement + Latin Extended-A, minus the two
invisible code points that break a YAML scalar (NBSP, SOFT HYPHEN).

Driver and circuit names guarantee accents - HULKENBERG, RAIKKONEN, Autodromo
Jose Carlos Pace, Nurburgring - so a reduced set draws boxes in the middle of
the content (decision 51).

  python3 tools/gen_glyphs.py            # print the YAML scalar
  python3 tools/gen_glyphs.py --report   # per-size flash estimate
"""
import argparse

SKIP = {0x00A0, 0x00AD}          # NBSP, SOFT HYPHEN - invisible, break YAML


def glyphs() -> str:
    cps = list(range(0x20, 0x7F)) + list(range(0xA0, 0x180))
    return "".join(chr(c) for c in cps if c not in SKIP)


def yaml_scalar(g: str) -> str:
    return "'" + g.replace("'", "''") + "'"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    g = glyphs()
    if a.report:
        print(f"glyphs: {len(g)}")
        total = 0
        for size, adv in [(12, 7), (14, 8), (16, 10), (18, 11), (24, 14)]:
            kb = len(g) * adv * size // 2 / 1024
            total += kb
            print(f"  mono{size}: ~{kb:5.1f} KB")
        print(f"  {'total':8} ~{total:5.1f} KB  (4 bpp, w*h/2 per glyph)")
    else:
        print(yaml_scalar(g))


if __name__ == "__main__":
    main()
