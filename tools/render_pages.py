#!/usr/bin/env python3
"""Render every page at 480x480 from the ACTUAL generated headers.

Not mockups: this parses f1_circuits.h, f1_flags.h, f1_drivers.h, f1_legends.h,
f1_portraits.h, f1_facts.h and f1_calendar.h - the same bytes the firmware
carries - plus the captured fixtures, and lays them out with the same geometry
and the same Roboto Mono metrics the device uses.

That makes these a check on the design rather than a drawing of it: a line that
overflows, a column that collides or a card that is too dense shows up here,
without a board (section 14.1).

  python3 tools/render_pages.py --font /path/to/RobotoMono.ttf
"""
import argparse, io, json, math, os, re, textwrap
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "f1-tracker")
SAMP = os.path.join(ROOT, "reference", "samples")
OUT = os.path.join(ROOT, "reference", "mockups")
W = H = 480

BG      = (0x00, 0x00, 0x00)
ORANGE  = (0xFF, 0x8A, 0x1F)
TEXT    = (0xC9, 0xD3, 0xF2)
MUTED   = (0x7E, 0x8B, 0xB3)
DIM     = (0x5A, 0x66, 0x87)
PANEL   = (0x10, 0x1B, 0x3D)
BORDER  = (0x2D, 0x5B, 0xD0)
WATCH   = (0x1A, 0x25, 0x47)
MAPBG   = (0x0B, 0x12, 0x20)
ROW_BRD = (0x22, 0x30, 0x5A)
WHITE   = (0xFF, 0xFF, 0xFF)


# ---------------------------------------------------------------- parsers
def _txt(name):
    return open(os.path.join(SRC, name)).read()


def circuits():
    s = _txt("f1_circuits.h")
    pts = [int(v) for v in re.findall(r"-?\d+", re.search(r"PTS\[\d+\] = \{(.*?)\};", s, re.S).group(1))]
    rows = re.findall(r'\{"([^"]+)", "([^"]*)", "([^"]*)", (\d+), (\d+), (-?\d+), (-?\d+), '
                      r'(\d+), (-?\d+), (\d+), (\d+), (-?\d+), (true|false)\}', s)
    out = [dict(id=r[0], name=r[1], loc=r[2], first=int(r[3]), n=int(r[4]), hw=int(r[5]),
                hh=int(r[6]), length=int(r[7]), alt=int(r[8]), opened=int(r[9]),
                firstgp=int(r[10]), rot=int(r[11]), closed=r[12] == "true") for r in rows]
    idmap = {m.group(1): int(m.group(2)) for m in re.finditer(r'\{"([a-z_0-9]+)", (\d+)\},\s*//', s)}
    return pts, sorted(out, key=lambda c: c["id"]), idmap


def flags():
    s = _txt("f1_flags.h")
    out = {}
    for m in re.finditer(r'\{"([A-Z]{3})",\s*\{([^}]*)\},\s*\{([^}]*)\}\}', s):
        v = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{4})", m.group(2))]
        im = Image.new("RGB", (16, 12))
        im.putdata([(((c >> 11) & 31) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3) for c in v])
        out[m.group(1)] = im
    return out


def profiles(fn):
    s = _txt(fn)
    rows = re.findall(r'\{"([a-z_0-9]+)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", '
                      r'"([^"]*)", "([^"]*)", (\d+), (\d+), (\d+), (\d+), (-?\d+), '
                      r'(\d+), (\d+), (\d+), (\d+)\}', s)
    return [dict(id=r[0], given=r[1], family=r[2], code=r[3], iso3=r[4], dob=r[5], line=r[6],
                 number=int(r[7]), wins=int(r[8]), starts=int(r[9]), podiums=int(r[10]),
                 poles=int(r[11]), first=int(r[12]), last=int(r[13]), titles=int(r[14]),
                 group=int(r[15])) for r in rows]


