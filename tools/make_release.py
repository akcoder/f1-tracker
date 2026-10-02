#!/usr/bin/env python3
"""Build the release artefacts: the update manifest and the web installer.

"Made for ESPHome" asks for updates over the internet from a publicly reachable
JSON manifest, and for a web installer so the device can be flashed from a
browser. One manifest serves both, because ESP Web Tools reads `builds[].parts`
and ESPHome's own updater reads `builds[].ota` from the same file.

The `md5` is NOT optional: the updater refuses a manifest without one, and it is
what stops a half-downloaded image being written to flash.

  python3 tools/make_release.py            # from the last esphome compile
"""
import argparse, hashlib, json, os, shutil, sys, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, ".esphome", "build", "f1-tracker", "build")
DOCS = os.path.join(ROOT, "docs")

CHIP = "ESP32-S3"


def version():
    s = open(os.path.join(ROOT, "f1-tracker.yaml"), encoding="utf-8").read()
    m = re.search(r'fw_version:\s*"([^"]+)"', s)
    return m.group(1) if m else "0.0.0"


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default="akcoder/f1-tracker")
    a = ap.parse_args()

    factory = os.path.join(BUILD, "firmware.factory.bin")
    ota = os.path.join(BUILD, "firmware.ota.bin")
    for p in (factory, ota):
        if not os.path.exists(p):
            print(f"missing {p} - run `esphome compile f1-tracker.yaml` first",
                  file=sys.stderr)
            sys.exit(1)

    os.makedirs(DOCS, exist_ok=True)
    v = version()
    fac_name, ota_name = f"f1-tracker-{v}.factory.bin", f"f1-tracker-{v}.ota.bin"
    shutil.copy2(factory, os.path.join(DOCS, fac_name))
    shutil.copy2(ota, os.path.join(DOCS, ota_name))

    manifest = {
        "name": "F1 Tracker",
        "version": v,
        "home_assistant_domain": "esphome",
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": CHIP,
            # ESP Web Tools reads this, for flashing from a browser
            "parts": [{"path": fac_name, "offset": 0}],
            # ESPHome's own updater reads this. md5 is required - it is what
            # stops a half-downloaded image reaching flash.
            "ota": {
                "path": ota_name,
                "md5": md5(ota),
                "summary": "Formula 1 season tracker for the Guition "
                           "ESP32-4848S040",
                "release_url": f"https://github.com/{a.repo}/releases",
            },
        }],
    }
    with open(os.path.join(DOCS, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    print(f"version {v}")
    print(f"  {fac_name}  {os.path.getsize(factory)/1024/1024:.2f} MB")
    print(f"  {ota_name}  {os.path.getsize(ota)/1024/1024:.2f} MB")
    print(f"  md5 {manifest['builds'][0]['ota']['md5']}")
    print(f"  manifest -> docs/manifest.json")


if __name__ == "__main__":
    main()
