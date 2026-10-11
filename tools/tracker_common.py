"""Find a file of tracker-common (github.com/akcoder/tracker-common): $TRACKER_COMMON, a checkout
beside this repo, or the copy ESPHome fetched for the last build."""
import glob
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find(rel):
    cands = [os.path.join(os.environ["TRACKER_COMMON"], rel)] if os.environ.get("TRACKER_COMMON") else []
    cands.append(os.path.join(ROOT, "..", "tracker-common", rel))
    cands += sorted(glob.glob(os.path.join(ROOT, ".esphome", "external_components", "*", rel)))
    cands += sorted(glob.glob(os.path.join(ROOT, ".esphome", "packages", "*", os.path.basename(rel))))
    for c in cands:
        if os.path.isfile(c):
            return c
    raise FileNotFoundError(f"{rel} not found: check out tracker-common beside this repo, or set TRACKER_COMMON")
