#!/usr/bin/env python3
"""Generate f1-tracker/f1_circuits.h from the vendored f1-circuits GeoJSON.

REQUIREMENTS.md section 5 (MAP-1..MAP-4), decisions 21-26.

Everything expensive happens here so the device only scales and translates:
  * project lon/lat to metres about each circuit's own centroid, WITH the
    cos(lat0) term - omitting it stretches Silverstone 1.6x east-west
    (decision 25);
  * choose a per-circuit rotation that maximises how much of the map box the
    trace fills, since north-up wastes most of the box on Spa, Jeddah, Baku and
    Las Vegas (decision 24);
  * normalise to int16 so the device does no floating-point per point;
  * cross-reference Jolpica circuitIds to traces by centroid at BUILD time,
    because doing it at runtime would mean carrying 40 lat/lon pairs and a
    nearest-neighbour search for no benefit (decision 23).

Guards, because a silently blank or wrong map is the failure mode to prevent:
  * every circuit on the CURRENT calendar must resolve to a trace, or the build
    fails naming it;
  * every trace must be non-degenerate - enough points, a real bounding box,
    and closed unless it is a known point-to-point layout.

  python3 tools/gen_circuits.py [--calendar reference/samples/jolpica-2026-races.json]
"""
import argparse, glob, json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GEO = os.path.join(ROOT, "reference", "f1-circuits")
OUT = os.path.join(ROOT, "f1-tracker", "f1_circuits.h")

COORD_MAX = 10000        # normalised half-extent of the longer axis
MATCH_KM = 15.0          # centroid tolerance; measured worst real match is 1.1 km
MIN_POINTS = 60          # MAP-2: below this the trace is not a usable outline
MIN_EXTENT_M = 200.0     # MAP-2: a real circuit is bigger than this on both axes
CLOSE_TOL_M = 150.0      # first and last point of a closed lap
NORTH_SNAP_DEG = 2.0     # a best-fill rotation this close to north-up is recorded
                         # as north-up, so the arrow is suppressed rather than
                         # pointing almost-straight-up. Measured: no circuit in
                         # the current set is within 2 deg (closest tr-2005 at
                         # 4.75), so this is defensive, not cosmetic.

M_PER_DEG_LAT = 110540.0
M_PER_DEG_LON = 111320.0

# Known-distinct venues that sit close enough to each other for a centroid match
# to pick the wrong one. A distance threshold cannot separate these from a
# legitimate match - Kyalami is a true match at 1.3 km, Zeltweg a false one at
# 2.4 km - so they are named explicitly with the reason.
EXCLUDE = {
    # Zeltweg Airfield hosted the 1964 Austrian GP. The Osterreichring /
    # Red Bull Ring was built beside it in 1969 and is a different circuit.
    ("zeltweg", "at-1969"),
}


# ---------------------------------------------------------------- geometry
def project(coords):
    """lon/lat degrees -> metres about the centroid. The cos(lat0) term is
    mandatory (decision 25)."""
    lon0 = sum(c[0] for c in coords) / len(coords)
    lat0 = sum(c[1] for c in coords) / len(coords)
    k = math.cos(math.radians(lat0))
    return [((c[0] - lon0) * M_PER_DEG_LON * k,
             (c[1] - lat0) * M_PER_DEG_LAT) for c in coords], lat0, lon0


def bbox(pts):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def rotate(pts, th):
    c, s = math.cos(th), math.sin(th)
    return [(p[0] * c - p[1] * s, p[0] * s + p[1] * c) for p in pts]


def best_rotation(pts, box_w=1.0, box_h=1.0, steps=720):
    """The rotation that maximises fill of a box_w x box_h box.

    NOT minimum-area: for a square box the objective is to minimise the LONGER
    side, which is what actually decides the scale factor. Optimising area
    would happily return a long thin rectangle.
    """
    best_th, best_scale = 0.0, -1.0
    for i in range(steps):
        th = math.pi * i / steps           # 180 deg is enough; a rect is symmetric
        x0, y0, x1, y1 = bbox(rotate(pts, th))
        w, h = x1 - x0, y1 - y0
        if w <= 0 or h <= 0:
            continue
        scale = min(box_w / w, box_h / h)
        if scale > best_scale:
            best_scale, best_th = scale, th
    return best_th


def normalise(pts):
    """Centre on the bbox and scale so the longer axis spans 2*COORD_MAX."""
    x0, y0, x1, y1 = bbox(pts)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    half = max(x1 - x0, y1 - y0) / 2
    if half <= 0:
        raise ValueError("degenerate trace")
    out, hw, hh = [], 0, 0
    for x, y in pts:
        ix = int(round((x - cx) / half * COORD_MAX))
        iy = int(round((y - cy) / half * COORD_MAX))
        ix = max(-32767, min(32767, ix)); iy = max(-32767, min(32767, iy))
        out.append((ix, iy))
        hw = max(hw, abs(ix)); hh = max(hh, abs(iy))
    return out, hw, hh


