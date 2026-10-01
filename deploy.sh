#!/usr/bin/env bash
# Build gate for F1 Tracker (REQUIREMENTS.md decision 63).
#
# plane-tracker learned this the hard way: grepping the build log only for
# "error:" hid a -Wformat-truncation in its own code. This script therefore
# fails on ANY compiler warning attributable to our sources - the YAML lambdas
# and f1-tracker/*.h - while ignoring the thousands that come out of ESP-IDF,
# LVGL and the toolchain, which we do not control.
#
#   ./deploy.sh              validate + compile + warning gate
#   ./deploy.sh --config     validate only
#   ./deploy.sh --upload     ... then flash over USB (needs a board; see 14.1)
set -euo pipefail

cd "$(dirname "$0")"
YAML=f1-tracker.yaml
ESPHOME=.venv/bin/esphome
LOG=$(mktemp -t f1build)

[ -x "$ESPHOME" ] || { echo "no venv: python3 -m venv .venv && .venv/bin/pip install esphome"; exit 1; }
[ -f secrets.yaml ] || { echo "no secrets.yaml: cp secrets.yaml.example secrets.yaml and fill it in"; exit 1; }

echo "==> validating"
"$ESPHOME" config "$YAML" > /dev/null

# decision 52: a missing glyph draws a box, survives compilation, and only
# appears on a screen. plane-tracker lost "UPGRADING" exactly this way.
echo "==> checking glyphs"
.venv/bin/python tools/check_glyphs.py | tail -3

if [ "${1:-}" = "--config" ]; then echo "config OK"; exit 0; fi

echo "==> compiling"
set +e
"$ESPHOME" compile "$YAML" > "$LOG" 2>&1
rc=$?
set -e

# Warnings in OUR code only. Everything under components/, esp-idf, the
# toolchain and the PlatformIO package tree is upstream and out of scope.
OURS=$(grep -nE "warning:" "$LOG" \
        | grep -viE "components/|/esp-idf/|/framework-|toolchain|\.platformio" || true)

if [ $rc -ne 0 ]; then
  echo "==> COMPILE FAILED"; grep -E "error:" "$LOG" | head -20; echo "(full log: $LOG)"; exit 1
fi

if [ -n "$OURS" ]; then
  echo "==> FAILED: compiler warnings in our own code (decision 63)"
  echo "$OURS" | sed 's/^/    /'
  echo "(full log: $LOG)"
  exit 1
fi

grep -E "^(RAM|Flash):" "$LOG" | sed 's/^/    /'
echo "==> clean: no warnings in our code"

if [ "${1:-}" = "--upload" ]; then
  echo "==> uploading"
  "$ESPHOME" upload "$YAML"
fi
rm -f "$LOG"
