#!/usr/bin/env python3
"""Verify that every character the device can draw exists in the font drawing it.

REQUIREMENTS.md decisions 36, 51 and 52. plane-tracker shipped a clock-only
glyph set on mono24 and the OTA panel drew "UPGRADING" as a row of boxes - the
bug is silent, survives compilation, and only appears on a screen.

This checks three sources, because the risk is spread across all of them:

  1. literal `text:` values in the YAML, and string literals in the C++ headers;
  2. every string the GENERATED tables can put on screen - circuit names,
     driver and legend names, the curated prose, facts, the calendar. That is
     where decision 52 says the real risk is, because nobody reads those files;
  3. OTA panel wording specifically, since that is the exact string
     plane-tracker lost.

A font's effective set is the glyphsets it requests INTERSECTED with what the
typeface actually provides: Roboto Mono is missing all 12 of GF_Latin_Core's
combining marks, so asking for them does not mean having them (UI-40c).

  python3 tools/check_glyphs.py
"""
import os, re, sys, unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "f1-tracker")
YAML = os.path.join(ROOT, "f1-tracker.yaml")

try:
    from fontTools.ttLib import TTFont
except ImportError:
    TTFont = None


def requested_glyphsets():
    """Which glyphsets each font block asks for, from the YAML."""
    s = open(YAML, encoding="utf-8").read()
    block = re.search(r"\nfont:\n(.*?)\n[a-z_]+:\n", s, re.S)
    if not block:
        return {}
    out, cur = {}, None
    for line in block.group(1).split("\n"):
        m = re.match(r"\s+id:\s*(\w+)", line)
        if m:
            cur = m.group(1)
        g = re.search(r"glyphsets:\s*(?:&\w+\s*)?\[([^\]]*)\]", line)
        if g and cur:
            out[cur] = [x.strip() for x in g.group(1).split(",") if x.strip()]
        if re.search(r"glyphsets:\s*\*\w+", line) and cur:
            out[cur] = ["GF_Latin_Core"]          # the shared anchor
        if re.search(r"^\s+glyphs:\s*\[", line) and cur:
            out[cur] = ["__explicit__"]
    return out


def glyphset_codepoints(names):
    sys.path.insert(0, os.path.join(ROOT, ".venv", "lib"))
    try:
        import esphome.components.font as f
        cps = set()
        for n in names:
            cps |= set(f.glyphsets.unicodes_per_glyphset(n))
        return cps
    except Exception as e:
        print(f"  (could not load esphome glyphsets: {e})", file=sys.stderr)
        return set()


def font_cmap(path):
    if TTFont is None or not os.path.exists(path):
        return None
    try:
        return set(TTFont(path, fontNumber=0).getBestCmap().keys())
    except Exception:
        return None


# ------------------------------------------------------------------ strings
def yaml_strings():
    """Each literal `text:` paired with the font that will actually draw it.

    decision 52: resolving the EFFECTIVE font is the whole point. Checking every
    string against one shared set is what let plane-tracker's UPGRADING bug
    through - and the first version of THIS script made the same mistake in the
    other direction, flagging the gear symbol because it was measured against
    Roboto Mono rather than against montserrat_28, which actually draws it.
    """
    s = open(YAML, encoding="utf-8").read()
    lines = s.split("\n")
    out = []
    for i, line in enumerate(lines):
        m = re.match(r'\s*text:\s*"((?:[^"\\]|\\.)*)"', line)
        if not m:
            continue
        indent = len(line) - len(line.lstrip())
        font = None
        # text_font: may sit either side of text: within the same widget block
        for j in list(range(i + 1, min(i + 10, len(lines)))) + list(range(i - 1, max(i - 10, -1), -1)):
            l2 = lines[j]
            if not l2.strip():
                continue
            ind2 = len(l2) - len(l2.lstrip())
            if ind2 < indent:
                break
            f = re.match(r"\s*text_font:\s*(\w+)", l2)
            if f:
                font = f.group(1)
                break
        out.append((f"yaml text ({font or 'inherited'})", m.group(1), font))
    # strings written into labels from lambdas
    for m in re.finditer(r'lv_label_set_text\([^,]+,\s*"((?:[^"\\]|\\.)*)"', s):
        out.append(("yaml lambda", m.group(1), None))
    for m in re.finditer(r'snprintf\([^;]*?"((?:[^"\\]|\\.)*)"', s, re.S):
        out.append(("yaml snprintf", m.group(1), None))
    return out


def cpp_strings():
    out = []
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith(".h"):
            continue
        # The generated data files are checked separately and exhaustively.
        if fn in ("f1_portraits.h",):
            continue
        src = open(os.path.join(SRC, fn), encoding="utf-8", errors="replace").read()
        src = re.sub(r"//[^\n]*", "", src)        # comments are not drawn
        src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', src):
            v = m.group(1)
            if v and not v.startswith("#") and not v.startswith("f1_"):
                out.append((fn, v, None))
    return out


