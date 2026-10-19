/* MASTER GLUE UI contract test: the module.json hierarchy and chain_params
 * run through Schwung's REAL page planner and contract validator (from a
 * Schwung checkout, SCHWUNG_ROOT), so the pages the device will draw are
 * checked, not assumed.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE). */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = process.env.SCHWUNG_ROOT || path.resolve(here, "../../schwung");
const load = (p) => import(pathToFileURL(path.join(root, p)).href);
const { planPages } = await load("src/shared/param_pages/page_plan.mjs");
const { validateContract } = await load("src/shared/param_pages/validate_contract.mjs");

let failures = 0;
const check = (cond, msg) => { if (cond) console.log("ok  ", msg); else { failures++; console.log("FAIL", msg); } };

const manifest = JSON.parse(fs.readFileSync(path.join(here, "../module.json"), "utf8"));
const caps = manifest.capabilities;
const hierarchy = caps.ui_hierarchy, chainParams = caps.chain_params;

const v = validateContract({ id: manifest.id, hierarchy, chainParams, capabilities: caps });
const bad = v.findings.filter((f) => f.level !== "info");
for (const f of v.findings) console.log(`     [${f.level}] ${f.rule}: ${f.message}`);
check(bad.length === 0, `Schwung's contract validator: no errors or warnings (${v.findings.length} info)`);

const plan = planPages({ hierarchy, chainParams });
const pages = plan.pages || plan;
const summary = pages.map((p) => `${p.name}[${p.kind}${p.keys ? ": " + p.keys.join(",") : ""}]`);
console.log("     pages:", summary.join("  "));
const byName = (n) => pages.find((p) => p.name === n);
const main = pages[0];
check(main && main.keys && main.keys.join() === "glue,punch,release,low_protect,density,limit,ceiling,output",
      "page 1 is MAIN with the eight knobs in order");
check(byName("Advanced") && byName("Advanced").keys.join() ===
      "ratio,threshold,knee,lim_release,lookahead,comp_bypass,lim_bypass,bypass", "page 2 is Advanced");
check(pages.indexOf(byName("Advanced")) === 1, "Advanced directly follows MAIN");
check(byName("Meters") && byName("Meters").keys.join() === "in_peak,out_peak,comp_gr,lim_gr,compare",
      "a Meters page carries the four readouts and COMPARE");
check(pages.some((p) => p.kind === "preset" && p.listParam === "preset" && p.countParam === "preset_count"),
      "a preset browser page for the three factory presets");
const readouts = chainParams.filter((p) => p.access === "read").map((p) => p.key);
check(readouts.join() === "in_peak,out_peak,comp_gr,lim_gr", "the meters are declared read-only (access: read)");
for (const p of chainParams) {
    const sn = p.short_name || "";
    if (sn.length > 5) { failures++; console.log("FAIL short name too long for a cell:", p.key, sn); }
}
check(true, "every short name fits a 5-character cell");

if (failures) { console.log(`\n${failures} UI FAILURE(S)`); process.exit(1); }
console.log("\nALL UI TESTS PASSED");