def portraits():
    s = _txt("f1_portraits.h")
    out, credits = {}, {}
    for m in re.finditer(r'// (\S+): ([^/\n]*?) / ([^\n]*)\ninline constexpr uint8_t (IMG_\w+)'
                         r'\[(\d+)\] = \{\n(.*?)\n\};', s, re.S):
        did, lic, cred, _, _, blob = m.groups()
        out[did] = bytes(int(v) for v in blob.replace("\n", "").split(",") if v.strip())
        credits[did] = cred.strip()
    return out, credits


def facts():
    s = _txt("f1_facts.h")
    return {m.group(1): m.groups() for m in re.finditer(
        r'\{"([a-z_0-9]+)", (\d+), "([^"]*)", "([^"]*)", (\d+), "([^"]*)", "([^"]*)", '
        r'"([^"]*)", (\d+), "([^"]*)", (\d+)\}', s)}


def calendar():
    s = _txt("f1_calendar.h")
    season = int(re.search(r"SEASON = (\d+)", s).group(1))
    rows = re.findall(r'\{(\d+), "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)",\s*'
                      r'\{([^}]*)\}\}', s)
    return season, [dict(round=int(r[0]), name=r[1], cid=r[2], country=r[3], iso3=r[4],
                         locality=r[5]) for r in rows]


# ---------------------------------------------------------------- helpers
def fit_box(c, w, h, pad=3.0):
    hw, hh = c["hw"] or 1, c["hh"] or 1
    return min((w - 2 * pad) / (2 * hw), (h - 2 * pad) / (2 * hh)), w / 2.0, h / 2.0


def trace_points(c, pts, s, cx, cy):
    out = [(cx + pts[c["first"] * 2 + i * 2] * s, cy - pts[c["first"] * 2 + i * 2 + 1] * s)
           for i in range(c["n"])]
    if c["closed"]:
        out.append(out[0])
    return out


def draw_map(d, c, pts, ox, oy, w, h, lw=3):
    # no panel behind the trace: it sits on the page's own black
    s, cx, cy = fit_box(c, w, h)
    p = [(ox + x, oy + y) for x, y in trace_points(c, pts, s, cx, cy)]
    d.line(p, fill=(0xE8, 0xED, 0xF7), width=lw, joint="curve")
    ax, ay = p[0]; bx, by = p[1]
    dx, dy = bx - ax, by - ay
    m = math.hypot(dx, dy) or 1.0
    px, py = -dy / m, dx / m
    d.line([(ax - px * 8, ay - py * 8), (ax + px * 8, ay + py * 8)], fill=ORANGE, width=lw)
    if c["rot"]:
        th = math.radians((-c["rot"] / 100.0) % 360.0)
        nx, ny = ox + w - 18, oy + 18
        d.line([(nx + math.sin(th) * 7, ny + math.cos(th) * 7),
                (nx - math.sin(th) * 7, ny - math.cos(th) * 7)], fill=MUTED, width=2)
        d.text((nx - 3, ny + 9), "N", font=FONT[10], fill=MUTED)


def footer(d, credit=None):
    """The attribution always sits on the bottom line. A photo credit, where one
    is needed, STACKS above it rather than sharing the line - they are both
    bottom-left, and the gear owns the bottom-right corner."""
    if credit:
        d.text((12, 448), credit, font=FONT[12], fill=DIM)
    d.text((12, 464), "Data: jolpi.ca · openf1.org", font=FONT[12], fill=DIM)


def gear(d):
    d.ellipse([W - 40, H - 40, W - 14, H - 14], outline=(0xB8, 0xBE, 0xC9), width=2)
    d.ellipse([W - 31, H - 31, W - 23, H - 23], outline=(0xB8, 0xBE, 0xC9), width=2)


FONT = {}


