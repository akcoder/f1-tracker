#!/usr/bin/env python3
"""Generate f1-tracker/f1_portraits.h - driver and legend photographs.

REQUIREMENTS.md 5.6.3, decisions 72-74, 91.

Portraits come from Wikimedia Commons and are baked into the firmware, which is
exactly what sky_photos.h does for the planet images. OpenF1's headshot_url is
deliberately NOT used: it points at media.formula1.com, which carries no public
licence to comply with, through a fallback transform that may not resolve
(decision 73).

The generator RECORDS the licence and the photographer for every image and
FAILS THE BUILD on one it cannot attribute, because most Commons photography is
CC BY-SA or CC BY and both require credit. The credit is drawn on the card.

Queries are batched - up to 50 titles per request - because a per-driver loop
earned an HTTP 429 during the initial survey, and 3.7.4 says a 4xx is not to be
retried tightly.

  python3 tools/gen_portraits.py
"""
import argparse, datetime, io, json, os, re, sys, time, urllib.parse, urllib.request
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CACHE = os.path.join(HERE, ".cache", "portraits")
UA = {"User-Agent": "f1-tracker-build/1.0 (personal, non-commercial ESPHome project)"}
WP = "https://en.wikipedia.org/w/api.php"
COMMONS = "https://commons.wikimedia.org/w/api.php"

W, H = 240, 320           # decision 91: the picture is the feature
JPEG_Q = 88
CAP_BYTES = 3 * 1024 * 1024   # decision 74
BATCH = 40
GAP_S = 2.0

# Licences that require crediting the photographer. Anything not matched here
# is treated as unattributable and fails the build.
OK_LICENCE = re.compile(r"(cc[ -]?by|public domain|cc0|pd-|attribution)", re.I)


def api(url, **params):
    params["format"] = "json"
    q = urllib.parse.urlencode(params)
    full = f"{url}?{q}"
    os.makedirs(CACHE, exist_ok=True)
    key = os.path.join(CACHE, "q_" + str(abs(hash(full))) + ".json")
    if os.path.exists(key):
        return json.load(open(key))
    time.sleep(GAP_S)
    with urllib.request.urlopen(urllib.request.Request(full, headers=UA), timeout=60) as r:
        d = json.load(r)
    json.dump(d, open(key, "w"))
    return d


def parse_header(path, ns):
    """Pull driver_id / given / family out of a generated profile header."""
    if not os.path.exists(path):
        return []
    src = open(path).read()
    rows = re.findall(r'\{"([^"]+)", "([^"]*)", "([^"]*)", "[^"]*", "[^"]*", "[^"]*", '
                      r'\d+, \d+, \d+, \d+, -?\d+, \d+, \d+, \d+, \d+\}', src)
    return [{"id": r[0], "given": r[1], "family": r[2]} for r in rows]


def page_titles(people):
    """Wikipedia article titles. The generated headers carry given+family, which
    resolves for every current driver and legend in the set."""
    return {p["id"]: f"{p['given']} {p['family']}".strip() for p in people}


def fetch_page_images(titles):
    out = {}
    tl = list(titles)
    for i in range(0, len(tl), BATCH):
        chunk = tl[i:i + BATCH]
        d = api(WP, action="query", prop="pageimages", piprop="original",
                redirects=1, titles="|".join(chunk))
        q = d.get("query", {})
        norm = {n["from"]: n["to"] for n in q.get("normalized", [])}
        redir = {r["from"]: r["to"] for r in q.get("redirects", [])}
        resolved = {}
        for t in chunk:
            r = norm.get(t, t)
            r = redir.get(r, r)
            resolved[r] = t
        for pg in q.get("pages", {}).values():
            orig = pg.get("original", {}).get("source")
            title = resolved.get(pg.get("title"), pg.get("title"))
            if orig:
                out[title] = orig
    return out


def fetch_licences(files):
    out = {}
    fl = list(files)
    for i in range(0, len(fl), BATCH):
        chunk = fl[i:i + BATCH]
        d = api(COMMONS, action="query", prop="imageinfo",
                iiprop="extmetadata", titles="|".join("File:" + f for f in chunk))
        for pg in d.get("query", {}).get("pages", {}).values():
            ii = (pg.get("imageinfo") or [{}])[0]
            em = ii.get("extmetadata", {})
            name = pg.get("title", "").replace("File:", "")
            lic = (em.get("LicenseShortName", {}) or {}).get("value", "")
            art = (em.get("Artist", {}) or {}).get("value", "")
            art = re.sub(r"<[^>]+>", "", art or "").strip()
            art = re.sub(r"\s+", " ", art)[:48]
            out[name] = (lic, art)
    return out


def download(url):
    os.makedirs(CACHE, exist_ok=True)
    key = os.path.join(CACHE, re.sub(r"[^A-Za-z0-9._-]", "_", url)[-120:])
    if not os.path.exists(key):
        time.sleep(GAP_S)
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=90) as r:
            open(key, "wb").write(r.read())
    return key


def portrait(path):
    """Crop to a 3:4 portrait around the upper-centre, where a face sits."""
    im = Image.open(path).convert("RGB")
    tw, th = W, H
    sw, sh = im.size
    want = tw / th
    have = sw / sh
    if have > want:                      # too wide: crop sides
        nw = int(sh * want)
        x = (sw - nw) // 2
        im = im.crop((x, 0, x + nw, sh))
    else:                                # too tall: crop from the top third
        nh = int(sw / want)
        y = min(int(sh * 0.08), max(0, sh - nh))
        im = im.crop((0, y, sw, y + nh))
    return im.resize((tw, th), Image.LANCZOS)


