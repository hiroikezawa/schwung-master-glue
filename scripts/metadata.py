#!/usr/bin/env python3
"""Embed module.json's contract in the DSP and check it against the C table.

The runtime callbacks never read files, so chain_params, ui_hierarchy and the
version are compiled in. The build fails if module.json and dsp/mg_params.h
disagree on any key, order, range or default.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
meta = json.loads((root / "module.json").read_text())
caps = meta["capabilities"]
cp = caps["chain_params"]
params_h = (root / "dsp" / "mg_params.h").read_text()
rows = re.findall(r'\{"(\w+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*([01])\}', params_h)
assert rows, "could not parse MG_PARAMS in mg_params.h"
editable = [p for p in cp if p.get("access") != "read"]
readouts = [p for p in cp if p.get("access") == "read"]
assert cp[:len(editable)] == editable, "readouts must come after every editable param"
assert [p["key"] for p in editable] == [r[0] for r in rows], "chain_params order must match mg_params.h"
for p, (key, lo, hi, de, integer) in zip(editable, rows):
    lo, hi, de = float(lo), float(hi), float(de)
    if p["type"] == "enum":
        assert lo == 0 and len(p["options"]) == hi + 1, key
        assert p["options"].index(p["default"]) == de, key
        assert integer == "1", key
    else:
        assert (p["min"], p["max"], p["default"]) == (lo, hi, de), key
        assert integer == "0", key
# The C option tables must spell the options exactly as module.json does.
for key, table in (("release", "MG_RELEASE_NAMES"), ("low_protect", "MG_HPF_NAMES"),
                   ("ratio", "MG_RATIO_NAMES"), ("bypass", "MG_SWITCH_NAMES")):
    body = re.search(table + r"\[[^\]]*\]\s*=\s*\{([^}]*)\}", params_h).group(1)
    c_names = re.findall(r'"([^"]*)"', body)
    opts = next(p for p in cp if p["key"] == key)["options"]
    assert c_names == opts, f"{table} {c_names} != module.json {opts}"
assert [p["key"] for p in readouts] == ["in_peak", "out_peak", "comp_gr", "lim_gr"]
assert meta["dsp"] == meta["id"] + ".so", "the host loads audio_fx/<id>/<id>.so"

hier = caps["ui_hierarchy"]
known = {p["key"] for p in cp}
for name, lvl in hier["levels"].items():
    for k in lvl.get("knobs", []):
        assert k in known, f"{name}: unknown knob {k}"
    assert len(lvl.get("knobs", [])) <= 8, name


def literal(value):
    return json.dumps(json.dumps(value, separators=(",", ":")), ensure_ascii=True)


out = pathlib.Path(sys.argv[1]) / "mg_metadata.h"
out.write_text("\n".join([
    "/* Generated at build time from module.json; edit that instead. */",
    "static const char kChainParams[] = " + literal(cp) + ";",
    "static const char kUiHierarchy[] = " + literal(hier) + ";",
    f'static const char kVersion[] = "{meta["version"]}";',
]) + "\n")