# ---------------------------------------------------------------- pages
def page_wifi(ctx):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    # the real generated mark, not a placeholder
    mark = os.path.join(ROOT, "reference", "logo", "boot-200.png")
    if os.path.exists(mark):
        img.paste(Image.open(mark), ((W - 200) // 2, 56))
    d.text((240, 300), "Connecting to Wi-Fi", font=FONT[34], fill=WHITE, anchor="mm")
    d.text((240, 348), "home-ssid", font=FONT[22], fill=(0xB0, 0xB0, 0xB0), anchor="mm")
    d.text((W - 60, 464), "v0.1.0", font=FONT[12], fill=DIM)
    return img, "page-1-wifi"


def page_race(ctx):
    pts, cs, idmap = ctx["circ"]
    season, cal = ctx["cal"]
    rnd = cal[13]
    c = cs[idmap[rnd["cid"]]]
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), f"R{rnd['round']} {rnd['name'].upper()}"[:34], font=FONT[16], fill=ORANGE)
    # the flag sits BELOW the clock, which owns the top-right corner - a
    # collision this renderer caught before any board existed
    if rnd["iso3"] in ctx["flags"]:
        img.paste(ctx["flags"][rnd["iso3"]].resize((24, 18), Image.NEAREST), (W - 36, 34))
    d.text((10, 26), "LIGHTS OUT IN 02:14:30", font=FONT[16], fill=TEXT)
    d.text((10, 46), "GRID (PROVISIONAL)", font=FONT[12], fill=MUTED)
    d.text((W - 12, 2), "14:35", font=FONT[24], fill=TEXT, anchor="ra")
    draw_map(d, c, pts, 90, 70, 300, 240)
    # top-5 strip
    st = ctx["grid"][:5]
    y = 320
    for i, e in enumerate(st):
        d.rectangle([6, y + 3, 10, y + 17], fill=e["colour"])
        if e["iso3"] in ctx["flags"]:
            img.paste(ctx["flags"][e["iso3"]], (54, y + 4))
        d.text((18, y + 2), f"P{i+1:<3}", font=FONT[12],
               fill=WHITE if e["code"] == "VER" else TEXT)
        d.text((76, y + 2), f"{e['code']:<4} {e['team']}"[:26], font=FONT[12],
               fill=WHITE if e["code"] == "VER" else TEXT)
        y += 20
    f = ctx["facts"].get(rnd["cid"])
    rx = 288
    d.text((rx, 320), f"{c['length']/1000:.3f} km", font=FONT[12], fill=MUTED)
    if f and f[2]:
        d.text((rx, 338), f"FL {f[2]}", font=FONT[12], fill=MUTED)
    if f and f[8] != "0":
        d.text((rx, 356), f"last {f[6]}", font=FONT[12], fill=MUTED)
    footer(d); gear(d)
    return img, "page-2-race"


def page_order(ctx):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), "R14 SPANISH GRAND PRIX", font=FONT[16], fill=ORANGE)
    d.text((10, 23), "GRID (PROVISIONAL)", font=FONT[15], fill=TEXT)
    d.text((10, 41), "LIGHTS OUT IN 02:14:30", font=FONT[12], fill=MUTED)
    d.text((12, 58), "POS  #   NAT DRIVER     TEAM              TO POLE", font=FONT[12], fill=MUTED)
    d.line([(8, 74), (W - 8, 74)], fill=ROW_BRD)
    y = 78
    for i, e in enumerate(ctx["grid"]):
        if y + 18 > H - 4:
            break
        if e["code"] == "VER":
            d.rectangle([6, y - 1, W - 6, y + 16], fill=WATCH)
        d.rectangle([6, y + 1, 9, y + 15], fill=e["colour"])
        ty = y + 2
        d.text((12, ty), str(i + 1), font=FONT[12], fill=TEXT)
        d.text((44, ty), str(e["number"]), font=FONT[12], fill=MUTED)
        if e["iso3"] in ctx["flags"]:
            img.paste(ctx["flags"][e["iso3"]], (72, ty + 2))
        d.text((96, ty), e["name"], font=FONT[12], fill=WHITE if e["code"] == "VER" else TEXT)
        d.text((178, ty), e["team"][:16], font=FONT[12], fill=MUTED)
        d.text((400, ty), "—" if i == 0 else f"+{i*0.164:.3f}", font=FONT[12], fill=MUTED)
        y += 18
        d.line([(8, y - 1), (W - 8, y - 1)], fill=ROW_BRD)
    return img, "page-3-order"


