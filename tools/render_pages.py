#!/usr/bin/env python3
"""Render every page at 480x480 from the ACTUAL generated headers.

Not mockups: this parses f1_circuits.h, f1_flags.h, f1_drivers.h, f1_legends.h,
f1_portraits.h, f1_facts.h and f1_calendar.h - the same bytes the firmware
carries - plus the captured fixtures, and lays them out with the same geometry
and the same Roboto Mono metrics the device uses.

That makes these a check on the design rather than a drawing of it: a line that
overflows, a column that collides or a card that is too dense shows up here,
without a board (section D.1).

  python3 tools/render_pages.py --font /path/to/RobotoMono.ttf
"""
import argparse, glob, io, json, math, os, re, sys, textwrap
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tracker_common
import math
import critter_common as cc
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "f1-tracker")
SAMP = os.path.join(ROOT, "reference", "samples")
OUT = os.path.join(ROOT, "docs", "renders")
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


def team_iso3():
    """The demonym -> ISO3 table of f1_store.h team_iso3(), so the render uses the firmware's own."""
    s = _txt("f1_store.h")
    body = s[s.index("inline const char *team_iso3"):]
    body = body[:body.index("return \"\";   // decision 20")]
    return dict(re.findall(r'\{"([^"]+)", "([A-Z]{3})"\}', body))


TEAM_ISO3 = team_iso3()


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


def _champ_line(standings):
    """Mirrors f1::champ::line - 8 rounds left at the fixture's round 15."""
    if len(standings) < 2:
        return ""
    rounds_left, sprints_left = 8, 2
    avail = rounds_left * 26 + sprints_left * 8
    gap = standings[0]["points"] - standings[1]["points"]
    alive = 1 + sum(1 for s in standings[1:] if s["points"] + avail >= standings[0]["points"])
    if alive == 1:
        return f"{standings[0]['name']} has won the championship"
    if gap > (rounds_left - 1) * 26 + sprints_left * 8:
        return f"{standings[0]['name']} can clinch it this weekend"
    if alive == 2 and 0 < gap <= avail:
        return (f"{standings[0]['name']} leads {standings[1]['name']} by {gap}, "
                f"{avail} still available")
    return ""


def geometry():
    """Widget x/y/width/height, read from the YAML itself.

    The renderer used to carry these as literals, and twice they drifted from
    the firmware - once hiding a 52 px collision between the map and the top-5
    strip, because the render drew the map shorter than the device does. A
    render is only a check while it is derived from what the firmware actually
    does, so the geometry comes from one place: the YAML.
    """
    s = open(os.path.join(ROOT, "f1-tracker.yaml"), encoding="utf-8").read()
    out, cur = {}, None
    for line in s.split("\n"):
        # Inline flow form: "- switch: {id: sw_autodim, x: 380, y: 60, ...}".
        # The settings page is written this way, and a line-based parser that
        # only understands the block form silently returns nothing for it -
        # which the drift guard caught the moment it was added.
        fm = re.search(r"\{[^}]*\bid:\s*(\w+)[^}]*\}", line)
        if fm:
            wid = fm.group(1)
            out.setdefault(wid, {})
            for k in ("x", "y", "width", "height"):
                kv = re.search(r"\b" + k + r":\s*(-?\d+)", fm.group(0))
                if kv:
                    out[wid].setdefault(k, int(kv.group(1)))
            continue
        m = re.match(r"\s+id:\s*(\w+)\s*(?:#.*)?$", line)
        if m:
            cur = m.group(1)
            out.setdefault(cur, {})
            continue
        if cur is None:
            continue
        for k in ("x", "y", "width", "height"):
            mm = re.match(r"\s+" + k + r":\s*(-?\d+)\s*(?:#.*)?$", line)
            if mm and k not in out[cur]:
                out[cur][k] = int(mm.group(1))
    return out


def box(G, wid, dx=0, dy=0):
    """(x, y, w, h) for a widget, with the page offsets the YAML implies."""
    if wid not in G:
        raise DriftError(f"box('{wid}') - no such widget in the YAML")
    g = G[wid]
    return (g.get("x", 0) + dx, g.get("y", 0) + dy,
            g.get("width", 0), g.get("height", 0))


class DriftError(Exception):
    """The renderer drew something the firmware does not have."""


def require(G, page, *ids):
    """Every widget a page draws must EXIST in the YAML.

    This renderer has drifted from the firmware three times, and each time it
    made the renders worse than useless - they looked fine while the device
    would not have been:

      1. a footer fix landed in the YAML and silently not here;
      2. the map was drawn 240 tall while the firmware had 300, hiding a 52 px
         collision with the top-5 strip;
      3. a flag was drawn in the alert banner that the firmware never had.

    Geometry now comes from the YAML (see geometry()), which fixes 1 and 2.
    This fixes 3: a page declares the widgets it draws, and inventing one is a
    hard failure rather than a nicer-looking picture.
    """
    missing = [i for i in ids if i not in G]
    if missing:
        raise DriftError(f"{page} draws {missing}, which the YAML does not define")


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
        # decision 202: an arrow with barbs and an "N", as draw_trace draws it
        th = math.radians((-c["rot"] / 100.0) % 360.0)
        nx, ny = ox + w - 22, oy + 20
        dx, dy = -math.sin(th), -math.cos(th)
        px, py = -dy, dx
        hx, hy = nx + dx * 8, ny + dy * 8
        d.line([(nx - dx * 8, ny - dy * 8), (hx, hy)], fill=MUTED, width=2)
        d.line([(hx - dx * 5 + px * 4, hy - dy * 5 + py * 4), (hx, hy),
                (hx - dx * 5 - px * 4, hy - dy * 5 - py * 4)], fill=MUTED, width=2)
        d.text((nx - 4, ny + 10), "N", font=FONT[12], fill=MUTED)


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
def _title(img, d, cx, cy):
    """The title row, as on the device: the F1 logo, 12 px, then "Tracker", centred on cx."""
    mark = Image.open(os.path.join(ROOT, "reference", "logo", "f1-wordmark.png"))
    tw = d.textlength("Tracker", font=FONT[50])
    x0 = cx - (mark.width + 12 + tw) / 2
    img.paste(mark, (int(round(x0)), int(round(cy - mark.height / 2))))
    d.text((x0 + mark.width + 12, cy), "Tracker", font=FONT[50], fill=WHITE, anchor="lm")


def page_wifi(ctx, mode="connecting"):
    require(ctx["geo"], "page-1-wifi", "boot_mark", "boot_title", "boot_f1", "boot_name", "wifi_title", "wifi_body")
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    # the real generated mark, not a placeholder
    mark = os.path.join(ROOT, "reference", "logo", "boot-200.png")
    # Same column as the firmware: logo, name, status, body (NET-2b).
    top = 40
    if os.path.exists(mark):
        img.paste(Image.open(mark), ((W - 200) // 2, top))
    _title(img, d, 240, top + 200 + 10 + 20)
    title, body = {
        "loading":    ("Loading", ""),
        "connecting": ("Connecting to Wi-Fi", "home-ssid"),
        "ap":         ("Set up Wi-Fi", "Join 'F1 Tracker - 9CAD'\nfrom your phone"),
    }[mode]
    y = top + 200 + 10 + 40 + 8 + 6
    d.text((240, y + 11), title, font=FONT[22], fill=WHITE, anchor="mm")
    d.multiline_text((240, y + 11 + 30), body, font=FONT[22], fill=(0xB0, 0xB0, 0xB0),
                     anchor="ma", align="center", spacing=4)
    d.text((W - 60, 464), "v0.1.0", font=FONT[12], fill=DIM)
    name = {"loading": "page-1-wifi", "connecting": "page-1b-wifi-connecting",
            "ap": "page-1c-wifi-setup"}[mode]
    return img, name


def page_wifi_loading(ctx): return page_wifi(ctx, "loading")
def page_wifi_connecting(ctx): return page_wifi(ctx, "connecting")
def page_wifi_ap(ctx): return page_wifi(ctx, "ap")


def page_race(ctx, hide_strip=False):
    require(ctx["geo"], "page-2-race", "race_title", "race_state", "race_sub",
            "race_clock", "race_flag", "map_box", "top5", "race_facts", "gear_btn")
    pts, cs, idmap = ctx["circ"]
    season, cal = ctx["cal"]
    rnd = cal[13]
    c = cs[idmap[rnd["cid"]]]
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), f"R{rnd['round']} \u00b7 {rnd['name']}"[:34], font=FONT[16], fill=ORANGE)
    # the flag sits BELOW the clock, which owns the top-right corner - a
    # collision this renderer caught before any board existed
    if rnd["iso3"] in ctx["flags"]:
        img.paste(ctx["flags"][rnd["iso3"]].resize((24, 18), Image.NEAREST), (W - 36, 34))
    d.text((10, 26), "Lights out in 2:14:30", font=FONT[16], fill=TEXT)
    d.text((10, 46), "Grid (provisional)", font=FONT[12], fill=MUTED)
    d.text((W - 12, 2), "14:35", font=FONT[24], fill=TEXT, anchor="ra")
    G = ctx["geo"]
    mx, my, mw, mh = box(G, "map_box")
    draw_map(d, c, pts, mx, my, mw, mh)
    # top-5 strip
    st = [] if hide_strip else ctx["grid"][:5]
    tx0, y, _, _ = box(G, "top5")
    for i, e in enumerate(st):
        d.rectangle([tx0, y + 3, tx0 + 4, y + 17], fill=e["colour"])
        if e["iso3"] in ctx["flags"]:
            img.paste(ctx["flags"][e["iso3"]], (tx0 + 48, y + 4))
        d.text((tx0 + 12, y + 2), f"P{i+1:<3}", font=FONT[12],
               fill=WHITE if e["code"] == "VER" else TEXT)
        d.text((tx0 + 70, y + 2), f"{e['code']:<4} {e['team']}"[:26], font=FONT[12],
               fill=WHITE if e["code"] == "VER" else TEXT)
        y += 20
    f = ctx["facts"].get(rnd["cid"])
    rx, ry, _, _ = box(G, "race_facts")
    if not hide_strip:
        d.text((rx, ry), f"{c['length']/1000:.3f} km", font=FONT[13], fill=MUTED)       # mono13 (decision 202)
        if f and f[2]:
            d.text((rx, ry + 19), f"FL {f[2]}", font=FONT[13], fill=MUTED)
        if f and f[8] != "0":
            d.text((rx, ry + 38), f"last {f[6]}", font=FONT[13], fill=MUTED)
        # 8.2 / decision 202: the season progress lives here now
        done = sum(1 for r in cal if r["round"] < rnd["round"])
        d.text((rx, ry + 57), f"{len(cal) - done} of {len(cal)} to go", font=FONT[13], fill=MUTED)
    footer(d); gear(d)
    return img, "page-2-race"


def page_order(ctx):
    require(ctx["geo"], "page-3-order", "order_title", "order_clock", "order_hdr", "order_rows")
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), "R14 \u00b7 Spanish Grand Prix", font=FONT[16], fill=ORANGE)
    d.text((10, 23), "Grid (provisional)", font=FONT[15], fill=TEXT)
    d.text((10, 41), "Lights out in 2:14:30", font=FONT[12], fill=MUTED)
    clock(d)
    d.text((12, 58), "Pos  #   Nat Driver     Team               To pole", font=FONT[12], fill=MUTED)
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


