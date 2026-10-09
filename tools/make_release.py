#!/usr/bin/env python3
"""Publish a release. The two consumers need different things, and testing
showed they cannot share one home.

THE DEVICE UPDATER -> GitHub Release assets.
  ESPHome's updater does NOT accept an absolute URL in `builds[].ota.path`: it
  always merges the path with the MANIFEST's own URL
  (http_request_update.cpp). So the manifest has to sit BESIDE the binary, not
  on Pages pointing at it. GitHub's `/releases/latest/download/<asset>` is a
  stable 302 to the newest release, which gives a fixed `source:` that always
  resolves to the current version - provided asset names carry NO version, or
  the relative path would change every release.

THE BROWSER INSTALLER -> GitHub Pages.
  MEASURED: GitHub Releases send no `access-control-allow-origin` on any hop of
  the download redirect chain, so a page on akcoder.github.io CANNOT fetch a
  release asset. ESP Web Tools runs in the browser, so the factory image has to
  stay same-origin on Pages.

Net effect: the OTA image and the manifest leave the repo entirely, and only the
factory image remains - halving what a release costs in history, rather than
eliminating it. Dropping the web installer would eliminate it.

  python3 tools/make_release.py              # build the manifests only
  python3 tools/make_release.py --publish    # ... and create the GitHub release
"""
import argparse, hashlib, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, ".esphome", "build", "f1-tracker", "build")
DOCS = os.path.join(ROOT, "docs")
CHIP = "ESP32-S3"

# Versionless, so /releases/latest/download/<name> is stable and the manifest's
# relative paths keep resolving.
FACTORY = "f1-tracker.factory.bin"
OTA = "f1-tracker.ota.bin"


def version():
    s = open(os.path.join(ROOT, "f1-tracker.yaml"), encoding="utf-8").read()
    m = re.search(r'fw_version:\s*"([^"]+)"', s)
    return m.group(1) if m else "0.0.0"


def release_notes(path):
    """What the device's "Update available" card shows (UI-68c).

    The manifest's ota.summary is the release text, and the panel shows ~1.5 KB of
    it, so it is written for a small screen: short lines, no tables. Taken from a
    file when given, otherwise the commit subjects since the previous tag - which
    is what changed, in the author's own words, with nothing to forget to write.
    """
    if path:
        return open(path, encoding="utf-8").read().strip()
    last = subprocess.run(["git", "describe", "--tags", "--abbrev=0"], cwd=ROOT,
                          capture_output=True, text=True)
    rng = f"{last.stdout.strip()}..HEAD" if last.returncode == 0 else "HEAD"
    log = subprocess.run(["git", "log", "--no-merges", "--pretty=- %s", rng], cwd=ROOT,
                         capture_output=True, text=True).stdout.strip()
    return log or "Formula 1 season tracker for the Guition ESP32-4848S040"


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default="akcoder/f1-tracker")
    ap.add_argument("--publish", action="store_true")
    ap.add_argument("--notes", help="file with the release notes (Markdown); default: the commit "
                                    "subjects since the previous tag")
    a = ap.parse_args()

    factory = os.path.join(BUILD, "firmware.factory.bin")
    ota = os.path.join(BUILD, "firmware.ota.bin")
    for p in (factory, ota):
        if not os.path.exists(p):
            print(f"missing {p} - run ./deploy.sh first", file=sys.stderr)
            sys.exit(1)

    v = version()
    tag = f"v{v}"
    summary = release_notes(a.notes)
    if len(summary) > 1500:       # the card shows ~1.5 KB; say so rather than cut mid-word
        print(f"  notes are {len(summary)} bytes; the device shows the first ~1500", file=sys.stderr)
    base = f"https://github.com/{a.repo}/releases/latest/download"

    manifest = {
        "name": "F1 Tracker",
        "version": v,
        "home_assistant_domain": "esphome",
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": CHIP,
            # Relative, so ESPHome's updater resolves them against this
            # manifest's own URL - which is the release, beside the binaries.
            "parts": [{"path": FACTORY, "offset": 0}],
            "ota": {
                "path": OTA,
                "md5": md5(ota),      # required; the updater refuses without it
                "summary": summary,
                "release_url": f"https://github.com/{a.repo}/releases/tag/{tag}",
            },
        }],
    }

    work = tempfile.mkdtemp(prefix="f1rel-")
    shutil.copy2(factory, os.path.join(work, FACTORY))
    shutil.copy2(ota, os.path.join(work, OTA))
    with open(os.path.join(work, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    # The installer's manifest and factory image stay on Pages, same-origin,
    # because release assets send no CORS headers (see the module docstring).
    web = json.loads(json.dumps(manifest))
    web["builds"][0].pop("ota", None)
    os.makedirs(DOCS, exist_ok=True)
    shutil.copy2(factory, os.path.join(DOCS, FACTORY))
    with open(os.path.join(DOCS, "manifest.json"), "w") as f:
        json.dump(web, f, indent=2)
        f.write("\n")

    print(f"version {v}  tag {tag}")
    print(f"  {FACTORY}  {os.path.getsize(factory)/1024/1024:.2f} MB")
    print(f"  {OTA}      {os.path.getsize(ota)/1024/1024:.2f} MB")
    print(f"  md5 {manifest['builds'][0]['ota']['md5']}")
    print(f"  device updater  -> {base}/manifest.json  (release asset)")
    print(f"  web installer   -> docs/manifest.json + docs/{FACTORY}  (Pages, "
          f"same-origin: releases send no CORS)")

    if not a.publish:
        print(f"\nnot published. Assets staged in {work}")
        return

    notes = (f"Firmware {v} for the Guition ESP32-4848S040.\n\n{summary}\n\n"
             f"- Install from a browser: https://akcoder.github.io/f1-tracker/\n"
             f"- Devices already running F1 Tracker pick this up on their hourly "
             f"update check, or from the *Check for updates* button on the "
             f"settings screen.\n")
    subprocess.run(["gh", "release", "view", tag, "--repo", a.repo],
                   capture_output=True)
    exists = subprocess.run(["gh", "release", "view", tag, "--repo", a.repo],
                            capture_output=True).returncode == 0
    if exists:
        print(f"\nrelease {tag} exists - replacing its assets")
        subprocess.run(["gh", "release", "upload", tag, "--repo", a.repo, "--clobber",
                        os.path.join(work, OTA),
                        os.path.join(work, "manifest.json")], check=True)
    else:
        subprocess.run(["gh", "release", "create", tag, "--repo", a.repo,
                        "--title", f"F1 Tracker {v}", "--notes", notes,
                        os.path.join(work, OTA),
                        os.path.join(work, "manifest.json")], check=True)
    print(f"\npublished https://github.com/{a.repo}/releases/tag/{tag}")


if __name__ == "__main__":
    main()