def generated_strings():
    """Everything the generated tables can put on a screen (decision 52)."""
    out = []

    def rows(fn, pattern, idxs, label):
        p = os.path.join(SRC, fn)
        if not os.path.exists(p):
            return
        src = open(p, encoding="utf-8").read()
        for m in re.finditer(pattern, src):
            for i in idxs:
                v = m.group(i)
                if v:
                    out.append((label, v, None))

    rows("f1_circuits.h", r'\{"([^"]+)", "([^"]*)", "([^"]*)", \d+,', (2, 3), "circuit")
    rows("f1_calendar.h", r'\{\d+, "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)",',
         (1, 3, 5), "calendar")
    for fn, lab in (("f1_drivers.h", "driver"), ("f1_legends.h", "legend")):
        rows(fn, r'\{"[a-z_0-9]+", "([^"]*)", "([^"]*)", "([^"]*)", "[^"]*", "[^"]*", "([^"]*)"',
             (1, 2, 3, 4), lab)
    rows("f1_facts.h", r'\{"[a-z_0-9]+", \d+, "([^"]*)", "([^"]*)", \d+, "([^"]*)", "([^"]*)", "([^"]*)", \d+, "([^"]*)"',
         (1, 2, 3, 4, 5, 6), "fact")
    p = os.path.join(SRC, "f1_portraits.h")
    if os.path.exists(p):
        for m in re.finditer(r'\{"[a-z_0-9]+", \w+, \d+, "([^"]*)", "([^"]*)"\}',
                             open(p, encoding="utf-8").read()):
            out.append(("credit", m.group(1), None))
            out.append(("licence", m.group(2), None))
    return out


def unescape(v):
    # \UXXXXXXXX and \uXXXX go first, so an icon codepoint becomes ONE character
    # rather than the dozen ASCII characters that spell its escape - otherwise
    # the checker reports a missing 'F' in a font that only ever draws a wifi
    # glyph, which is a false positive that would train people to ignore it.
    v = re.sub(r"\\U([0-9A-Fa-f]{8})", lambda m: chr(int(m.group(1), 16)), v)
    v = re.sub(r"\\u([0-9A-Fa-f]{4})", lambda m: chr(int(m.group(1), 16)), v)
    return (v.replace("\\n", "\n").replace("\\t", "\t").replace('\\"', '"')
             .replace("\\\\", "\\"))


IGNORE = set("\n\t\r")
# Format specifiers and LVGL symbols are not literal content.
SKIP_PATTERNS = (re.compile(r"^[%\-0-9.lu*]*[sdfuxcp%]$"),)


def main():
    req = requested_glyphsets()
    shared = [n for f, n in req.items() if n != ["__explicit__"]]
    names = sorted({n for ns in shared for n in ns}) or ["GF_Latin_Core"]
    want = glyphset_codepoints(names)
    print(f"glyphsets requested: {', '.join(names)}  ({len(want)} codepoints)")

    mono = font_cmap("/tmp/RobotoMono.ttf")
    if mono:
        have = want & mono
        missing_from_face = sorted(want - mono)
        print(f"Roboto Mono provides {len(have)} of them; "
              f"{len(missing_from_face)} are not in the typeface")
        if missing_from_face:
            cats = {unicodedata.category(chr(c)) for c in missing_from_face}
            print(f"  (all {sorted(cats)} - combining marks, UI-40c)")
    else:
        have = want
        print("fontTools or the font file is unavailable; checking against the "
              "requested set only (install fonttools for the exact answer)")

    strings = yaml_strings() + cpp_strings() + generated_strings()
    print(f"\nchecking {len(strings)} strings from the YAML, the C++ headers and "
          f"the generated tables")

    # Fonts that are NOT part of the shared Latin set, with what they do carry.
    # montserrat_28 is an LVGL built-in that ships the LV_SYMBOL range; the mdi_*
    # faces carry only the codepoints their `glyphs:` list names.
    SYMBOL_FONTS = {
        "montserrat_28": set(range(0xF000, 0xF900)) | set(range(0x20, 0x7F)),
    }
    for fid, names in req.items():
        if names == ["__explicit__"]:
            m = re.search(r"id:\s*" + fid + r"\b.*?glyphs:\s*\[([^\]]*)\]",
                          open(YAML, encoding="utf-8").read(), re.S)
            cps = set()
            if m:
                for g in re.finditer(r"\\U([0-9A-Fa-f]{8})", m.group(1)):
                    cps.add(int(g.group(1), 16))
            SYMBOL_FONTS[fid] = cps

    bad = {}
    checked_chars = set()
    for item in strings:
        where, raw = item[0], item[1]
        font = item[2] if len(item) > 2 else None
        v = unescape(raw)
        if any(p.match(raw) for p in SKIP_PATTERNS):
            continue
        for ch in v:
            if ch in IGNORE:
                continue
            cp = ord(ch)
            if cp < 0x20:
                continue
            checked_chars.add(ch)
            allowed = SYMBOL_FONTS.get(font, have) if font in SYMBOL_FONTS else have
            if cp not in allowed:
                bad.setdefault(ch, []).append((where, v[:54]))

    print(f"distinct characters drawn: {len(checked_chars)}")
    nonascii = sorted(c for c in checked_chars if ord(c) > 127)
    if nonascii:
        print(f"non-ASCII in use: {''.join(nonascii)}")

    # decision 52 / plane-tracker's exact bug: the OTA panel's words.
    ota = [i[1] for i in strings if "UPLOAD" in i[1] or "UPGRAD" in i[1]]
    print(f"\nOTA panel wording found: {sorted(set(ota))}")
    missing_ota = {c for v in ota for c in v if ord(c) >= 0x20 and ord(c) not in have}
    print(f"  all characters present: {'YES' if not missing_ota else 'NO ' + repr(missing_ota)}")

    if bad:
        print(f"\nFAIL: {len(bad)} character(s) would draw as a box:")
        for ch, uses in sorted(bad.items()):
            nm = unicodedata.name(ch, "?")
            print(f"  U+{ord(ch):04X} {ch!r} {nm}")
            for where, sample in uses[:3]:
                print(f"      {where}: {sample}")
        return 1

    print("\nOK: every character in every drawable string is in the font.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
