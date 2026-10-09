#!/usr/bin/env python3
"""Render the order page (REQUIREMENTS.md section 6.3) at 480x480 and measure the fit.

Answers open question 3: do 22 rows fit at mono12? Uses the real Roboto Mono
metrics, the real 2026 entry list and the real OpenF1 team colours, so the
measurement is of the actual layout rather than an estimate.

  python3 tools/mock_order_page.py --font /path/RobotoMono.ttf --out docs/renders
"""
import argparse, json, os, sys
from PIL import Image, ImageDraw, ImageFont

W = H = 480
BG      = (0x00, 0x00, 0x00)
ORANGE  = (0xFF, 0x8A, 0x1F)
TEXT    = (0xC9, 0xD3, 0xF2)
MUTED   = (0x7E, 0x8B, 0xB3)
BORDER  = (0x22, 0x30, 0x5A)
HILITE  = (0x1A, 0x25, 0x47)      # watched-driver row

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SAMP = os.path.join(ROOT, "reference", "samples")

TEAM_FALLBACK = {
    "Red Bull Racing": "4781D7", "McLaren": "F47600", "Ferrari": "ED1131",
    "Mercedes": "00E7B0", "Aston Martin": "229971", "Alpine": "00A1E8",
    "Williams": "1868DB", "Racing Bulls": "6C98FF", "Audi": "01823C",
    "Haas F1 Team": "9C9FA2", "Cadillac": "C8A96B",
}


def load_grid():
    """22 drivers from the real fixtures: surname, number, nationality, team."""
    dj = json.load(open(os.path.join(SAMP, "jolpica-2026-drivers.json")))
    jol = {d["driverId"]: d for d in dj["MRData"]["DriverTable"]["Drivers"]}
    of = json.load(open(os.path.join(SAMP, "openf1-drivers-11377.json")))
    pos = json.load(open(os.path.join(SAMP, "openf1-position-11377.json")))

    # earliest position row per driver == the grid (RACE-11)
    pos.sort(key=lambda r: r["date"])
    first = {}
    for r in pos:
        first.setdefault(r["driver_number"], r)
    grid = sorted(first.values(), key=lambda r: r["position"])

    by_num = {d["driver_number"]: d for d in of}
    dem = {"Dutch": "NLD", "British": "GBR", "Monegasque": "MCO", "Australian": "AUS",
           "Spanish": "ESP", "Italian": "ITA", "French": "FRA", "German": "DEU",
           "Mexican": "MEX", "Thai": "THA", "Brazilian": "BRA", "Argentine": "ARG",
           "Canadian": "CAN", "Japanese": "JPN", "Finnish": "FIN",
           "New Zealander": "NZL"}
    surname = {}
    for d in jol.values():
        if d.get("code"):
            surname[d["code"]] = (d["familyName"], dem.get(d.get("nationality"), None))

    rows = []
    for g in grid:
        o = by_num.get(g["driver_number"], {})
        ac = o.get("name_acronym", "???")
        sn, iso = surname.get(ac, (o.get("last_name") or ac, None))
        rows.append({
            "pos": g["position"], "num": g["driver_number"], "name": sn.upper(),
            "iso": iso, "team": o.get("team_name", "-"),
            "colour": o.get("team_colour") or TEAM_FALLBACK.get(o.get("team_name", ""), "888888"),
            "acr": ac,
        })
    return rows


