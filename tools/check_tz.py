#!/usr/bin/env python3
"""Check f1-tracker/f1_tz.h's zone table against the IANA database (zoneinfo): for each zone, the
standard and daylight offsets (seconds west of UTC), whether it has daylight time, and the month it
starts and ends in. The IANA name is in each row's comment."""
import os, re, sys, datetime as dt
from zoneinfo import ZoneInfo

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "f1-tracker", "f1_tz.h")
H = 3600


def rows():
    out = []
    for line in open(SRC):
        m = re.match(r"\s*\{([^}]*?)(?:, (R\([^)]*\)|NO), (R\([^)]*\)|NO))?\},\s*// (.+?)\s+(\S+)\s*$", line)
        if not m or "unused" in line:
            continue
        a, s, e, label, iana = m.groups()
        std, dst = (eval(x, {"H": H}) for x in a.split(",")[:2])
        month = lambda r: None if r in (None, "NO") else int(re.match(r"R\((\d+)", r).group(1))
        out.append((label.strip(), iana, std, dst, month(s), month(e)))
    return out


def main():
    bad = 0
    for label, iana, std, dst, sm, em in rows():
        z = ZoneInfo(iana)
        offs = {}
        for d in range(1, 366, 3):
            t = dt.datetime(2026, 1, 1, 12, tzinfo=dt.timezone.utc) + dt.timedelta(days=d - 1)
            offs[t] = -int(t.astimezone(z).utcoffset().total_seconds())
        lo, hi = max(offs.values()), min(offs.values())      # standard = more west, daylight = less
        ok = (std == lo and dst == hi)
        if dst != std:                                        # the month of each change, in local standard time
            prev = None; changes = []
            for t, o in offs.items():
                if prev is not None and o != prev[1]:
                    changes.append((t.month, "start" if o == hi else "end"))
                prev = (t, o)
            starts = {m for m, k in changes if k == "start"}; ends = {m for m, k in changes if k == "end"}
            ok &= (sm in starts or sm in {x - 1 for x in starts} or sm in {x + 1 for x in starts}) and (em in ends or em in {x - 1 for x in ends} or em in {x + 1 for x in ends})
        else:
            ok &= sm is None
        print(("ok   " if ok else "FAIL ") + f"{label:15s} {iana:22s} std {std / H:+.2f} dst {dst / H:+.2f} start {sm} end {em}")
        bad += not ok
    print(f"{len(rows())} zones, {bad} wrong")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