def _card_head(img, d, ctx, badge, title, iso3):
    d.text((10, 6), badge, font=FONT[12], fill=ORANGE)
    d.text((10, 24), title[:38], font=FONT[18], fill=TEXT)
    if iso3 and iso3 in ctx["flags"]:
        img.paste(ctx["flags"][iso3].resize((32, 24), Image.NEAREST), (420, 26))


def page_circuit(ctx):
    pts, cs, idmap = ctx["circ"]
    season, cal = ctx["cal"]
    rnd = next(r for r in cal if r["cid"] == "monaco")
    c = cs[idmap["monaco"]]
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    _card_head(img, d, ctx, "CIRCUIT", c["name"], rnd["iso3"])
    draw_map(d, c, pts, 90, 52, 300, 240)
    f = ctx["facts"].get("monaco")
    body = f"{c['loc']}   {c['length']/1000:.3f} km   first GP {c['firstgp']}   Round {rnd['round']}, {season}"
    if f:
        _, races, flt, flw, fly, ev, drv, team, ly, top, topn = f
        if flt:
            body += f"\n\nFastest lap  {flt}   {flw} {fly}  (since 2004)"
        if ly != "0":
            body += f"\nLast winner  {drv}, {team}\n             {ly} {ev}"
        if int(topn) > 1:
            body += f"\nMost wins    {top} ({topn})   {races} races held"
    d.multiline_text((12, 300), body, font=FONT[12], fill=MUTED, spacing=4)
    footer(d); gear(d)
    return img, "page-4-circuit"


def _profile_card(ctx, p, badge, name):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    _card_head(img, d, ctx, badge, f"{p['given']} {p['family']}", p["iso3"])
    blob = ctx["por"][0].get(p["id"])
    credit = None
    if blob:
        img.paste(Image.open(io.BytesIO(blob)), (24, 56))
        credit = f"photo: {ctx['por'][1].get(p['id'], '')}"[:58]
    tx, y = 290, 60
    if p["number"] and p["code"]:
        d.text((tx, y), f"#{p['number']}  {p['code']}", font=FONT[14], fill=MUTED); y += 24
    elif p["code"]:
        d.text((tx, y), p["code"], font=FONT[14], fill=MUTED); y += 24
    d.text((tx, y), f"{p['first']}-{p['last']}", font=FONT[14], fill=MUTED); y += 30
    d.text((tx, y), f"{p['starts']} starts", font=FONT[14], fill=TEXT); y += 20
    d.text((tx, y), f"{p['wins']} wins", font=FONT[14], fill=TEXT); y += 20
    if p["poles"] >= 0:
        d.text((tx, y), f"{p['poles']} poles", font=FONT[14], fill=TEXT); y += 20
    if p["titles"]:
        d.text((tx, y), f"{p['titles']} world title" + ("s" if p["titles"] > 1 else ""),
               font=FONT[14], fill=ORANGE)
    if p["line"]:
        d.multiline_text((16, 386), "\n".join(textwrap.wrap(p["line"], 62)),
                         font=FONT[12], fill=TEXT, spacing=4)
    footer(d, credit); gear(d)
    return img, name


def page_driver(ctx):
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    return _profile_card(ctx, p, "DRIVER · LEGEND", "page-5-driver")


def page_legend(ctx):
    p = next(x for x in ctx["legends"] if x["id"] == "senna")
    return _profile_card(ctx, p, "LEGEND", "page-6-legend")


def page_standings(ctx):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), "CHAMPIONSHIP", font=FONT[16], fill=ORANGE)
    d.text((12, 32), "POS DRIVER     TEAM            PTS", font=FONT[12], fill=MUTED)
    y = 52
    for s in ctx["standings"][:19]:
        d.text((12, y), f"{s['pos']:>2}  {s['name']:<10} {s['team'][:14]:<14} {s['points']:>4}"
               + ("  W" if s["wins"] else ""), font=FONT[14], fill=TEXT)
        y += 21
    footer(d); gear(d)
    return img, "page-7-standings"