def clock(d):
    """The clock, upper right of every content page (owner): mono24, 10 px in."""
    d.text((W - 12, 2), "14:35", font=FONT[24], fill=TEXT, anchor="ra")


def _card_head(img, d, ctx, badge, title, iso3):
    d.text((10, 4), badge, font=FONT[16], fill=ORANGE)      # the same two lines as the race page's header
    d.text((10, 24), title[:38], font=FONT[16], fill=TEXT)
    clock(d)
    if iso3 and iso3 in ctx["flags"]:
        # decision 202: just past the end of the title, never a fixed x
        x = min(10 + int(d.textlength(title[:38], font=FONT[16])) + 12, 480 - 12 - 32)
        img.paste(ctx["flags"][iso3].resize((32, 24), Image.NEAREST), (x, 24))


def page_circuit(ctx):
    require(ctx["geo"], "page-4-circuit", "card_badge", "card_title", "card_map", "circuit_clock",
            "card_body", "card_flag", "card_credit")
    pts, cs, idmap = ctx["circ"]
    season, cal = ctx["cal"]
    rnd = next(r for r in cal if r["cid"] == "monaco")
    c = cs[idmap["monaco"]]
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    _card_head(img, d, ctx, "Circuit", c["name"], None)      # the flag sits by the place name instead
    cx, cy, cw, ch = box(ctx["geo"], "card_map")
    draw_map(d, c, pts, cx, cy, cw, ch)
    f = ctx["facts"].get("monaco")
    # decision 202: four lines under a full-width trace, in mono14, no blank line, a
    # 13-character label column - as f1_ui.h composes it; the first line indented for the flag
    body = f"{c['loc']}    · {c['length']/1000:.3f} km · first GP {c['firstgp']} · Round {rnd['round']}, {season}"
    if rnd["iso3"] in ctx["flags"]:
        fx, fy, _, _ = box(ctx["geo"], "card_body")
        cell = d.textlength("0", font=FONT[14])      # the flag AFTER the place name, as f1_ui.h places it
        img.paste(ctx["flags"][rnd["iso3"]], (int(fx + (len(c["loc"]) + 1) * cell), fy + 3))
    if f:
        _, races, flt, flw, fly, ev, drv, team, ly, top, topn = f
        if flt:
            body += f"\n{'Fastest lap':<13}{flt}  {flw} {fly} (since 2004)"
        if ly != "0":
            body += f"\n{'Last winner':<13}{drv}, {team} {ly}"
        if int(topn) > 1:
            body += f"\n{'Most wins':<13}{top} ({topn}) · {races} races held"
    bx, by, _, _ = box(ctx["geo"], "card_body")
    d.multiline_text((bx, by), body, font=FONT[14], fill=MUTED, spacing=3)
    footer(d); gear(d)
    return img, "page-4-circuit"


def _age(dob, season):
    """f1_ui.h age_from_dob(): the age in the season."""
    try:
        return int(season) - int(dob[:4]) if dob else 0
    except ValueError:
        return 0


