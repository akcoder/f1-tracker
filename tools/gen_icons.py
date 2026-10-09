#!/usr/bin/env python3
"""Emit the MDI codepoint list for the settings page, from MDI's own metadata.

Guessing a codepoint gives a blank glyph that compiles, flashes and only shows
up on a screen - the same silent failure class as a missing font glyph
(decision 52). So the list is generated from @mdi/svg's meta.json by NAME, and
the generator fails if a name does not exist.

  python3 tools/gen_icons.py            # the YAML glyphs: list
  python3 tools/gen_icons.py --check    # verify every name resolves
"""
import argparse, json, os, sys, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CACHE = os.path.join(HERE, ".cache", "mdi-meta.json")
URL = "https://cdn.jsdelivr.net/npm/@mdi/svg@7.4.47/meta.json"

# Every icon the device draws, with what it labels. Kept here so the YAML's
# glyph list and the C++ that uses it cannot disagree about which is which.
ICONS = {
    "auto_dim":      ("brightness-auto",   "Auto-dim display"),
    "auto_off":      ("power-sleep",       "Auto off"),
    "brightness":    ("brightness-6",      "Brightness"),
    "carousel_secs": ("timer-outline",     "Carousel interval"),
    "carousel_what": ("view-carousel",     "Carousel content"),
    "carousel_order":("sort",              "Carousel order"),
    "watched":       ("account-star",      "Watched driver"),
    "alerts":        ("bell-ring",         "Watched driver alerts"),
    "milestones":    ("trophy-outline",    "Milestones only"),
    "clock":         ("clock-outline",     "Clock format"),
    "latitude":      ("latitude",          "Latitude"),
    "longitude":     ("longitude",         "Longitude"),
    "practice":      ("timer-sand",        "Show practice sessions"),
    "sprint":        ("flash",             "Show sprint sessions"),
    "columns":       ("table-column",      "Order columns"),
    "force_page":    ("page-next-outline", "Force page"),
    "team":          ("shield-star",       "Favourite team"),
    "night":         ("weather-night",     "Night"),
    "update":        ("cloud-download",    "Check for updates"),
    "tab_display":   ("monitor",           "Settings tab: Display"),
    "tab_race":      ("flag-checkered",    "Settings tab: Race"),
    "tab_location":  ("map-marker",        "Settings tab: Location"),
    "about":         ("information-outline", "About"),
    "save":          ("content-save",      "Save"),
    "cancel":        ("close",             "Cancel / Close"),
    "sun":           ("white-balance-sunny", "Sun elevation"),
    "device":        ("chip",              "This device"),
    "ip":            ("ip-network",        "IP address"),
    "ssid":          ("wifi",              "Connected SSID"),
    "signal":        ("wifi-strength-3",   "Wi-Fi signal"),
    "uptime":        ("timer-outline",     "Uptime"),
}


def meta():
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    if not os.path.exists(CACHE):
        with urllib.request.urlopen(URL, timeout=90) as r:
            open(CACHE, "wb").write(r.read())
    return {x["name"]: x["codepoint"] for x in json.load(open(CACHE))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    m = meta()
    bad = [n for n, _ in ICONS.values() if n not in m]
    if bad:
        print(f"FAIL: no such MDI icon: {bad}", file=sys.stderr)
        sys.exit(1)
    if a.check:
        print(f"all {len(ICONS)} icon names resolve in MDI 7.4.47")
        return
    cps = sorted({m[n] for n, _ in ICONS.values()})
    print("    glyphs: [" + ", ".join(f'"\\\\U000{c.upper()}"' for c in cps) + "]")
    print()
    for key, (name, label) in sorted(ICONS.items()):
        print(f'  // {key:15} {name:18} {label}')
        print(f'  inline constexpr const char *IC_{key.upper()} = "\\\\U000{m[name].upper()}";')


if __name__ == "__main__":
    main()