def page_summary(ctx):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), "RACE SUMMARY", font=FONT[16], fill=ORANGE)
    s = ctx["summary"]
    body = (f"{s['season']} {s['event']}\n\n"
            f"{'Fastest lap':<16} {s['fl_time']}\n{'':<16} {s['fl_driver']}, lap {s['fl_lap']}\n\n"
            f"{'Pit stops':<16} {s['n_stops']}\n"
            f"{'Quickest':<16} {s['best_driver']}, {s['best']}\n\n"
            f"{'Podium':<16} " + " ".join(s["podium"]))
    d.multiline_text((16, 44), body, font=FONT[14], fill=TEXT, spacing=5)
    footer(d); gear(d)
    return img, "page-8-summary"


def page_settings(ctx):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((16, 14), "SETTINGS", font=FONT[16], fill=ORANGE)
    for x, w, col, lab in ((238, 110, (0x1A, 0x25, 0x47), "Cancel"), (356, 110, BORDER, "Save")):
        d.rounded_rectangle([x, 8, x + w, 46], 6, fill=col)
        d.text((x + w // 2, 27), lab, font=FONT[16], fill=TEXT, anchor="mm")

    def sw(y, on):
        d.rounded_rectangle([380, y, 452, y + 32], 16, fill=BORDER if on else (0x33, 0x3A, 0x4A))
        cx = 436 if on else 396
        d.ellipse([cx - 12, y + 4, cx + 12, y + 28], fill=WHITE)

    d.text((16, 66), "Auto-dim display", font=FONT[14], fill=TEXT); sw(60, True)
    d.multiline_text((16, 90),
        "Latitude and longitude set sunrise/sunset for auto-dim only.\n"
        "The time zone is fixed in firmware and does not follow them.",
        font=FONT[12], fill=MUTED, spacing=3)
    d.text((16, 136), "Brightness", font=FONT[14], fill=TEXT)
    d.rounded_rectangle([150, 140, 450, 154], 7, fill=(0x22, 0x30, 0x5A))
    d.rounded_rectangle([150, 140, 390, 154], 7, fill=BORDER)
    d.text((16, 180), "Carousel", font=FONT[14], fill=TEXT)
    d.text((150, 180), "45 s", font=FONT[14], fill=MUTED)
    d.rounded_rectangle([230, 184, 450, 198], 7, fill=(0x22, 0x30, 0x5A))
    d.rounded_rectangle([230, 184, 296, 198], 7, fill=BORDER)
    d.text((16, 224), "Watched driver alerts", font=FONT[14], fill=TEXT); sw(218, True)
    d.text((16, 262), "Milestones only", font=FONT[14], fill=TEXT); sw(256, False)
    d.text((W - 64, 464), "v0.1.0", font=FONT[12], fill=DIM)
    return img, "page-9-settings"


def page_debug(ctx):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((8, 8), "DEBUG", font=FONT[16], fill=ORANGE)
    d.rounded_rectangle([W - 104, 4, W - 8, 38], 6, fill=(0x1A, 0x25, 0x47))
    d.text((W - 56, 21), "Close", font=FONT[16], fill=TEXT, anchor="mm")
    body = ("SYSTEM\n"
            "  IP 192.168.1.47\n"
            "  Wi-Fi home-ssid  -58 dBm\n"
            "  Firmware 0.1.0  ESPHome 2026.9.1\n"
            "  Up 2d 06h 14m\n"
            "  RAM 196 kB  PSRAM 7944 kB free\n\n"
            "DATA\n"
            "  Calendar: fetched, 3 h\n"
            "  State: RACE_WEEK\n"
            "  Order: GRID (PROVISIONAL)\n"
            "  Watched: max_verstappen, entered\n"
            "  Carousel: CIRCUIT 14/40\n"
            "  Jolpica 41 fetches, last 3 h\n"
            "  OpenF1 skipped (window) x6")
    d.multiline_text((8, 36), body, font=FONT[12], fill=TEXT, spacing=4)
    return img, "page-10-debug"


def overlay_detail(ctx):
    img, _ = page_order(ctx)
    d = ImageDraw.Draw(img, "RGBA")
    d.rectangle([0, 0, W, H], fill=(0, 0, 0, 150))
    d.rounded_rectangle([20, 40, 460, 440], 12, fill=PANEL, outline=BORDER, width=2)
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    d.text((36, 52), f"{p['given']} {p['family']}", font=FONT[18], fill=ORANGE)
    if p["iso3"] in ctx["flags"]:
        img.paste(ctx["flags"][p["iso3"]].resize((32, 24), Image.NEAREST), (412, 56))
    body = (f"#{p['number']}  {p['code']}   Red Bull Racing\n\n"
            f"{'Starts (provisional)':<20} P8\n"
            f"{'Gap':<20} +5.848\n\n"
            f"{'Career starts':<20} {p['starts']}\n"
            f"{'Wins':<20} {p['wins']}\n"
            f"{'Poles':<20} {p['poles']}\n"
            f"{'World titles':<20} {p['titles']}\n\n" + "\n".join(textwrap.wrap(p["line"], 48)))
    d.multiline_text((36, 90), body, font=FONT[14], fill=TEXT, spacing=5)
    d.text((240, 420), "tap anywhere to close", font=FONT[12], fill=DIM, anchor="mm")
    return img, "overlay-detail-card"


def overlay_alert(ctx):
    img, _ = page_race(ctx)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 60, W, 156], fill=WATCH)
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    if p["iso3"] in ctx["flags"]:
        img.paste(ctx["flags"][p["iso3"]].resize((48, 36), Image.NEAREST), (24, 90))
    d.text((240, 108), "MAX IS RACING TODAY", font=FONT[24], fill=WHITE, anchor="mm")
    return img, "overlay-alert-event"