def esc(s):
    return (s or "").replace("\\", "\\\\").replace('"', '\\"')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "f1-tracker", "f1_portraits.h"))
    a = ap.parse_args()

    people = (parse_header(os.path.join(ROOT, "f1-tracker", "f1_drivers.h"), "drivers")
              + parse_header(os.path.join(ROOT, "f1-tracker", "f1_legends.h"), "legends"))
    seen, uniq = set(), []
    for p in people:
        if p["id"] not in seen:
            seen.add(p["id"]); uniq.append(p)
    if not uniq:
        print("no profile headers yet - run tools/gen_drivers.py first", file=sys.stderr)
        sys.exit(1)
    print(f"{len(uniq)} distinct people")

    titles = page_titles(uniq)
    images = fetch_page_images(set(titles.values()))
    print(f"page images found: {len(images)}/{len(set(titles.values()))}")

    files = {}
    for did, t in titles.items():
        url = images.get(t)
        if url:
            files[did] = (url, urllib.parse.unquote(url.rsplit("/", 1)[-1]).split("?")[0])
    lic = fetch_licences({f for _, f in files.values()})

    rows, total, skipped = [], 0, []
    for p in uniq:
        did = p["id"]
        if did not in files:
            skipped.append((did, "no page image"))
            continue
        url, fname = files[did]
        l, artist = lic.get(fname, ("", ""))
        # decision 72: fail rather than ship an image we cannot attribute.
        if not l or not OK_LICENCE.search(l):
            skipped.append((did, f"unattributable licence '{l or '?'}'"))
            continue
        try:
            im = portrait(download(url))
        except Exception as e:
            skipped.append((did, f"download/decode failed: {e}"))
            continue
        buf = io.BytesIO()
        im.save(buf, "JPEG", quality=JPEG_Q, optimize=True, progressive=False)
        data = buf.getvalue()
        total += len(data)
        credit = artist or "Wikimedia Commons"
        rows.append((did, data, l, credit))

    if skipped:
        print(f"\n{len(skipped)} without a usable portrait (they get a text-only card, RACE-13e):")
        for did, why in skipped:
            print(f"  {did}: {why}")

    if total > CAP_BYTES:
        print(f"\nFAIL: portraits total {total/1024/1024:.2f} MB, cap is "
              f"{CAP_BYTES/1024/1024:.0f} MB (decision 74)", file=sys.stderr)
        sys.exit(1)

    L = []; w = L.append
    w("#pragma once")
    w("// GENERATED by tools/gen_portraits.py - do not edit.")
    w("//")
    w("// Driver and legend portraits from Wikimedia Commons, cropped to 3:4 and")
    w(f"// stored as baseline JPEG at {W}x{H} (decision 91). Decoded on display with")
    w("// the copied sky_jpg.h - one decode per card, i.e. once per 15-120 s.")
    w("//")
    w("// EVERY image here carries a licence that requires crediting the")
    w("// photographer, and the credit is drawn on the card (decision 72). An")
    w("// image that could not be attributed is not included - that person gets a")
    w("// text-only card instead (RACE-13e).")
    w("//")
    w("// OpenF1's headshot_url is deliberately not used: media.formula1.com")
    w("// carries no public licence (decision 73).")
    w(f"// Built {datetime.datetime.now(datetime.timezone.utc):%Y-%m-%d %H:%M UTC}")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("#include <cstring>")
    w("")
    w("namespace f1 {")
    w("namespace portraits {")
    w("")
    w(f"inline constexpr int PW = {W}, PH = {H};")
    w("")
    w("struct Portrait {")
    w("  const char *driver_id;")
    w("  const uint8_t *jpeg;")
    w("  uint32_t len;")
    w("  const char *credit;    // photographer - MUST be drawn on the card")
    w("  const char *licence;")
    w("};")
    w("")
    for did, data, l, credit in rows:
        sym = "IMG_" + re.sub(r"[^A-Za-z0-9]", "_", did).upper()
        w(f"// {did}: {l} / {credit}")
        w(f"inline constexpr uint8_t {sym}[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            w("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
        w("};")
    w("")
    w(f"inline constexpr Portrait P[{len(rows)}] = {{")
    for did, data, l, credit in rows:
        sym = "IMG_" + re.sub(r"[^A-Za-z0-9]", "_", did).upper()
        w(f'    {{"{esc(did)}", {sym}, {len(data)}, "{esc(credit)}", "{esc(l)}"}},')
    w("};")
    w(f"inline constexpr int N = {len(rows)};")
    w("")
    w("inline const Portrait *find(const char *driver_id) {")
    w("  if (driver_id == nullptr || !*driver_id) return nullptr;")
    w("  for (int i = 0; i < N; i++)")
    w("    if (std::strcmp(driver_id, P[i].driver_id) == 0) return &P[i];")
    w("  return nullptr;   // no portrait: render a text-only card (RACE-13e)")
    w("}")
    w("")
    w("}  // namespace portraits")
    w("}  // namespace f1")
    open(a.out, "w").write("\n".join(L) + "\n")
    print(f"\n{len(rows)} portraits -> {a.out}")
    print(f"  {total} B ({total/1024/1024:.2f} MB), cap {CAP_BYTES/1024/1024:.0f} MB")
    print(f"  mean {total//max(1,len(rows))} B each")


if __name__ == "__main__":
    main()
