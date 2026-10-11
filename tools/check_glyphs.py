#!/usr/bin/env python3
"""f1-tracker's glyph check: the shared engine (tracker-common/tools/check_glyphs.py) plus this
project's generated tables (decision 52: that is where the real risk is, because nobody reads
those files). REQUIREMENTS.md decisions 36, 51 and 52."""
import glob, importlib.util, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import tracker_common

spec = importlib.util.spec_from_file_location("glyph_engine", tracker_common.find("tools/check_glyphs.py"))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)
SRC = os.path.join(ROOT, "f1-tracker")


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



engine.configure(
    ROOT, "f1-tracker.yaml", "f1-tracker",
    shared_fonts=tracker_common.find("fonts.yaml"),
    generated=generated_strings,
    skip_headers=("f1_portraits.h",),
    skip_prefix=("f1_",),
    font_files=sorted(glob.glob(os.path.join(ROOT, ".esphome", "font", "Roboto Mono@400*.ttf"))) + ["/tmp/RobotoMono.ttf"],
)

if __name__ == "__main__":
    sys.exit(engine.main())