def overlay_milestone(ctx):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.rectangle([0, 130, W, 330], fill=WATCH)
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    if p["iso3"] in ctx["flags"]:
        img.paste(ctx["flags"][p["iso3"]].resize((64, 48), Image.NEAREST), (208, 152))
    d.text((240, 232), "MAX WINS", font=FONT[34], fill=WHITE, anchor="mm")
    d.text((240, 280), f"{p['wins']+1}TH CAREER WIN", font=FONT[18], fill=ORANGE, anchor="mm")
    return img, "overlay-alert-milestone"


def page_offseason(ctx):
    pts, cs, idmap = ctx["circ"]
    season, _ = ctx["cal"]
    c = cs[idmap["suzuka"]]
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), "F1 TRACKER", font=FONT[16], fill=ORANGE)
    d.text((10, 26), f"{season} SEASON COMPLETE", font=FONT[16], fill=TEXT)
    d.text((10, 46), "next calendar not yet published", font=FONT[12], fill=MUTED)
    d.text((W - 12, 2), "14:35", font=FONT[24], fill=TEXT, anchor="ra")
    draw_map(d, c, pts, 90, 70, 300, 240)
    d.text((12, 322), c["name"], font=FONT[14], fill=TEXT)
    d.text((12, 344), f"{c['loc']}   {c['length']/1000:.3f} km", font=FONT[12], fill=MUTED)
    s = ctx["standings"][0]
    d.text((12, 376), f"{season} champion", font=FONT[12], fill=MUTED)
    d.text((12, 394), f"{s['name']}, {s['team']}   {s['points']} pts", font=FONT[14], fill=TEXT)
    footer(d); gear(d)
    return img, "page-11-offseason"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/tmp/RobotoMono.ttf")
    ap.add_argument("--sans", default=None)
    a = ap.parse_args()
    for sz in (10, 12, 14, 15, 16, 18, 22, 24, 34):
        FONT[sz] = ImageFont.truetype(a.font, sz)
    os.makedirs(OUT, exist_ok=True)

    pts, cs, idmap = circuits()
    fl = flags()
    drv = profiles("f1_drivers.h")
    leg = profiles("f1_legends.h")

    # grid from the real fixtures, as mock_order_page does
    pos = json.load(open(os.path.join(SAMP, "openf1-position-11377.json")))
    pos.sort(key=lambda r: r["date"])
    first = {}
    for r in pos:
        first.setdefault(r["driver_number"], r)
    of = {d["driver_number"]: d for d in json.load(open(os.path.join(SAMP, "openf1-drivers-11377.json")))}
    bycode = {p["code"]: p for p in drv if p["code"]}
    grid = []
    for g in sorted(first.values(), key=lambda r: r["position"]):
        o = of.get(g["driver_number"], {})
        code = o.get("name_acronym", "???")
        p = bycode.get(code)
        grid.append(dict(number=g["driver_number"], code=code,
                         name=(p["family"].upper() if p else code),
                         team=o.get("team_name", "-"),
                         iso3=(p["iso3"] if p else ""),
                         colour=tuple(int((o.get("team_colour") or "888888")[i:i+2], 16)
                                      for i in (0, 2, 4))))

    sj = json.load(open(os.path.join(SAMP, "jolpica-driver-standings.json")))
    sl = sj["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"]
    standings = [dict(pos=int(x["position"]), points=int(float(x["points"])),
                      wins=int(x["wins"]), name=x["Driver"]["familyName"].upper(),
                      team=x["Constructors"][0]["name"]) for x in sl]

    fa = json.load(open(os.path.join(SAMP, "jolpica-last-fastest.json")))["MRData"]["RaceTable"]["Races"][0]
    ps = json.load(open(os.path.join(SAMP, "jolpica-last-pitstops.json")))["MRData"]["RaceTable"]["Races"][0]["PitStops"]
    rs = json.load(open(os.path.join(SAMP, "jolpica-last-results.json")))["MRData"]["RaceTable"]["Races"][0]["Results"]
    best = min(ps, key=lambda x: float(x["duration"]))
    summary = dict(season=fa["season"], event=fa["raceName"],
                   fl_time=fa["Results"][0]["FastestLap"]["Time"]["time"],
                   fl_driver=fa["Results"][0]["Driver"]["familyName"].upper(),
                   fl_lap=int(fa["Results"][0]["FastestLap"]["lap"]),
                   n_stops=len(ps), best=f"{float(best['duration']):.3f}s",
                   best_driver=best["driverId"].split("_")[-1].upper(),
                   podium=[r["Driver"]["familyName"].upper() for r in rs[:3]])

    ctx = dict(circ=(pts, cs, idmap), flags=fl, drivers=drv, legends=leg,
               por=portraits(), facts=facts(), cal=calendar(), grid=grid,
               standings=standings, summary=summary)

    pages = [page_wifi, page_race, page_order, page_circuit, page_driver, page_legend,
             page_standings, page_summary, page_settings, page_debug, page_offseason,
             overlay_detail, overlay_alert, overlay_milestone]
    made = []
    for fn in pages:
        img, name = fn(ctx)
        p = os.path.join(OUT, f"{name}.png")
        img.save(p)
        made.append(name)
        print(f"  {name}")

    # contact sheet
    cols = 4
    rows = (len(made) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 248, rows * 268), (0x14, 0x16, 0x1A))
    sd = ImageDraw.Draw(sheet)
    for i, name in enumerate(made):
        im = Image.open(os.path.join(OUT, f"{name}.png")).resize((240, 240), Image.LANCZOS)
        ox, oy = (i % cols) * 248 + 4, (i // cols) * 268 + 4
        sheet.paste(im, (ox, oy))
        sd.text((ox + 2, oy + 244), name, font=FONT[14], fill=(0x9A, 0xA4, 0xB8))
    sheet.save(os.path.join(OUT, "all-pages.png"))
    print(f"\n{len(made)} renders + all-pages.png -> {OUT}")


if __name__ == "__main__":
    main()