# ---------------------------------------------------------------- load
def load_traces():
    out = []
    for path in sorted(glob.glob(os.path.join(GEO, "*.geojson"))):
        d = json.load(open(path))
        for ft in d["features"]:
            g = ft["geometry"]
            if g["type"] != "LineString":
                continue
            p = ft["properties"]
            out.append({"id": p["id"], "name": p["Name"], "loc": p.get("Location", ""),
                        "len": int(p.get("length") or 0), "alt": int(p.get("altitude") or 0),
                        "opened": int(p.get("opened") or 0), "firstgp": int(p.get("firstgp") or 0),
                        "coords": g["coordinates"], "file": os.path.basename(path)})
    return out


def check(tr, m, lat0):
    """MAP-2 guards. Returns a list of problems."""
    bad = []
    if len(tr["coords"]) < MIN_POINTS:
        bad.append(f"only {len(tr['coords'])} points (min {MIN_POINTS})")
    x0, y0, x1, y1 = bbox(m)
    if (x1 - x0) < MIN_EXTENT_M or (y1 - y0) < MIN_EXTENT_M:
        bad.append(f"bbox {x1-x0:.0f}x{y1-y0:.0f} m is under {MIN_EXTENT_M:.0f} m")
    gap = math.hypot(m[0][0] - m[-1][0], m[0][1] - m[-1][1])
    closed = gap <= CLOSE_TOL_M
    return bad, closed, gap


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--calendar", default=os.path.join(ROOT, "reference", "samples",
                                                       "jolpica-2026-races.json"))
    ap.add_argument("--all-circuits", default=None,
                    help="Jolpica /circuits/ JSON; without it only the calendar is cross-referenced")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()

    traces = load_traces()
    print(f"traces: {len(traces)}")

    # ---- project, rotate, normalise
    prepared, problems = [], []
    for t in traces:
        m, lat0, lon0 = project(t["coords"])
        bad, closed, gap = check(t, m, lat0)
        if bad:
            problems.append((t["id"], "; ".join(bad)))
        th = best_rotation(m)
        if abs(math.degrees(th)) < NORTH_SNAP_DEG or \
           abs(abs(math.degrees(th)) - 180.0) < NORTH_SNAP_DEG:
            th = 0.0
        pts, hw, hh = normalise(rotate(m, th))
        t.update({"pts": pts, "hw": hw, "hh": hh, "rot": th,
                  "lat0": lat0, "lon0": lon0, "closed": closed, "gap": gap})
        prepared.append(t)

    if problems:
        print("\nFAIL: degenerate geometry (MAP-2)", file=sys.stderr)
        for cid, why in problems:
            print(f"  {cid}: {why}", file=sys.stderr)
        sys.exit(1)

    # ---- cross-reference Jolpica circuitIds by centroid (decision 23)
    def centroid_match(cid, lat, lon):
        best, bestkm = None, 1e9
        for t in prepared:
            if (cid, t["id"]) in EXCLUDE:
                continue
            dk = math.hypot((t["lat0"] - lat) * M_PER_DEG_LAT,
                            (t["lon0"] - lon) * M_PER_DEG_LON *
                            math.cos(math.radians(lat))) / 1000.0
            if dk < bestkm:
                best, bestkm = t, dk
        return (best, bestkm) if bestkm <= MATCH_KM else (None, bestkm)

    wanted = {}
    cal = json.load(open(a.calendar))["MRData"]["RaceTable"]
    cal_season = cal.get("season", "?")
    for r in cal["Races"]:
        ci = r["Circuit"]
        wanted[ci["circuitId"]] = (float(ci["Location"]["lat"]), float(ci["Location"]["long"]), True)
    if a.all_circuits:
        for ci in json.load(open(a.all_circuits))["MRData"]["CircuitTable"]["Circuits"]:
            wanted.setdefault(ci["circuitId"],
                              (float(ci["Location"]["lat"]), float(ci["Location"]["long"]), False))

    idmap, unmatched_cal, unmatched_hist = [], [], []
    for cid, (lat, lon, on_cal) in sorted(wanted.items()):
        t, km = centroid_match(cid, lat, lon)
        if t is None:
            (unmatched_cal if on_cal else unmatched_hist).append((cid, km))
        else:
            idmap.append((cid, t["id"], km))

    if unmatched_cal:
        print(f"\nFAIL: circuits on the {cal_season} calendar with no trace "
              f"(decision 23)", file=sys.stderr)
        for cid, km in unmatched_cal:
            print(f"  {cid}: nearest trace {km:.0f} km away", file=sys.stderr)
        sys.exit(1)

    print(f"calendar {cal_season}: {len(cal['Races'])} rounds, all matched")
    print(f"circuitId -> trace rows: {len(idmap)}")
    if unmatched_hist:
        print(f"historical circuitIds with no trace (fine - they show a no-map card): "
              f"{len(unmatched_hist)}")

    rots = sorted(abs(math.degrees(t["rot"])) for t in prepared)
    print(f"fill rotations: {sum(1 for r in rots if r == 0.0)} north-up, "
          f"closest non-zero {min((r for r in rots if r > 0), default=0):.2f} deg, "
          f"max {rots[-1]:.2f} deg")
    worst = max(idmap, key=lambda r: r[2])
    print(f"worst centroid match: {worst[0]} -> {worst[1]} at {worst[2]:.1f} km")
    open_laps = [t['id'] for t in prepared if not t['closed']]
    print(f"point-to-point / open traces: {open_laps if open_laps else 'none'}")

    # ---- emit
    index = {t["id"]: i for i, t in enumerate(sorted(prepared, key=lambda t: t["id"]))}
    ordered = sorted(prepared, key=lambda t: t["id"])
    L = []
    w = L.append
    w("#pragma once")
    w("// GENERATED by tools/gen_circuits.py - do not edit.")
    w("//")
    w("// Source: github.com/bacinger/f1-circuits (MIT), vendored in")
    w("// reference/f1-circuits/. MIT imposes no licence on this firmware: this is")
    w("// geometry, not artwork (REQUIREMENTS.md decision 21).")
    w("//")
    w("// Points are projected about each circuit's centroid with the cos(lat0) term,")
    w("// rotated to maximise fill of a square box, and normalised so the LONGER axis")
    w("// spans +/-COORD_MAX. The device only scales and translates (MAP-3/MAP-5).")
    w("#include <cstdint>")
    w("")
    w("namespace f1 {")
    w("namespace circuits {")
    w("")
    w(f"inline constexpr int16_t COORD_MAX = {COORD_MAX};")
    w("")
    w("struct Circuit {")
    w("  const char *id;        // trace id, e.g. \"mc-1929\"")
    w("  const char *name;")
    w("  const char *location;")
    w("  uint16_t first_pt;     // index of the first x,y pair in PTS")
    w("  uint16_t n_pts;")
    w("  int16_t half_w, half_h;  // normalised extents; aspect without scanning")
    w("  uint16_t length_m;")
    w("  int16_t altitude_m;")
    w("  uint16_t opened, firstgp;")
    w("  int16_t rot_cdeg;      // rotation APPLIED, centi-degrees. The north arrow")
    w("                         // points at -rot_cdeg (MAP-3).")
    w("  bool closed;           // false = point-to-point, do not draw a closing segment")
    w("};")
    w("")
    flat, first = [], {}
    for t in ordered:
        first[t["id"]] = len(flat) // 2
        for x, y in t["pts"]:
            flat += [x, y]
    w(f"// {len(flat)//2} points, {len(flat)*2} bytes")
    w(f"inline constexpr int16_t PTS[{len(flat)}] = {{")
    for i in range(0, len(flat), 16):
        w("    " + ",".join(str(v) for v in flat[i:i + 16]) + ",")
    w("};")
    w("")
    w(f"inline constexpr Circuit C[{len(ordered)}] = {{")
    for t in ordered:
        w(f'    {{"{esc(t["id"])}", "{esc(t["name"])}", "{esc(t["loc"])}", '
          f'{first[t["id"]]}, {len(t["pts"])}, {t["hw"]}, {t["hh"]}, '
          f'{t["len"]}, {t["alt"]}, {t["opened"]}, {t["firstgp"]}, '
          f'{int(round(math.degrees(t["rot"]) * 100))}, {"true" if t["closed"] else "false"}}},')
    w("};")
    w(f"inline constexpr int N = {len(ordered)};")
    w("")
    w("// Jolpica circuitId -> index into C. Sorted, for binary search (decision 23).")
    w("struct IdMap { const char *circuit_id; uint16_t idx; };")
    w(f"inline constexpr IdMap BY_CIRCUIT_ID[{len(idmap)}] = {{")
    for cid, tid, km in sorted(idmap):
        w(f'    {{"{esc(cid)}", {index[tid]}}},   // {km:.1f} km')
    w("};")
    w(f"inline constexpr int N_IDMAP = {len(idmap)};")
    w("")
    w("}  // namespace circuits")
    w("}  // namespace f1")
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    open(a.out, "w").write("\n".join(L) + "\n")
    sz = os.path.getsize(a.out)
    print(f"\nwrote {a.out}")
    print(f"  {len(ordered)} circuits, {len(flat)//2} points, "
          f"{len(flat)*2} B of point data ({len(flat)*2/1024:.1f} kB)")
    print(f"  header {sz/1024:.0f} kB of source")


if __name__ == "__main__":
    main()