def _profile_card(ctx, p, badge, name, photo_rows=320):
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    _card_head(img, d, ctx, badge, f"{p['given']} {p['family']}", p["iso3"])
    blob = ctx["por"][0].get(p["id"])
    credit = None
    if blob:
        # decoded top-down (f1_photo.h): rows not yet done are black, as the device's buffer starts
        px, py, pw, ph_ = box(ctx["geo"], "card_photo")
        d.rectangle([24, 56, 24 + 239, 56 + 319], fill=(0, 0, 0))
        img.paste(Image.open(io.BytesIO(blob)).crop((0, 0, 240, photo_rows)), (24, 56))
        credit = f"photo: {ctx['por'][1].get(p['id'], '')}"[:58]
    # the text beside the portrait: the same lines f1_ui.h compose_profile() writes, in the
    # card_body label (mono12, 7.2 px/char) moved to x 282, y 60, 190 px wide
    tx, y, _, _ = 282, 60, 190, 0
    cols = int(190 // 7.2)
    lines = []
    if p["number"] and p["code"]: lines.append(f"#{p['number']}  {p['code']}")
    elif p["code"]: lines.append(p["code"])
    eras = f"{p['first']}-{p['last']}" if p["first"] and p["last"] else ""
    age = _age(p["dob"], ctx["cal"][0])
    lines.append(eras + (f"   age {age}" if age else ""))
    lines += ["", f"{p['starts']} starts   {p['wins']} wins" + (f"   {p['poles']} poles" if p["poles"] >= 0 else "")]
    if p["titles"]:
        lines.append(f"{p['titles']} world title" + ("s" if p["titles"] > 1 else ""))
    if p["line"]:
        lines += [""] + p["line"].split("\n")
    wrapped = []
    for ln in lines:
        wrapped += textwrap.wrap(ln, cols) or [""]
    d.multiline_text((tx, y), "\n".join(wrapped), font=FONT[12], fill=MUTED, spacing=4)
    footer(d, credit); gear(d)
    return img, name


def page_driver(ctx):
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    return _profile_card(ctx, p, "DRIVER · LEGEND", "page-5-driver")


def _silhouette():
    """STAND-IN for a fetched headshot: 206x206, a plain silhouette (the real picture is the F1
    media server's, fetched by the device, and is not kept in this repository)."""
    ph_img = Image.new("RGB", (206, 206), (0, 0, 0))
    pd = ImageDraw.Draw(ph_img)
    for r in range(0, 206):                                          # a soft studio gradient behind
        v = int(40 + 50 * (1 - r / 206))
        pd.line([(0, r), (205, r)], fill=(v // 2, v // 2, v))
    pd.ellipse([68, 28, 138, 112], fill=(205, 170, 140))              # head
    pd.rounded_rectangle([34, 118, 172, 230], 40, fill=(30, 50, 110))  # shoulders
    pd.rectangle([92, 100, 114, 126], fill=(205, 170, 140))           # neck
    return ph_img


def _rookie_card(ctx, name, photo_rows=206):
    """A driver with no compiled portrait: the card as f1_ui.h draws it with a fetched headshot.
    The picture is a STAND-IN (a plain silhouette drawn here): the real one is the F1 media
    server's, fetched by the device, and is not kept in this repository."""
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    _card_head(img, d, ctx, "Driver", "Arvid Lindblad", "GBR")
    px, py, pw, ph_ = box(ctx["geo"], "card_photo")
    ph_img = _silhouette()
    d.rectangle([px, py, px + 205, py + 205], fill=(0, 0, 0))
    img.paste(ph_img.crop((0, 0, 206, photo_rows)), (px, py))
    # the text beside it, as f1_ui.h lays it out: x 282, y 60, 190 wide
    d.multiline_text((282, 60), "#41  LIN\n\nNew this season", font=FONT[12], fill=MUTED, spacing=4)
    footer(d, "photo: formula1.com"); gear(d)
    return img, name


def page_rookie(ctx):
    return _rookie_card(ctx, "page-5c-rookie")


def page_rookie_loading(ctx):
    """The headshot arriving: the first rows, top-down."""
    return _rookie_card(ctx, "page-5d-rookie-loading", photo_rows=84)


def page_driver_loading(ctx):
    """Part way through the decode: 128 of the 320 rows, top-down (f1_photo.h)."""
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    return _profile_card(ctx, p, "DRIVER · LEGEND", "page-5b-driver-loading", photo_rows=128)


def page_legend(ctx):
    p = next(x for x in ctx["legends"] if x["id"] == "senna")
    return _profile_card(ctx, p, "Legend", "page-6-legend")


def page_standings(ctx):
    require(ctx["geo"], "page-7-standings", "standings_clock", "standings_hdr", "standings_body",
            "constructors_hdr", "constructors_body", "champ_line")
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), "Championship", font=FONT[16], fill=ORANGE)
    clock(d)
    d.text((12, 32), "Pos     Driver      Pts", font=FONT[12], fill=MUTED)
    LH = 22          # mono12's line height 16 + 6 px of line space (decision 202); the firmware places the flags on the label's own pitch
    BOTTOM = 428     # the scroll area ends here: rows past it are reached by scrolling
    cw = d.textlength("0", font=FONT[12])
    y = 52
    for s in ctx["standings"][:22]:
        if y + LH > BOTTOM: break          # the rest scrolls
        d.text((12, y), f"{s['pos']:>2}      {s['name'][:10]:<10} {s['points']:>4}"
               + (" W" if s["wins"] else ""), font=FONT[12], fill=TEXT)
        if s.get("iso3") in ctx["flags"]:    # between the position and the name, by driverId
            img.paste(ctx["flags"][s["iso3"]], (int(12 + 3 * cw), y + (LH - 12) // 2))
        y += LH
    d.text((278, 32), "Constructors", font=FONT[12], fill=MUTED)
    y = 52
    for c in ctx["constructors"][:11]:
        d.text((278, y), f"{c['pos']:>2}      {c['name'][:13]:<13} {c['points']:>4}",
               font=FONT[12], fill=TEXT)
        if c.get("iso3") in ctx["flags"]:    # the team's nationality flag (store.h team_iso3)
            img.paste(ctx["flags"][c["iso3"]], (int(278 + 3 * cw), y + (LH - 12) // 2))
        y += LH
    # 8.1: blank for most of a season on purpose
    if ctx.get("champ_line"):
        d.text((12, 430), ctx["champ_line"], font=FONT[14], fill=(0xFF, 0xD5, 0x4A))
    footer(d); gear(d)
    return img, "page-7-standings"


def page_summary(ctx):
    require(ctx["geo"], "page-8-summary", "summary_clock", "summary_body")
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), "Race summary", font=FONT[16], fill=ORANGE)
    clock(d)
    s = ctx["summary"]
    body = (f"{s['season']} {s['event']}\n\n"
            f"{'Fastest lap':<16} {s['fl_time']}\n{'':<16} {s['fl_driver']}, lap {s['fl_lap']}\n\n"
            f"{'Pit stops':<16} {s['n_stops']}\n"
            f"{'Quickest':<16} {s['best_driver']}, {s['best']}\n\n"
            f"{'Podium':<16} " + " ".join(s["podium"]) + "\n\n"
            # decision 134: OpenF1's half, same text format_extras() produces
            f"{'Tyres':<16} RUS M30 S5\n{'':<16} VER M24 H11\n{'':<16} HAD H18 M17\n"
            f"{'Weather':<16} Track 39-48\u00b0C, air 26-27\u00b0C, dry\n"
            f"{'Incidents':<16} Safety car, lap 31\n{'':<16} Safety car, lap 36")
    d.multiline_text((16, 44), body, font=FONT[14], fill=TEXT, spacing=5)
    footer(d); gear(d)
    return img, "page-8-summary"


def _mdi(size=18):
    path = os.path.join(ROOT, "tools", ".cache", "mdi.ttf")     # the firmware's MDI 7.4.47
    return ImageFont.truetype(path, size) if os.path.exists(path) else None


def _pair(d, x, y, w, h, label, cp, size, fill=None):
    """An icon and a label centred as a pair, as the firmware's FLEX row does."""
    mdi = _mdi(18)
    tw = d.textlength(label, font=FONT[size])
    iw, gap = (18, 6) if mdi else (0, 0)
    x0 = x + (w - (iw + gap + tw)) / 2
    if mdi:
        d.text((x0, y + h // 2), chr(cp), font=mdi, fill=fill or TEXT, anchor="lm")
    d.text((x0 + iw + gap, y + h // 2), label, font=FONT[size], fill=fill or TEXT, anchor="lm")


def _icon_button(d, G, wid, label, cp, size):
    x, y, w, h = box(G, wid)
    if wid in G and G[wid].get("x", 0) < 0 or wid == "btn_about":
        x = W + G[wid].get("x", 0) - w          # BOTTOM_RIGHT / TOP_RIGHT anchored
        y = H + G[wid].get("y", 0) - h if wid == "btn_about" else y
    d.rounded_rectangle([x, y, x + w, y + h], 8, fill=(0x1A, 0x25, 0x47))
    _pair(d, x, y, w, h, label, cp, size)


def _settings_frame(ctx, page, tab, ids):
    """Header, the tab row and the panel origin - all read from the YAML."""
    G = ctx["geo"]
    require(G, page, "tab_display", "tab_race", "tab_location", "btn_about", "btn_update",
            "settings_title",
            "pnl_display", "pnl_race", "pnl_location", "settings_err", *ids)
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((16, 14), ("Display", "Race", "Location")[tab], font=FONT[16], fill=ORANGE)   # names the open tab
    for x, w, col, lab in ((238, 110, (0x1A, 0x25, 0x47), "Cancel"),
                           (356, 110, BORDER, "Save")):
        d.rounded_rectangle([x, 10, x + w, 48], 6, fill=col)
        d.text((x + w / 2, 10 + 19), lab, font=FONT[16], fill=TEXT, anchor="mm")
    mdi = None
    mdi_path = os.path.join(ROOT, "tools", ".cache", "mdi.ttf")     # the firmware's MDI 7.4.47
    if os.path.exists(mdi_path):
        mdi = ImageFont.truetype(mdi_path, 18)
    for i, (wid, cp) in enumerate((("tab_display", 0xF0379), ("tab_race", 0xF023C),
                                   ("tab_location", 0xF034E))):
        x, y, w, h = box(G, wid)
        d.rounded_rectangle([x, y, x + w, y + h], 8, fill=BORDER if i == tab else (0x1A, 0x25, 0x47))
        if mdi:      # icon only, centred (sky-tracker UI-16a)
            d.text((x + w / 2, y + h / 2), chr(cp), font=mdi, fill=TEXT, anchor="mm")
    for wid, lab in (("btn_update", "Upgrade Check"), ("btn_about", "About")):
        x, y, w, h = box(G, wid)
        d.rounded_rectangle([x, y, x + w, y + h], 8, fill=(0x1A, 0x25, 0x47))
        d.text((x + w / 2, y + h / 2), lab, font=FONT[16], fill=TEXT, anchor="mm")
    px, py, pw, ph = box(G, ("pnl_display", "pnl_race", "pnl_location")[tab])
    return img, d, px, py


def _sw(d, px, py, wid, G, on):
    x, y, w, h = box(G, wid, px, py)
    d.rounded_rectangle([x, y, x + w, y + h], h // 2, fill=BORDER if on else (0x33, 0x3A, 0x4A))
    cx = x + w - h // 2 if on else x + h // 2
    d.ellipse([cx - 12, y + 4, cx + 12, y + h - 4], fill=WHITE)


def _field(d, px, py, wid, G, text):
    x, y, w, h = box(G, wid, px, py)
    d.rectangle([x, y, x + w, y + h], fill=(0x0E, 0x18, 0x36), outline=(0x3A, 0x4E, 0x86), width=2)
    d.text((x + 8, y + h // 2), text, font=FONT[16], fill=(0xEE, 0xF2, 0xFF), anchor="lm")


def _slider(d, px, py, wid, G, frac):
    x, y, w, h = box(G, wid, px, py)
    d.rounded_rectangle([x, y, x + w, y + h], h // 2, fill=(0x22, 0x30, 0x5A))
    d.rounded_rectangle([x, y, x + int(w * frac), y + h], h // 2, fill=BORDER)


def _tracker_update_h():
    """tracker_update.h of tracker-common (see tools/tracker_common.py)."""
    try:
        return tracker_common.find(os.path.join("components", "tracker_common", "tracker_update.h"))
    except FileNotFoundError as e:
        raise DriftError(str(e))


def update_consts():
    """The card's geometry, read from tracker-common's tracker_update.h so the render cannot drift."""
    src = open(_tracker_update_h(), encoding="utf-8").read()
    out = {}
    for m in re.finditer(r"inline constexpr int ([^;]+);", src):
        for part in m.group(1).split(","):
            k, _, v = part.partition("=")
            out[k.strip()] = int(v.strip())
    need = ("CARD_W", "CARD_H", "CARD_TALL_W", "CARD_TALL_H", "NOTES_W", "NOTES_H", "NOTES_Y",
            "BTN_W", "BTN_H", "BTN_Y", "BTN_TALL_Y", "BAR_W", "BAR_H", "BAR_Y",
            "ICON_W", "ICON_H", "ICON_X", "ICON_Y")
    missing = [k for k in need if k not in out]
    if missing:
        raise DriftError(f"tracker_update.h no longer defines {missing}")
    return out


def _card(mode):
    c = update_consts()
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    tall = mode == "available"
    cw, ch = (c["CARD_TALL_W"], c["CARD_TALL_H"]) if tall else (c["CARD_W"], c["CARD_H"])
    d.rectangle([0, 0, W, H], fill=(0, 0, 0))
    x0, y0 = (W - cw) // 2, (H - ch) // 2
    d.rounded_rectangle([x0, y0, x0 + cw, y0 + ch], 14, fill=(0x0E, 0x18, 0x36), outline=(0x2A, 0x3A, 0x66), width=2)
    def line(txt, y, size, fill):
        d.text((x0 + cw // 2, y0 + y), txt, font=FONT[size], fill=fill, anchor="ma")
    l1y, l2y = (58, 82) if tall else (76, 104)
    btn_y = c["BTN_TALL_Y"] if tall else c["BTN_Y"]
    def button(i, label, col):
        bx = x0 + (cw // 2 - 186 if i == 0 else cw // 2 + 16)
        d.rounded_rectangle([bx, y0 + btn_y, bx + c["BTN_W"], y0 + btn_y + c["BTN_H"]], 8, fill=col)
        d.text((bx + c["BTN_W"] // 2, y0 + btn_y + c["BTN_H"] // 2), label, font=FONT[16], fill=WHITE, anchor="mm")
    def one_button(label):
        bx = x0 + (cw - c["BTN_W"]) // 2
        d.rounded_rectangle([bx, y0 + btn_y, bx + c["BTN_W"], y0 + btn_y + c["BTN_H"]], 8, fill=(0x1A, 0x25, 0x47))
        d.text((bx + c["BTN_W"] // 2, y0 + btn_y + c["BTN_H"] // 2), label, font=FONT[16], fill=WHITE, anchor="mm")
    if mode == "checking":
        line("Checking for updates", 16, "s34", WHITE); line("Asking GitHub for the latest release", l1y, 16, (0xDC, 0xE4, 0xF8))
        line("Installed: 0.1.0", l2y, 16, MUTED); one_button("Close"); name = "overlay-update-checking"
    elif mode == "latest":
        line("Up to date", 16, "s34", WHITE); line("0.1.0 is the latest version", l1y, 16, (0xDC, 0xE4, 0xF8))
        line("Checks again every hour", l2y, 16, MUTED); one_button("Close"); name = "overlay-update-latest"
    elif mode == "available":
        line("Update available", 16, "s34", WHITE); line("New version: 0.2.0", l1y, 16, (0xDC, 0xE4, 0xF8))
        line("Installed: 0.1.0 - what's new:", l2y, 16, MUTED)
        nx, ny = x0 + (cw - c["NOTES_W"]) // 2, y0 + c["NOTES_Y"]
        d.rounded_rectangle([nx, ny, nx + c["NOTES_W"], ny + c["NOTES_H"]], 8, fill=(0x0A, 0x11, 0x28))
        d.multiline_text((nx + 8, ny + 8),
            "- Settings in three tabs, with a\n  keyboard\n- About screen\n- Auto-dim from your latitude and\n  longitude\n"
            "- Firmware updates show what is new,\n  and never offer an older version",
            font=FONT[16], fill=TEXT, spacing=4)
        button(0, "Not now", (0x1A, 0x25, 0x47)); button(1, "Update", BORDER); name = "overlay-update-available"
    elif mode == "installing":
        line("Updating firmware", 16, "s34", WHITE); line("Downloading", l1y, 16, (0xDC, 0xE4, 0xF8))
        line("Keep the power on", l2y, 16, (0xFF, 0xB5, 0x47))
        bx, by = x0 + (cw - c["BAR_W"]) // 2, y0 + c["BAR_Y"]
        d.rounded_rectangle([bx, by, bx + c["BAR_W"], by + c["BAR_H"]], 8, fill=(0x1A, 0x25, 0x47))
        d.rounded_rectangle([bx, by, bx + int(c["BAR_W"] * 0.42), by + c["BAR_H"]], 8, fill=BORDER)
        name = "overlay-update-installing"
    else:
        line("Couldn't check", 16, "s34", WHITE); line("No network connection", l1y, 16, (0xDC, 0xE4, 0xF8))
        line("Check the Wi-Fi and try again later", l2y, 16, MUTED); one_button("Close"); name = "overlay-update-failed"
    return img, name


def overlay_update_available(ctx): return _card("available")
def overlay_update_installing(ctx): return _card("installing")
def overlay_update_checking(ctx): return _card("checking")
def overlay_update_failed(ctx): return _card("failed")


def page_race_with_icon(ctx):
    """The race page with the update icon beside the gear (UI-68a)."""
    img, name = page_race(ctx)
    c = update_consts()
    d = ImageDraw.Draw(img)
    x = W + c["ICON_X"] - c["ICON_W"]; y = H + c["ICON_Y"] - c["ICON_H"]
    d.text((x + c["ICON_W"] // 2, y + c["ICON_H"] // 2), "\u2193", font=FONT[24], fill=(0xFF, 0xB5, 0x47), anchor="mm")
    return img, "page-2b-race-update-icon"


def page_about(ctx):
    G = ctx["geo"]
    require(G, "page-12-about", "about_close", "about_mark", "about_title", "about_f1", "about_name", "about_version",
            "about_hdr", "about_by", "about_build", "about_device_hdr", "about_device", "about_data_hdr", "about_data", "about_legal")
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    mark = os.path.join(ROOT, "reference", "logo", "boot-200.png")
    mx, my, _, _ = box(G, "about_mark")
    if os.path.exists(mark):
        img.paste(Image.open(mark).resize((112, 112), Image.LANCZOS), (mx, my))
    # the close button is TOP_RIGHT anchored
    cx, cy, cw, ch = box(G, "about_close")
    cx = W + cx - cw
    d.rounded_rectangle([cx, cy, cx + cw, cy + ch], 8, fill=(0x1A, 0x25, 0x47))
    d.text((cx + cw // 2, cy + ch // 2), "Close", font=FONT[16], fill=TEXT, anchor="mm")
    def centred(wid, text, size, fill):
        x, y, w, h = box(G, wid)
        d.text((x + w // 2, y), text, font=FONT[size], fill=fill, anchor="ma")
    tx, ty, tw, th = box(G, "about_title")
    _title(img, d, tx + tw // 2, ty + th // 2)
    hx, hy, _, _ = box(G, "about_hdr")
    d.text((hx, hy), "About", font=FONT[16], fill=(0xFF, 0x8A, 0x1F))
    centred("about_version", "Version 0.1.0", 16, (0xDC, 0xE4, 0xF8))
    centred("about_by", "by Dan Morphis", 14, MUTED)
    centred("about_build", "ESPHome 2026.9.1, built Oct 8 2026", 12, MUTED)
    hx, hy, _, _ = box(G, "about_device_hdr")
    d.text((hx, hy), "This device", font=FONT[12], fill=(0xFF, 0x8A, 0x1F))
    dx, dy, dw, dh = box(G, "about_device")
    d.multiline_text((dx, dy),
        "f1-tracker-9cad68  192.168.1.52\nWi-Fi home-ssid  -52 dBm\nMAC 24:EC:4A:9C:AD:68\nUp 3h 12m",
        font=FONT[14], fill=(0xDC, 0xE4, 0xF8), spacing=2)
    hx, hy, _, _ = box(G, "about_data_hdr")
    d.text((hx, hy), "Data from", font=FONT[12], fill=(0xFF, 0x8A, 0x1F))
    x, y, w, h = box(G, "about_data")
    d.multiline_text((x, y),
        "Jolpica jolpi.ca, OpenF1 openf1.org (CC BY-NC-SA)\nPortraits: Wikimedia Commons, credited on each card\n"
        "Flags: flag-icons (MIT)  Circuits: f1-circuits (MIT)",
        font=FONT[14], fill=TEXT, spacing=2)
    x, y, w, h = box(G, "about_legal")
    d.multiline_text((x, y),
        "Unofficial and not affiliated with Formula 1, the FIA or any\nteam. The F1 logo and names are trademarks of their owners.",
        font=FONT[12], fill=DIM, spacing=5)
    return img, "page-12-about"


def _row_icon(d, px, py, y, cp):
    m = _mdi(18)
    if m: d.text((px + 16, py + y), chr(cp), font=m, fill=MUTED)


def page_settings(ctx):
    G = ctx["geo"]
    img, d, px, py = _settings_frame(ctx, "page-9-settings", 0,
        ("dd_bright", "sw_24h", "sl_bright"))
    _row_icon(d, px, py, 6, 0xF00E1); d.text((px + 44, py + 6), "Display Brightness", font=FONT[16], fill=TEXT)
    _field(d, px, py, "dd_bright", G, "Auto dim")
    bx, by, bw, bh = box(G, "dd_bright", px, py)
    d.polygon([(bx + bw - 26, by + bh // 2 - 3), (bx + bw - 14, by + bh // 2 - 3), (bx + bw - 20, by + bh // 2 + 4)], fill=MUTED)
    d.text((px + 44, py + 46), "dims after sunset, or dark from sunset to sunrise", font=FONT[12], fill=MUTED)
    _row_icon(d, px, py, 72, 0xF00DF); d.text((px + 44, py + 72), "Brightness", font=FONT[16], fill=TEXT)
    _slider(d, px, py, "sl_bright", G, 0.8)
    _row_icon(d, px, py, 116, 0xF0150); d.text((px + 44, py + 116), "Time format", font=FONT[16], fill=TEXT)   # clock-outline
    d.text((px + 250, py + 116), "12h", font=FONT[16], fill=TEXT); d.text((px + 378, py + 116), "24h", font=FONT[16], fill=TEXT)
    _sw(d, px, py, "sw_24h", G, True)
    return img, "page-9-settings"


def page_settings_race(ctx):
    G = ctx["geo"]
    img, d, px, py = _settings_frame(ctx, "page-9b-settings-race", 1,
        ("sl_carousel", "lbl_carousel", "sw_alerts", "sw_milestone"))
    _row_icon(d, px, py, 6, 0xF051B); d.text((px + 44, py + 6), "Carousel", font=FONT[16], fill=TEXT)
    d.text((px + 160, py + 6), "45 s", font=FONT[16], fill=MUTED)
    _slider(d, px, py, "sl_carousel", G, 0.29)
    _row_icon(d, px, py, 56, 0xF009E); d.text((px + 44, py + 56), "Watched driver alerts", font=FONT[16], fill=TEXT)
    _sw(d, px, py, "sw_alerts", G, True)
    _row_icon(d, px, py, 106, 0xF053A); d.text((px + 44, py + 106), "Milestones only", font=FONT[16], fill=TEXT)
    _sw(d, px, py, "sw_milestone", G, False)
    return img, "page-9b-settings-race"


def page_settings_location(ctx):
    G = ctx["geo"]
    img, d, px, py = _settings_frame(ctx, "page-9c-settings-location", 2,
        ("ta_lat", "ta_lon", "dd_tz", "settings_kb"))
    _row_icon(d, px, py, 10, 0xF0F57); d.text((px + 44, py + 10), "Latitude", font=FONT[16], fill=TEXT)
    _field(d, px, py, "ta_lat", G, "61.580")
    d.text((px + 320, py + 12), "N+  S-", font=FONT[12], fill=MUTED)
    _row_icon(d, px, py, 60, 0xF0F5A); d.text((px + 44, py + 60), "Longitude", font=FONT[16], fill=TEXT)
    _field(d, px, py, "ta_lon", G, "-149.440")
    d.text((px + 320, py + 62), "E+  W-", font=FONT[12], fill=MUTED)
    _row_icon(d, px, py, 110, 0xF01E7); d.text((px + 44, py + 110), "Time zone", font=FONT[16], fill=TEXT)   # earth
    _field(d, px, py, "dd_tz", G, "Alaska")
    bx, by, bw, bh = box(G, "dd_tz", px, py)
    d.polygon([(bx + bw - 26, by + bh // 2 - 3), (bx + bw - 14, by + bh // 2 - 3), (bx + bw - 20, by + bh // 2 + 4)], fill=MUTED)
    d.multiline_text((px + 16, py + 156),
        "Latitude and longitude set sunrise/sunset for auto-dim only.\n"
        "The time zone is its own setting above and does not follow them.",
        font=FONT[12], fill=MUTED, spacing=3)
    # The keyboard covers the lower third while a field is being edited; draw it
    # so a panel row that slides under it shows up here rather than on a device.
    kx, ky, kw, kh = box(G, "settings_kb")
    ky = H - (kh or 160)
    d.rectangle([0, ky, W, H], fill=(0x0E, 0x18, 0x36))
    # LVGL's NUMBER keyboard map (UI-44b): three rows of four, then five keys.
    # Glyphs are ASCII stand-ins for the LV_SYMBOL_* keys (keyboard, OK, backspace,
    # left, right); the key positions are the point, not the exact symbols.
    rows = (("1", "2", "3", "kb"), ("4", "5", "6", "OK"), ("7", "8", "9", "<x"),
            ("+/-", "0", ".", "<", ">"))
    rh = (H - ky - 16) // 4
    for r, keys in enumerate(rows):
        kw_ = (W - 16 - 6 * (len(keys) - 1)) / len(keys)
        for c, k in enumerate(keys):
            x0 = 8 + c * (kw_ + 6)
            y0 = ky + 8 + r * rh
            d.rounded_rectangle([x0, y0, x0 + kw_, y0 + rh - 6], 4, fill=(0x1A, 0x25, 0x47))
            d.text((x0 + kw_ / 2, y0 + (rh - 6) / 2), k, font=FONT[16], fill=TEXT, anchor="mm")
    return img, "page-9c-settings-location"


def anim_consts():
    """The animation's constants, read from f1_anim.h / f1_animlogic.h."""
    a = _txt("f1_anim.h"); b = _txt("f1_animlogic.h")
    c = {}
    for src in (a, b):
        for m in re.finditer(r"constexpr (?:int32_t|int|uint32_t|float) ([^;]+);", src):
            for part in m.group(1).split(","):
                k, _, v = part.partition("=")
                v = v.strip().rstrip("f")
                try:
                    c[k.strip()] = int(v, 0) if v.lower().startswith("0x") or v.lstrip("-").isdigit() else float(v)
                except ValueError:
                    pass
    m = re.search(r"CAR_COL\[[^\]]*\] = \{([^}]*)\}", a)
    if m:
        for k, v in enumerate(m.group(1).split(",")):
            c["CAR_COL%d" % k] = int(v.strip(), 16)
    c["OUT_MS"] = c["N_LIGHTS"] * c["STEP_MS"] + c["HOLD_MS"] if {"N_LIGHTS", "STEP_MS", "HOLD_MS"} <= set(c) else 0
    need = ("N_CARS", "CAR_GAP", "HEADINGS", "LAP_MS", "BOOT_LOGO", "ABOUT_LOGO", "CAR_COL0",
            "DEMO_SEED", "SWING_MIN", "SWING_MAX", "SIDE_PX_PER_100", "PASS_ZONE",
            "BOOT_LAMP_D", "BOOT_LAMP_GAP", "BOOT_LAMP_Y", "LAMP_OFF", "LAMP_ON", "N_LIGHTS", "STEP_MS", "HOLD_MS")
    missing = [k for k in need if k not in c]
    if missing:
        raise DriftError(f"f1_anim.h / f1_animlogic.h no longer define {missing}")
    return c


def _lap(size):
    """The Monza trace as the logo lays it out (gen_logo.layout), closed, with arc lengths."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import gen_logo
    pts = gen_logo.layout(float(size)); pts.append(pts[0])
    cum = [0.0]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        cum.append(cum[-1] + math.hypot(x1 - x0, y1 - y0))
    return pts, cum


def _lap_at(track, frac):
    pts, cum = track
    frac -= math.floor(frac)
    sdist = frac * cum[-1]
    i = 1
    while i < len(pts) - 1 and cum[i] < sdist:
        i += 1
    seg = cum[i] - cum[i - 1]
    u = (sdist - cum[i - 1]) / seg if seg else 0
    return (pts[i - 1][0] + (pts[i][0] - pts[i - 1][0]) * u, pts[i - 1][1] + (pts[i][1] - pts[i - 1][1]) * u)


def _car_sprites():
    """The alpha masks of f1_cars.h: {"LARGE"|"SMALL": [Image per heading]}."""
    src = _txt("f1_cars.h")
    sizes = {m.group(1): int(m.group(2)) for m in re.finditer(r"SZ_(LARGE|SMALL) = (\d+)", src)}
    out = {}
    for name, n in sizes.items():
        body = src[src.index(f"{name}[STEPS]"):]
        body = body[:body.index("};")]
        vals = [int(v) for v in re.findall(r"\b\d+\b", body[body.index("{"):])]
        out[name] = [Image.frombytes("L", (n, n), bytes(vals[i * n * n:(i + 1) * n * n]))
                     for i in range(len(vals) // (n * n))]
    return out


def _heading(track, frac, steps):
    """animlogic::heading(): the sprite that points along the track."""
    x0, y0 = _lap_at(track, frac - 0.004)
    x1, y1 = _lap_at(track, frac + 0.004)
    deg = math.degrees(math.atan2(x1 - x0, -(y1 - y0)))
    return round(deg / (360.0 / steps)) % steps


class Pack:
    """animlogic::Pack - the cars' speeds, from a seed. The same xorshift and the same maths as
    f1_animlogic.h, so the renders show what the device draws for that seed."""
    def __init__(self, c, seed):
        self.c = c
        x = (seed * 2654435761 + 0x9E3779B9) & 0xFFFFFFFF or 1
        def nxt():
            nonlocal x
            x ^= (x << 13) & 0xFFFFFFFF; x ^= x >> 17; x ^= (x << 5) & 0xFFFFFFFF
            return x
        for _ in range(4): nxt()
        self.n, self.a, self.phase = [], [], []
        n_cars = int(c["N_CARS"])
        for _ in range(n_cars):
            self.n.append(1 + nxt() % 2)
            self.a.append(c["SWING_MIN"] + (c["SWING_MAX"] - c["SWING_MIN"]) * (nxt() % 1000) / 999.0)
            self.phase.append((nxt() % 1000) / 1000.0)
        # MOTION-3a: lap 0 is the grid with no gain
        self.lap = 0
        self.off = [-k * c["CAR_GAP"] for k in range(n_cars)]
        self.gain = [0.0] * n_cars

    def next(self, seed):
        """animlogic::next_pack - the next lap's set: the order at the line re-dealt, each car's
        net pace carrying it to its new slot, position and speed continuous at the line."""
        c = self.c
        p = Pack(c, seed)
        p.lap = self.lap + 1
        x = (seed * 2246822519 + 0x3C6EF372) & 0xFFFFFFFF or 1
        for _ in range(4): x = _xorshift(x)
        slot = list(range(len(self.a)))
        for i in range(len(slot) - 1, 0, -1):
            x = _xorshift(x)
            j = x % (i + 1)
            slot[i], slot[j] = slot[j], slot[i]
        for k in range(len(self.a)):
            p.off[k] = self.off[k] + self.gain[k]
            p.gain[k] = -slot[k] * c["CAR_GAP"] - p.off[k]
            v = self.gain[k] + self.a[k] * math.cos(2 * math.pi * self.phase[k])
            need = v - p.gain[k]
            if p.a[k] < abs(need): p.a[k] = min(abs(need), c["SWING_CAP"])
            ph = math.acos(max(-1.0, min(1.0, need / p.a[k]))) / (2 * math.pi)
            p.phase[k] = ph if p.phase[k] < 0.5 else 1.0 - ph
        return p

    def swing(self, k, u):
        return self.a[k] * math.sin(2 * math.pi * (self.n[k] * u + self.phase[k])) / (2 * math.pi * self.n[k])

    def pos(self, k, u):
        f = u - self.lap
        return u + self.off[k] + self.gain[k] * f + self.swing(k, f) - self.swing(k, 0.0)


    def speed(self, k, u):
        f = u - self.lap
        return 1.0 + self.gain[k] + self.a[k] * math.cos(2 * math.pi * (self.n[k] * f + self.phase[k]))

    def pulling_out(self, k, u):
        w = 0.0
        for j in range(len(self.n)):
            if j == k: continue
            d = self.pos(k, u) - self.pos(j, u)
            d -= math.floor(d + 0.5)
            if abs(d) < self.c["PASS_ZONE"] and self.speed(k, u) > self.speed(j, u):
                w = max(w, 1.0 - abs(d) / self.c["PASS_ZONE"])
        return w


def _xorshift(x):
    x ^= (x << 13) & 0xFFFFFFFF; x ^= x >> 17; x ^= (x << 5) & 0xFFFFFFFF
    return x


def lap_pack(c, seed, lap):
    """animlogic::lap_pack - the pack for lap `lap` of an opening: lap 0 is Pack(seed), each later
    lap follows from the one before through the same xorshift chain the device walks."""
    p = Pack(c, seed)
    x = seed or 1
    for _ in range(lap):
        x = _xorshift(x)
        p = p.next(x)
    return p


def _hex(v): return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def _anim(img, c, logo_xy, size, t_ms, lights=False):
    """Draw the pack (and, on the boot page, the start lights) at `t_ms` after the page
    opened, as f1_anim.h does."""
    d = ImageDraw.Draw(img)
    if lights:
        step, n = c["STEP_MS"], int(c["N_LIGHTS"])
        lit = 0 if t_ms < step or t_ms >= c["OUT_MS"] else min(n, t_ms // step)
        if 0 <= t_ms < c["OUT_MS"] + 500:
            span = (n - 1) * c["BOOT_LAMP_GAP"]
            r = c["BOOT_LAMP_D"] / 2
            for i in range(n):
                cx = 240 - span / 2 + i * c["BOOT_LAMP_GAP"]
                d.ellipse([cx - r, c["BOOT_LAMP_Y"] - r, cx + r, c["BOOT_LAMP_Y"] + r],
                          fill=_hex(c["LAMP_ON"] if i < lit else c["LAMP_OFF"]))
    track = _lap(size)
    f = (t_ms % c["LAP_MS"]) / c["LAP_MS"]
    sprites = _car_sprites()["LARGE" if size >= 150 else "SMALL"]
    u = t_ms / c["LAP_MS"] if t_ms > 0 else 0.0
    pack = lap_pack(c, int(c["DEMO_SEED"]), int(u))              # MOTION-3a: this lap's swings
    side = c["SIDE_PX_PER_100"] * size / 100.0
    order = []
    for k in range(int(c["N_CARS"])):
        fk = pack.pos(k, u)
        x, y = _lap_at(track, fk)
        w = pack.pulling_out(k, u)
        if w > 0:                                            # the passer steps out to its left
            x2, y2 = _lap_at(track, fk + 0.004)
            dx, dy = x2 - x, y2 - y
            l = math.hypot(dx, dy)
            if l > 1e-4:
                x += dy / l * side * w; y -= dx / l * side * w
        order.append((w > 0, k, x, y, fk))
    for passing, k, x, y, fk in sorted(order):               # the car passing is drawn over the one it passes
        mask = sprites[_heading(track, fk, int(c["HEADINGS"]))]
        n = mask.width
        img.paste(Image.new("RGB", (n, n), _hex(c["CAR_COL%d" % k])),
                  (int(round(logo_xy[0] + x)) - n // 2, int(round(logo_xy[1] + y)) - n // 2), mask)


def _anim_boot(ctx, t_ms):
    c = anim_consts()
    img, _ = page_wifi(ctx, "connecting")
    _anim(img, c, ((W - 200) // 2, 40), 200, t_ms, lights=True)
    return img


def _anim_about(ctx, t_ms):
    c = anim_consts()
    img, _ = page_about(ctx)
    mx, my, _, _ = box(ctx["geo"], "about_mark")
    _anim(img, c, (mx, my), 112, t_ms)
    return img


def page_boot_cars(ctx):
    return _anim_boot(ctx, 2100), "page-1d-boot-cars"


def page_boot_strip(ctx):
    """Four moments of the boot sequence side by side: 1 light, 3, 5, then lights out."""
    c = anim_consts()
    ts = [c["STEP_MS"] * 1 + 100, c["STEP_MS"] * 3 + 100, c["STEP_MS"] * 5 + 400, c["OUT_MS"] + 250]
    frames = [_anim_boot(ctx, t).crop((60, 20, 420, 460)) for t in ts]   # to y 460: the lamps sit at 442
    out = Image.new("RGB", (360 * 4 + 12, 440), (0x20, 0x20, 0x20))
    for i, f in enumerate(frames):
        out.paste(f, (i * 364, 0))
    return out, "boot-cars-strip"


def _pass_moments(c):
    """The first overtake in the demo seed: (time ms, the passer, the passed)."""
    pack = Pack(c, int(c["DEMO_SEED"]))
    n = int(c["N_CARS"]); lap = c["LAP_MS"]
    for i in range(1, 4000):
        u0, u1 = (i - 1) / 4000, i / 4000
        for j in range(n):
            for k in range(n):
                if j == k: continue
                d0 = pack.pos(j, u0) - pack.pos(k, u0); d1 = pack.pos(j, u1) - pack.pos(k, u1)
                if d0 < 0 <= d1 and abs(d0) < 0.3 and abs(d1) < 0.3:     # j was behind k, now ahead
                    return u1 * lap, j, k
    raise DriftError("the demo seed has no overtake - choose another DEMO_SEED")


def page_overtake(ctx):
    """An overtake on the logo, zoomed: the pair a moment before, alongside, level and past."""
    c = anim_consts()
    t, j, k = _pass_moments(c)
    pack = Pack(c, int(c["DEMO_SEED"]))
    track = _lap(200)
    lap = c["LAP_MS"]
    frames = []
    for dt in (-0.07, -0.03, 0.0, 0.05):
        tt = max(1, int(t + dt * lap))
        # follow the pair: the window sits on the midpoint of the two cars at that moment
        mxp, myp = _lap_at(track, (pack.pos(j, tt / lap) + pack.pos(k, tt / lap)) / 2)
        cx, cy = (W - 200) // 2 + mxp, 40 + myp
        img = _anim_boot(ctx, tt)
        frames.append(img.crop((int(cx - 45), int(cy - 45), int(cx + 45), int(cy + 45))).resize((360, 360), Image.LANCZOS))
    out = Image.new("RGB", (360 * 4 + 12, 360), (0x20, 0x20, 0x20))
    for i, f in enumerate(frames):
        out.paste(f, (i * 364, 0))
    return out, "boot-cars-overtake"


def page_about_cars(ctx):
    return _anim_about(ctx, 1500), "page-12b-about-cars"


def page_debug(ctx):
    require(ctx["geo"], "page-10-debug", "debug_label")
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((8, 8), "Debug", font=FONT[16], fill=ORANGE)
    d.rounded_rectangle([W - 104, 10, W - 8, 44], 6, fill=(0x1A, 0x25, 0x47))
    d.text((W - 56, 27), "Close", font=FONT[16], fill=TEXT, anchor="mm")
    body = ("System\n"
            "  IP 192.168.1.47\n"
            "  Wi-Fi home-ssid  -58 dBm\n"
            "  Firmware 0.1.0  ESPHome 2026.9.1\n"
            "  Up 2d 06h 14m\n"
            "  RAM 196 kB  PSRAM 7944 kB free\n\n"
            "Data\n"
            "  Calendar: fetched, 3 h\n"
            "  State: RACE_WEEK\n"
            "  Order: GRID (PROVISIONAL)\n"
            "  Watched: max_verstappen, entered\n"
            "  Carousel: CIRCUIT 14/40\n"
            "  Jolpica 41 fetches, last 3 h\n"
            "  OpenF1 skipped (window) x6")
    d.multiline_text((8, 32), body, font=FONT[16], fill=TEXT, spacing=2)
    return img, "page-10-debug"


def _detail_card(ctx, name, photo_rows):
    """The detail card with the portrait beside the text. `photo_rows` of the 160 are
    decoded; the rest are black, the card's colour - as on the device while it decodes."""
    require(ctx["geo"], name, "detail_panel", "detail_title", "detail_body", "detail_flag",
            "detail_photo", "detail_credit")
    G = ctx["geo"]
    img, _ = page_order(ctx)
    d = ImageDraw.Draw(img, "RGBA")
    d.rectangle([0, 0, W, H], fill=(0, 0, 0, 150))
    d.rounded_rectangle([20, 20, 460, 460], 12, fill=PANEL, outline=BORDER, width=2)
    px0, py0 = 20, 20                       # the panel's origin: every child is relative to it
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    d.text((px0 + 16, py0 + 12), f"{p['given']} {p['family']}", font=FONT[18], fill=ORANGE)
    if p["iso3"] in ctx["flags"]:
        img.paste(ctx["flags"][p["iso3"]].resize((32, 24), Image.NEAREST), (412, 56))
    # the text keeps to 280 px beside the portrait (f1_detail.h TEXT_W_PHOTO), mono14 = 8.4 px/char
    cols = int(280 // 8.4)
    body = (f"#{p['number']}  {p['code']}   Red Bull Racing\n\n"
            f"{'Starts (provisional)':<20} P8\n"
            f"{'Gap':<20} +5.848\n\n"
            f"{'Career starts':<20} {p['starts']}\n"
            f"{'Wins':<20} {p['wins']}\n"
            f"{'Poles':<20} {p['poles']}\n"
            f"{'World titles':<20} {p['titles']}\n\n" + "\n".join(textwrap.wrap(p["line"], cols)))
    bx, by, _, _ = box(G, "detail_body", px0, py0)
    d.multiline_text((bx, by), body, font=FONT[16], fill=TEXT, spacing=4)
    # the portrait: half size, a 2x2 box average of the 240x320 JPEG (f1_jpg.h)
    jpg = ctx["por"][0].get(p["id"])
    fx, fy, _, _ = box(G, "detail_photo", px0, py0)
    if jpg:
        full = Image.open(io.BytesIO(jpg)).convert("RGB").resize((120, 160), Image.BOX)
        d.rectangle([fx, fy, fx + 119, fy + 159], fill=(0, 0, 0))      # the buffer starts black
        img.paste(full.crop((0, 0, 120, photo_rows)), (fx, fy))
        cx, cy, cw, _ = box(G, "detail_credit", px0, py0)
        credit = textwrap.wrap("photo: " + ctx["por"][1][p["id"]], int(cw // 7.2))
        d.multiline_text((cx, cy), "\n".join(credit), font=FONT[12], fill=MUTED, spacing=2)
    d.text((240, 440), "tap anywhere to close", font=FONT[12], fill=DIM, anchor="mm")
    return img, name


def overlay_detail_rookie(ctx):
    """The detail card for a driver with no compiled portrait (decision 193): the F1 media
    server's headshot, 206 px scaled to 120 (LVGL scale 256*120/206), credited to formula1.com."""
    require(ctx["geo"], "overlay-detail-rookie", "detail_panel", "detail_title", "detail_body", "detail_flag",
            "detail_photo", "detail_credit")
    G = ctx["geo"]
    img, _ = page_order(ctx)
    d = ImageDraw.Draw(img, "RGBA")
    d.rectangle([0, 0, W, H], fill=(0, 0, 0, 150))
    d.rounded_rectangle([20, 20, 460, 460], 12, fill=PANEL, outline=BORDER, width=2)
    px0, py0 = 20, 20
    d.text((px0 + 16, py0 + 12), "Arvid Lindblad", font=FONT[18], fill=ORANGE)
    if "GBR" in ctx["flags"]:
        img.paste(ctx["flags"]["GBR"].resize((32, 24), Image.NEAREST), (412, 56))
    body = "#41  LIN   Racing Bulls\n\n" + f"{'Starts (provisional)':<20} P12\n" + f"{'Gap':<20} +3.204"
    bx, by, _, _ = box(G, "detail_body", px0, py0)
    d.multiline_text((bx, by), body, font=FONT[16], fill=TEXT, spacing=4)
    fx, fy, _, _ = box(G, "detail_photo", px0, py0)
    sz = 256 * 120 // 206 * 206 // 256          # the scaled size LVGL gives it: ~120 px
    img.paste(_silhouette().resize((sz, sz), Image.BILINEAR), (fx, fy))
    cx, cy, cw, _ = box(G, "detail_credit", px0, py0)
    d.multiline_text((cx, cy), "\n".join(textwrap.wrap("photo: formula1.com", int(cw // 7.2))), font=FONT[12], fill=MUTED, spacing=2)
    d.text((240, 440), "tap anywhere to close", font=FONT[12], fill=DIM, anchor="mm")
    return img, "overlay-detail-rookie"


def overlay_detail(ctx):
    return _detail_card(ctx, "overlay-detail-card", 160)


def overlay_detail_loading(ctx):
    """Part way through the decode: the rows done so far, top-down (f1_jpg.h)."""
    return _detail_card(ctx, "overlay-detail-card-loading", 64)


def overlay_alert(ctx):
    require(ctx["geo"], "overlay-alert-event", "watch_banner", "watch_text", "watch_flag")
    img, _ = page_race(ctx, hide_strip=True)
    d = ImageDraw.Draw(img)
    # The lower band, over the top-5 strip. The trace stays fully visible: the
    # map is the thing a reader is looking at. Geometry from the YAML.
    _, by, _, bh = box(ctx["geo"], "watch_banner")
    d.rectangle([0, by, W, by + bh], fill=WATCH)
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    # flag 24-72, text from 120: 48 px of clear space between them
    if p["iso3"] in ctx["flags"]:
        img.paste(ctx["flags"][p["iso3"]].resize((48, 36), Image.NEAREST),
                  (24, by + bh // 2 - 18))
    d.text((120, by + bh // 2), "Max is racing today", font=FONT[24], fill=WHITE,
           anchor="lm")
    return img, "overlay-alert-event"


def overlay_milestone(ctx):
    require(ctx["geo"], "overlay-alert-milestone", "watch_banner", "watch_text",
            "watch_flag")
    img, _ = page_race(ctx, hide_strip=True)
    d = ImageDraw.Draw(img)
    # Same layout as an event banner - flag left with real padding, text beside
    # it - with a second line beneath. The flag previously sat directly above
    # the text and the two collided.
    by, bh = 326, 100
    d.rectangle([0, by, W, by + bh], fill=WATCH)
    p = next(x for x in ctx["drivers"] if x["id"] == "max_verstappen")
    if p["iso3"] in ctx["flags"]:
        img.paste(ctx["flags"][p["iso3"]].resize((48, 36), Image.NEAREST),
                  (24, by + bh // 2 - 18))
    d.text((120, by + 38), "Max wins", font=FONT[24], fill=WHITE, anchor="lm")
    n = p["wins"] + 1
    suf = "th" if 11 <= n % 100 <= 13 else {1: "st", 2: "nd", 3: "rd"}.get(n % 10, "th")
    d.text((120, by + 70), f"{n}{suf} career win", font=FONT[16], fill=ORANGE, anchor="lm")
    return img, "overlay-alert-milestone"


def page_offseason(ctx):
    require(ctx["geo"], "page-11-offseason", "race_title", "race_state", "map_box")
    pts, cs, idmap = ctx["circ"]
    season, _ = ctx["cal"]
    c = cs[idmap["suzuka"]]
    img = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(img)
    d.text((10, 4), "F1 Tracker", font=FONT[16], fill=ORANGE)
    d.text((10, 26), f"{season} season complete", font=FONT[16], fill=TEXT)
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


def animation_gifs(ctx, step_ms=50):
    """boot-cars.gif and about-cars.gif: one lap of the pack, looping, as f1_anim.h plays it
    (the same constants and sprites); the boot one opens with the start lights, which fit
    inside the first lap. Slow, so only with --gif.

    ONE palette for every frame. A palette per frame (the quantizer's default) drops the
    orange start/finish tick from some frames and keeps it in others, so the line flashes."""
    c = anim_consts()
    for name, fn in (("boot-cars", _anim_boot), ("about-cars", _anim_about)):
        rgb = [fn(ctx, t) for t in range(0, int(c["LAP_MS"]), step_ms)]
        # the palette comes from a spread of frames plus a swatch of every colour that must survive
        keep = [c["LAMP_ON"], c["LAMP_OFF"]] + [c["CAR_COL%d" % k] for k in range(int(c["N_CARS"]))] + \
               [0xFF8A1F, 0xF2F5FC, 0x121C33, 0xFFFFFF, 0x000000, 0xB0B0B0]
        sheet = Image.new("RGB", (W, H * 3 + 16), (0, 0, 0))
        for i, k in enumerate((0, len(rgb) // 3, 2 * len(rgb) // 3)):
            sheet.paste(rgb[k], (0, i * H))
        sw = ImageDraw.Draw(sheet)
        for i, col in enumerate(keep):
            sw.rectangle([i * 16, H * 3, i * 16 + 15, H * 3 + 15], fill=_hex(col))
        pal = sheet.quantize(colors=48, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        # the quantizer can still lose a small colour (the About logo's tick is ~180 px of a
        # 230,000-px frame): pin every must-keep colour into the palette exactly
        flat = pal.getpalette()[:48 * 3]
        used = set()
        for col in keep:
            want = _hex(col)
            best = min((i for i in range(48) if i not in used),
                       key=lambda i: sum((flat[i * 3 + j] - want[j]) ** 2 for j in range(3)))
            flat[best * 3:best * 3 + 3] = want
            used.add(best)
        pal.putpalette(flat + [0] * (768 - len(flat)))
        frames = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in rgb]
        path = os.path.join(OUT, name + ".gif")
        frames[0].save(path, save_all=True, append_images=frames[1:], duration=step_ms, loop=0,
                       optimize=False, disposal=1)
        print(f"  {name}.gif  {len(frames)} frames, {os.path.getsize(path) // 1024} KB")


# ---------------------------------------------------------------- the walking dinosaur (MOTION-5)
_DINO = {}


def _dino():
    """tools/dino_art.py: the dinosaur's vector art, and the Python twin of the walk's timing."""
    if not _DINO:
        import dino_art as mod
        _DINO["art"] = mod
        _DINO["poses"] = [mod.sprite(k) for k in range(mod.N_POSES)]
        _DINO["tails"] = [mod.tail_image(False), mod.tail_image(True)]
    return _DINO


def _dino_frame(ctx, bg, walk, t_ms):
    """One frame of a walk over `bg`, as f1_critter.h draws it."""
    D = _dino()
    return cc.frame(bg, D["art"], D["poses"], D["tails"], walk, t_ms, FONT[16], W)


def page_dino_walks(ctx):
    """Three walks on three lanes, caught walking in, roaring, and walking left: seeds chosen for the picture."""
    D = _dino(); art = D["art"]
    bg, _ = page_order(ctx)
    out = bg.copy()
    for seed, lane_pick in ((11, 0), (23, 1), (41, 2)):
        w = art.Walk(seed)
        w.lane = (70, 185, 335)[lane_pick]                       # the picture needs them apart
        t = (w.t_stop * 0.55, w.t_stop + 1000, w.t_stop * 0.4)[lane_pick]
        if lane_pick == 2: w.flip = True
        fr = _dino_frame(ctx, bg, w, int(t))
        top = w.lane - 46
        out.paste(fr.crop((0, top, W, w.lane + art.SH + 4)), (0, top))
    return out, "dino-walks"


def page_dino_poses(ctx):
    D = _dino(); art = D["art"]
    sheet = Image.new("RGB", (art.N_POSES * 150 + 10, 120), BG)
    for i, f in enumerate(D["poses"]):
        sheet.paste(f, (10 + i * 150, 12), f)
    return sheet, "dino-poses"


# ---------------------------------------------------------------- the toucan (MOTION-6)
_TOUCAN = {}


def _toucan():
    """tools/toucan_art.py: the toucan's vector art, and the Python twin of the flight's timing."""
    if not _TOUCAN:
        import toucan_art as mod
        _TOUCAN["art"] = mod
        _TOUCAN["poses"] = [mod.sprite(k) for k in range(mod.N_POSES)]
        _TOUCAN["tails"] = _dino()["tails"]          # the speech bubble's pointers are the dinosaur's
    return _TOUCAN


def _toucan_frame(ctx, bg, flight, t_ms):
    """One frame of a visit over `bg`, as f1_critter.h draws it."""
    D = _toucan()
    return cc.frame(bg, D["art"], D["poses"], D["tails"], flight, t_ms, FONT[16], W)


def page_toucan_flights(ctx):
    """Three visits on three lanes: in flight, perched and speaking, and flying left."""
    D = _toucan(); art = D["art"]
    bg, _ = page_order(ctx)
    out = bg.copy()
    for seed, lane_pick in ((11, 0), (23, 1), (41, 2)):
        w = art.Flight(seed)
        w.lane = (80, 190, 330)[lane_pick]
        w._finish()
        t = (w.t_stop * 0.6, w.t_stop + 1000, w.t_stop * 0.45)[lane_pick]
        if lane_pick == 2: w.flip = True
        fr = _toucan_frame(ctx, bg, w, int(t))
        top = w.lane - 60
        out.paste(fr.crop((0, top, W, w.lane + art.SH + 16)), (0, top))
    return out, "toucan-flights"


def page_toucan_poses(ctx):
    D = _toucan(); art = D["art"]
    sheet = Image.new("RGB", (art.N_POSES * 120 + 10, 100), BG)
    for i, f in enumerate(D["poses"]):
        sheet.paste(f, (10 + i * 120, 8), f)
    return sheet, "toucan-poses"


# ---------------------------------------------------------------- a hungry critter (MOTION-7)
# After plane-tracker's UI-68: eating is not plowing through the screen. The visitor stops at ONE thing
# - the circuit's trace, a driver's portrait - and eats that thing, bite by bite: each bite takes a
# scalloped piece off the near edge and shows the flesh (a pale rim), crumbs fall from the mouth, it
# chews, steps up to the rest, and when the last piece is gone it says its word and walks on. Nothing
# else on the card is touched.
CREAM = (0xEA, 0xDC, 0xB4)
def _bite_in(kind, left, dx, dy, r):
    """f1_critterbite.h bite_in: the shape of the mouth that took the bite (the bill's lens, the jaw's teeth)."""
    u = -dx if left else dx; v = dy; R = float(r)
    if kind == "bill":
        L = 2.6 * R; H = 0.55 * R
        t = (u + 0.3 * L) / L
        if t < 0 or t > 1: return False
        w = 2 * t - 1
        return abs(v) <= H * (1 - w * w)
    if u < -0.3 * R or abs(v) > 0.85 * R: return False
    w = v / (0.42 * R) + 100.0; f = w - math.floor(w)
    return u <= 1.1 * R - 0.35 * R * abs(f - 0.5) * 2


def _eaten(bg, h, k):
    """bg with the first k bites taken out of the item: each the shape of the mouth, the flesh a pale rim around it.
    Only the item is touched; the marks stay until the card changes."""
    if k == 0: return bg
    xa, ya, xb, yb = h.rect
    gone = Image.new("L", bg.size, 0); gp = gone.load()
    for i in range(k):
        (cx, cy), r = h.pts[i], h.radii[i]
        ext = int(r * 2.8) + 4
        for y in range(max(ya, cy - ext), min(yb, cy + ext) + 1):
            for x in range(max(xa, cx - ext), min(xb, cx + ext) + 1):
                if _bite_in(h.kind, h.flip, x - cx, y - cy, r): gp[x, y] = 255
    item = bg.convert("L").point(lambda v: 255 if v > 24 else 0)
    out = bg.copy()
    out.paste(Image.new("RGB", bg.size, BG), (0, 0), gone)
    rim = ImageChops.subtract(gone.filter(ImageFilter.MaxFilter(7)), gone)
    rim = ImageChops.multiply(rim, item)
    out.paste(Image.new("RGB", bg.size, CREAM), (0, 0), rim)
    return out


def _smooth(u):
    u = max(0.0, min(1.0, u))
    return u * u * (3 - 2 * u)


class _Hungry:
    """The visit: in to the first bite, then chomps (3 to 5, each a different size and place) with a chew after
    each and a move along the item to the next; the head goes to each bite (a lunge); the word; away. The marks stay."""
    def __init__(self, art, base, rect, pts, flip, word, speed, lunge, bubble_ms=1500):
        self.art, self.base, self.rect, self.pts, self.word, self.flip = art, base, rect, pts, word, flip
        self.words = [getattr(art, "HUNGRY_WORDS", art.WORDS)[word]] if hasattr(art, "HUNGRY_WORDS") else art.WORDS
        r = cc.Rng(len(pts) * 7919 + pts[0][0] + pts[0][1])
        self.radii = [17 + int(r.next() * 10) for _ in pts]
        toucan = hasattr(art, "BEAK_OPEN")
        self.kind = "bill" if toucan else "jaw"
        self.P_OPEN, self.P_HALF, self.P_REST = ((art.BEAK_OPEN, art.BEAK_HALF, art.PERCH) if toucan
                                                  else (art.ROAR_OPEN, art.ROAR_HALF, art.STAND))
        self.mouth = (-2, art.MOUTH_DY) if flip else (art.SW + 2, art.MOUTH_DY)
        self.speed, self.lunge = speed, lunge
        P = pts[0]
        base.flip = flip
        base.lane = P[1] - self.mouth[1]
        left = P[0] - self.mouth[0]
        base.stop_x = 480 - art.SW - left if flip else left
        base._finish()
        self.T0 = base.t_stop
        self.y_in = base.at(self.T0)[1]
        # the timeline: chomp k at tb[k]; after its chew, a move to the next point
        self.tb, self.moves = [], []
        t = self.T0 + 500
        for k in range(len(pts)):
            self.tb.append(t)
            t += 750 + int(r.next() * 500)                         # the chew, a different length each time
            if k + 1 < len(pts):
                d = math.hypot(pts[k + 1][0] - pts[k][0], pts[k + 1][1] - pts[k][1])
                mv = int(d / speed * 1000) + 150
                self.moves.append((t, t + mv, k)); t += mv + 150
        self.t_say = t
        self.t_exit = self.t_say + bubble_ms
        self.end_pt = pts[-1]
        self.t_end = self.t_exit + int(((self.end_pt[0] + 40 + art.SW) if flip else (520 - self.end_pt[0] + self.mouth[0])) / speed * 1000) + 300

    def anchor(self, t):
        """where the mouth is (the bite point) at t, and whether it is travelling"""
        for t0, t1, k in self.moves:
            if t0 <= t < t1:
                u = _smooth((t - t0) / (t1 - t0)); a, b = self.pts[k], self.pts[k + 1]
                return a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, True
        k = sum(1 for t0, t1, _ in self.moves if t >= t1)
        return self.pts[k][0], self.pts[k][1], False

    def lunging(self, t):
        """0..1: the head going into a bite (fast), then back (slower)"""
        e = 0.0
        for tb in self.tb:
            u = t - (tb - 130)
            if 0 <= u < 130: e = max(e, _smooth(u / 130))
            elif 130 <= u < 430: e = max(e, 1 - _smooth((u - 130) / 300))
        return e

    def at(self, t):
        art = self.art
        if t < self.T0:
            x, y, pose, _ = self.base.at(t)
            return x, y, pose, False
        if t < self.t_exit:
            ax, ay, moving = self.anchor(t)
            e = self.lunging(t)
            x = int(round(ax - self.mouth[0] + (-1 if self.flip else 1) * self.lunge[0] * e))
            y = int(round(ay - self.mouth[1] + self.lunge[1] * e))
            if moving:
                return x, y, int(t / 90) % 4, False
            for tb in self.tb:                                   # open wide going in, chew after
                if tb - 260 <= t < tb: return x, y, self.P_OPEN, False
                if tb <= t < tb + 600: return x, y, self.P_HALF if (t // 140) % 2 else self.P_REST, False
            return x, y, self.P_REST, t >= self.t_say
        d = (t - self.t_exit) * self.speed / 1000.0
        ax, ay = self.end_pt
        x0 = ax - self.mouth[0]
        return int(round(x0 - d if self.flip else x0 + d)), int(round(ay - self.mouth[1])), int(d / art.STEP_PX) % 4, False

    def bites_done(self, t):
        return sum(1 for tb in self.tb if t >= tb)

    def crumbs(self, t, bg):
        """(x, y, colour) of the pieces falling from each bite in the last 800 ms."""
        out = []
        for k, tb in enumerate(self.tb):
            u = (t - tb) / 1000.0
            if u < 0 or u > 0.8: continue
            r = cc.Rng(1000 + k)
            (cx, cy), rad = self.pts[k], self.radii[k]
            for j in range(9):
                sx = cx + rad * 0.3 + (r.next() - 0.3) * 50 * u + (r.next() - 0.5) * 16
                sy = cy + 6 - 70 * u * r.next() + 380 * u * u * (0.7 + 0.6 * r.next())
                col = None
                for _ in range(16):                               # a colour from the piece just taken
                    px = bg.getpixel((int(cx + r.next() * rad * 0.8), int(cy + (r.next() - 0.5) * rad * 1.4)))
                    if sum(px) > 90: col = px; break
                out.append((int(sx), int(sy), col or (255, 255, 255)))
        return out


def _hungry_frame(h, bg, poses, tails, t):
    img = _eaten(bg, h, h.bites_done(t))
    img = cc.frame(img, _WordArt(h.art, h.words), poses, tails, _AtShim(h), t, FONT[16], W)
    d = ImageDraw.Draw(img)
    for x, y, col in h.crumbs(t, bg):
        d.rectangle([x, y, x + 3, y + 3], fill=col)
    return img


class _WordArt:
    """the art, but saying what a hungry visit says"""
    def __init__(self, art, words): self._a, self.WORDS = art, words
    def __getattr__(self, n): return getattr(self._a, n)


class _AtShim:
    """cc.frame wants .at(t), .flip and .word."""
    def __init__(self, h): self.h, self.flip, self.word = h, h.flip, h.word
    def at(self, t): return self.h.at(t)


def _item_rect(bg, box):
    """The bounding box of what is drawn inside `box` (the thing to eat)."""
    xa, ya, xb, yb = bg.crop(box).convert("L").point(lambda v: 255 if v > 24 else 0).getbbox()
    return (box[0] + xa, box[1] + ya, box[0] + xb, box[1] + yb)


def _trace_walk(px, r1, p0, n, rng):
    """n points along the trace from p0, each 34-52 px on, keeping the way it was going (up the line first)."""
    pts = [p0]; dirv = (0.0, -1.0)
    ys, xs = r1[3] - r1[1], r1[2] - r1[0]
    cand = [(x + r1[0], y + r1[1]) for x in range(xs) for y in range(ys) if px[x, y] > 128 and x + r1[0] >= 146]
    for _ in range(n - 1):
        c = pts[-1]; best = []
        for q in cand:
            dx, dy = q[0] - c[0], q[1] - c[1]; d = math.hypot(dx, dy)
            if 34 <= d <= 52:
                best.append(((dx * dirv[0] + dy * dirv[1]) / d, q))
        best.sort(reverse=True)
        top = [q for cs, q in best[:6] if cs > 0.4] or [q for cs, q in best[:3]]
        if not top: break
        q = top[int(rng.next() * len(top))]
        d = math.hypot(q[0] - c[0], q[1] - c[1]); dirv = ((q[0] - c[0]) / d, (q[1] - c[1]) / d)
        pts.append(q)
    return pts


def _hungry_setups(ctx):
    D = _dino(); T = _toucan()
    D["art"].MOUTH_DY = int(D["art"].SH * 0.30); T["art"].MOUTH_DY = int(T["art"].SH * 0.45)
    bg1, _ = page_circuit(ctx)
    r1 = _item_rect(bg1, (0, 60, 420, 330))                  # the trace, not the north arrow
    px = bg1.convert("L").crop(r1).load()
    rng = cc.Rng(5)
    n1 = 3 + int(rng.next() * 3)                              # 3 to 5 chomps
    p0 = min(((x + r1[0], y + r1[1]) for x in range(r1[2] - r1[0]) for y in range(r1[3] - r1[1])
              if px[x, y] > 128 and x + r1[0] >= 146), key=lambda p: (p[0] - 192) ** 2 + (p[1] - 318) ** 2)
    pts1 = _trace_walk(px, r1, p0, n1, rng)
    h1 = _Hungry(D["art"], D["art"].Walk(23), r1, pts1, False, 0, 70, (12, 5))
    bg2, _ = page_driver(ctx)
    r2 = _item_rect(bg2, (0, 50, 270, 380))                  # the portrait, bitten along its right edge, the bird facing it
    rng2 = cc.Rng(9)
    n2 = 3 + int(rng2.next() * 3)
    ys = [140 + i * (170 // max(1, n2 - 1)) + int((rng2.next() - 0.5) * 24) for i in range(n2)]
    for i in range(n2 - 1, 0, -1):                            # not top to bottom: the bird hops about
        j = int(rng2.next() * (i + 1)); ys[i], ys[j] = ys[j], ys[i]
    pts2 = [(r2[2] - int(rng2.next() * 10), y) for y in ys]
    h2 = _Hungry(T["art"], T["art"].Flight(11), r2, pts2, True, 7, 130, (18, 9))
    return (("hungry-dino", bg1, h1, D), ("hungry-toucan", bg2, h2, T))


def _hungry_sheet(ctx, name):
    n, bg, h, D = next(x for x in _hungry_setups(ctx) if x[0] == name)
    ts = (h.tb[0] + 100, h.tb[len(h.tb) // 2] + 100, h.tb[-1] + 250, h.t_end - 100)
    sheet = Image.new("RGB", (W * 2 + 4, H * 2 + 4), (0x2D, 0x5B, 0xD0))
    for i, t in enumerate(ts):
        sheet.paste(_hungry_frame(h, bg, D["poses"], D["tails"], t), ((i % 2) * (W + 4), (i // 2) * (H + 4)))
    return sheet


def page_hungry_dino(ctx):
    """The dinosaur is hungry: three bites out of the circuit's trace, Chomp!, and the marks stay."""
    return _hungry_sheet(ctx, "hungry-dino"), "hungry-dino"


def page_hungry_toucan(ctx):
    """The toucan is hungry: three bites out of the driver's portrait, and the marks stay."""
    return _hungry_sheet(ctx, "hungry-toucan"), "hungry-toucan"


def hungry_gifs(ctx, fps=15):
    for name, bg, h, D in _hungry_setups(ctx):
        frames = [_hungry_frame(h, bg, D["poses"], D["tails"], int(i * 1000 / fps)) for i in range(int(h.t_end * fps / 1000) + 1)]
        pal = frames[len(frames) // 2].quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
        out = os.path.join(OUT, name + ".gif")
        q[0].save(out, save_all=True, append_images=q[1:], duration=int(1000 / fps), loop=0, optimize=False, disposal=1)
        print(f"  {name}.gif  {len(q)} frames, {os.path.getsize(out) // 1024} KB")


def _critter_gif(ctx, name, frame_fn, visit, fps=12):
    """<name>.gif: one visit across the order page, with the stop and the bubble."""
    bg, _ = page_order(ctx)
    frames = [frame_fn(ctx, bg, visit, int(i * 1000 / fps)) for i in range(int(visit.t_end * fps / 1000) + 1)]
    sheet = Image.new("RGB", (W, H * 3), (0, 0, 0))
    for i, k in enumerate((0, len(frames) // 2, len(frames) * 2 // 3)):
        sheet.paste(frames[k], (0, i * H))
    pal = sheet.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    path = os.path.join(OUT, name + ".gif")
    q[0].save(path, save_all=True, append_images=q[1:], duration=int(1000 / fps), loop=0, optimize=False, disposal=1)
    print(f"  {name}.gif  {len(q)} frames, {os.path.getsize(path) // 1024} KB")


def dino_gif(ctx, fps=12):
    art = _dino()["art"]
    w = art.Walk(7); w.flip = False; w.lane = 312; w.stop_x = 170
    w.t_stop = int((w.stop_x + art.SW) / w.speed * 1000)
    w.t_end = w.t_stop + w.pause_ms + int((art.SCREEN_W - w.stop_x) / w.speed * 1000)
    _critter_gif(ctx, "dino-walk", _dino_frame, w, fps)


def toucan_gif(ctx, fps=15):
    """toucan-flight.gif: a visit left to right with its swoops, the perch, the beak clacking and the bubble."""
    art = _toucan()["art"]
    w = art.Flight(5); w.flip = False; w.lane = 300; w.stop_x = 200
    w._finish()
    _critter_gif(ctx, "toucan-flight", _toucan_frame, w, fps)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/tmp/RobotoMono.ttf")
    ap.add_argument("--sans", default=None)
    ap.add_argument("--out", default=None, help="output directory (default docs/renders)")
    ap.add_argument("--hungry-gif", action="store_true", help="only the hungry critter GIFs")
    ap.add_argument("--gif", action="store_true", help="also write the boot and About animations as GIFs (slow)")
    a = ap.parse_args()
    for sz in (10, 12, 13, 14, 15, 16, 18, 22, 24, 34):
        FONT[sz] = ImageFont.truetype(a.font, sz)
    FONT["s34"] = ImageFont.truetype(a.sans or a.font, 34)   # setup_title: the update card's title
    FONT[50] = ImageFont.truetype(a.sans or a.font, 50)   # title_big: cap height = the F1 logo's 36 px
    global OUT
    if a.out:
        OUT = os.path.abspath(a.out)
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

    cj = json.load(open(os.path.join(SAMP, "jolpica-constructor-standings.json")))
    cl = cj["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"]
    constructors = [dict(pos=int(x["position"]), points=int(float(x["points"])),
                         name=x["Constructor"]["name"],
                         iso3=TEAM_ISO3.get(x["Constructor"].get("nationality", ""), ""))
                    for x in cl]

    sj = json.load(open(os.path.join(SAMP, "jolpica-driver-standings.json")))
    sl = sj["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"]
    standings = [dict(pos=int(x["position"]), points=int(float(x["points"])),
                      wins=int(x["wins"]), name=x["Driver"]["familyName"].upper(),
                      team=x["Constructors"][0]["name"],
                      iso3=next((q["iso3"] for q in drv if q["id"] == x["Driver"]["driverId"]), ""))
                 for x in sl]

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

    G = geometry()
    ctx = dict(circ=(pts, cs, idmap), flags=fl, drivers=drv, legends=leg,
               por=portraits(), facts=facts(), cal=calendar(), grid=grid,
               standings=standings, summary=summary, geo=G,
               constructors=constructors,
               # the real arithmetic, not a made-up sentence
               champ_line=_champ_line(standings))

    # The check the renderer exists for: assert the firmware's own geometry does
    # not collide, before drawing anything from it.
    mx, my, mw, mh = box(G, "map_box")
    tx, ty, tw, th = box(G, "top5")
    bx, by, bw, bh = box(G, "watch_banner")
    probs = []
    if my + mh > ty:
        probs.append(f"map_box {my}-{my+mh} runs into top5 at {ty}")
    if by < my + mh:
        probs.append(f"watch_banner {by}-{by+bh} covers the trace {my}-{my+mh}")
    if by + bh > 432:
        probs.append(f"watch_banner {by}-{by+bh} covers the gear at 432")
    if probs:
        print("GEOMETRY PROBLEMS:", file=sys.stderr)
        for x in probs:
            print("  " + x, file=sys.stderr)
        sys.exit(1)
    print(f"geometry OK: map {my}-{my+mh}, top5 {ty}-{ty+th}, "
          f"banner {by}-{by+bh}, gear 432+")

    pages = [page_wifi_loading, page_wifi_connecting, page_wifi_ap, page_race, page_order, page_circuit, page_driver, page_driver_loading, page_rookie, page_rookie_loading, page_legend,
             page_standings, page_summary, page_settings, page_settings_race,
             page_settings_location, page_about, page_about_cars, page_boot_cars, page_boot_strip, page_overtake, page_debug, page_offseason,
             overlay_detail, overlay_detail_loading, overlay_detail_rookie, overlay_alert, overlay_milestone,
             page_race_with_icon, overlay_update_checking, overlay_update_available,
             overlay_update_installing, overlay_update_failed, page_dino_walks, page_dino_poses, page_toucan_flights, page_toucan_poses, page_hungry_dino, page_hungry_toucan]
    if a.hungry_gif:
        hungry_gifs(ctx)
    if a.gif:
        hungry_gifs(ctx)
        animation_gifs(ctx)
        dino_gif(ctx)
        toucan_gif(ctx)
    made = []
    for fn in pages:
        try:
            img, name = fn(ctx)
        except DriftError as e:
            print(f"DRIFT: {e}", file=sys.stderr)
            sys.exit(1)
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