def render(rows, font_path, with_team=True, row_h=18, label="a", watched="VER",
           big_state=False):
    f12 = ImageFont.truetype(font_path, 12)
    f15 = ImageFont.truetype(font_path, 15)
    f16 = ImageFont.truetype(font_path, 16)
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    # ---- header (section 6.2 UI-3: clock-driven, never lap-driven)
    d.text((10, 4), "R14 SPANISH GRAND PRIX", font=f16, fill=ORANGE)
    if big_state:
        # UI-3 is the page's most important line: spend the slack on it
        d.text((10, 23), "GRID (PROVISIONAL)", font=f15, fill=TEXT)
        d.text((10, 41), "LIGHTS OUT IN 02:14:30", font=f12, fill=MUTED)
        y0 = 58
    else:
        d.text((10, 24), "GRID (PROVISIONAL)  ·  LIGHTS OUT IN 02:14:30", font=f12, fill=MUTED)
        y0 = 42

    # ---- column header
    X_POS, X_NUM, X_FLAG, X_NAME = 12, 44, 72, 96
    X_TEAM = 178
    X_GAP = 400 if with_team else 300
    d.text((X_POS, y0), "POS", font=f12, fill=MUTED)
    d.text((X_NUM, y0), "#", font=f12, fill=MUTED)
    d.text((X_FLAG, y0), "NAT", font=f12, fill=MUTED)
    d.text((X_NAME, y0), "DRIVER", font=f12, fill=MUTED)
    if with_team:
        d.text((X_TEAM, y0), "TEAM", font=f12, fill=MUTED)
    d.text((X_GAP, y0), "GAP", font=f12, fill=MUTED)
    y = y0 + 16
    d.line([(8, y), (W - 8, y)], fill=BORDER)
    y += 2

    overflow = None
    for r in rows:
        if y + row_h > H - 2 and overflow is None:
            overflow = r["pos"]
        if y + row_h > H:
            break
        is_w = r["acr"] == watched
        if is_w:
            d.rectangle([6, y - 1, W - 6, y + row_h - 2], fill=HILITE)
        # team colour bar
        c = tuple(int(r["colour"][i:i + 2], 16) for i in (0, 2, 4))
        d.rectangle([6, y, 9, y + row_h - 3], fill=c)
        ty = y + (row_h - 14) // 2
        d.text((X_POS, ty), str(r["pos"]), font=f12, fill=TEXT)
        d.text((X_NUM, ty), str(r["num"]), font=f12, fill=MUTED)
        # flag placeholder, real size 16x12 (section 6.6)
        if r["iso"]:
            d.rectangle([X_FLAG, ty + 2, X_FLAG + 15, ty + 13], fill=(0x33, 0x44, 0x66))
            d.text((X_FLAG + 1, ty + 1), r["iso"][:3], font=ImageFont.truetype(font_path, 9),
                   fill=(0xDD, 0xE4, 0xF5))
        d.text((X_NAME, ty), r["name"], font=f12, fill=(0xFF, 0xFF, 0xFF) if is_w else TEXT)
        if with_team:
            d.text((X_TEAM, ty), r["team"], font=f12, fill=MUTED)
        d.text((X_GAP, ty), "—" if r["pos"] == 1 else f"+{r['pos'] * 0.731:.3f}",
               font=f12, fill=MUTED)
        y += row_h
        d.line([(8, y - 1), (W - 8, y - 1)], fill=BORDER)

    return img, overflow, y, f12


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "renders"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rows = load_grid()
    print(f"grid loaded: {len(rows)} drivers from the real fixtures\n")

    f12 = ImageFont.truetype(a.font, 12)
    longest_name = max(rows, key=lambda r: len(r["name"]))
    longest_team = max(rows, key=lambda r: len(r["team"]))
    print(f"  per-char advance @12px : {f12.getlength('M'):.1f} px")
    print(f"  longest surname        : {longest_name['name']} "
          f"({len(longest_name['name'])} ch, {f12.getlength(longest_name['name']):.0f} px)")
    print(f"  longest team           : {longest_team['team']} "
          f"({len(longest_team['team'])} ch, {f12.getlength(longest_team['team']):.0f} px)")
    asc, desc = f12.getmetrics()
    print(f"  12px ascent/descent    : {asc}/{desc}  -> natural line {asc + desc} px\n")

    variants = [("a-team-18px", True, 18, False), ("b-team-19px", True, 19, False),
                ("c-noteam-18px", False, 18, False), ("d-team-17px", True, 17, False),
                ("RECOMMENDED-team-18px-bigstate", True, 18, True)]
    for label, wt, rh, bs in variants:
        img, overflow, yend, _ = render(rows, a.font, wt, rh, label, big_state=bs)
        p = os.path.join(a.out, f"order-page-{label}.png")
        img.save(p)
        verdict = "FITS" if overflow is None else f"OVERFLOWS at P{overflow}"
        print(f"  {label:16} row={rh}px team={'y' if wt else 'n'}  "
              f"last y={yend:3}  {verdict}  -> {os.path.basename(p)}")


if __name__ == "__main__":
    main()
