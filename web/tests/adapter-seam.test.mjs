#!/usr/bin/env node
// adapter-seam.test.mjs — proves the demo shell is host-agnostic.
//
// WS-B introduced a WAM "host adapter" INSIDE pulp-player.js. The whole point of
// the seam is that the shell (mountDemo and everything below it) talks ONLY to
// `adapter.*` and never reaches for the `PulpWAM` backend directly. This test
// pins that invariant statically (fast, zero-dependency, runs in any Node):
//
//   1. `PulpWAM` is referenced only in the import, the contract comment, and the
//      single `createWamAdapter` factory — NOWHERE inside the mountDemo shell.
//   2. The shell exposes the injectable adapter seam (`opts.createAdapter`) and
//      creates the main plugin + the chained-synth voice pool through it.
//   3. The adapter factory exposes the COMPLETE contract WS-B settled on.
//
// A companion runtime proof (a fake adapter that records calls, mounted in a
// real browser) lives in adapter-smoke.html.
//
//   Run:  node web/tests/adapter-seam.test.mjs

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const HERE = dirname(fileURLToPath(import.meta.url));
const PLAYER = join(HERE, "..", "site", "player", "pulp-player.js");
const src = readFileSync(PLAYER, "utf8");

let failed = 0;
const ok = (cond, msg) => { console.log(`${cond ? "  ok  " : "FAIL  "}${msg}`); if (!cond) failed++; };

// ——— 1. PulpWAM is confined to the adapter factory, never in the shell body.
const shellStart = src.indexOf("export async function mountDemo(");
ok(shellStart > 0, "found the mountDemo shell");
const shellBody = src.slice(shellStart);
ok(!/\bPulpWAM\b/.test(shellBody),
   "the mountDemo shell body contains no `PulpWAM` reference");

// Every PulpWAM occurrence sits before the shell — i.e. import / comment / factory.
const factoryStart = src.indexOf("function createWamAdapter(");
const factoryEnd = src.indexOf("export async function mountDemo(");
ok(factoryStart > 0 && factoryStart < factoryEnd, "createWamAdapter factory precedes the shell");
// The only *code* use of PulpWAM (createInstance) is inside the factory.
const createUses = [...src.matchAll(/PulpWAM\s*\.\s*createInstance/g)].map((m) => m.index);
ok(createUses.length === 1, "exactly one PulpWAM.createInstance call site");
ok(createUses.every((i) => i >= factoryStart && i < factoryEnd),
   "PulpWAM.createInstance is only called inside createWamAdapter");

// ——— 2. The shell drives an injectable adapter seam, not a hardwired backend.
ok(/opts\.createAdapter\s*\|\|\s*createWamAdapter/.test(shellBody),
   "shell honours an injectable opts.createAdapter seam");
ok(/S\.wam\s*=\s*await\s+makeAdapter\(/.test(shellBody),
   "main plugin instance is created via the adapter seam (makeAdapter)");
ok(/await\s+S\.wam\.createSecondary\(/.test(shellBody),
   "chained-synth voice pool is created via adapter.createSecondary");

// ——— 3. The adapter factory exposes the complete WS-B contract.
const factoryBody = src.slice(factoryStart, factoryEnd);
const CONTRACT = [
  "descriptor", "audioNode", "getParameterInfo", "setParameterValue",
  "getParameterValue", "scheduleMidi", "sendSysex", "getState", "setState",
  "onMidiOut", "onParamsChanged", "createSecondary", "destroy",
];
for (const member of CONTRACT) {
  ok(factoryBody.includes(member), `adapter contract exposes \`${member}\``);
}

console.log(failed ? `\n${failed} assertion(s) FAILED` : "\nadapter seam intact — all assertions passed");
process.exit(failed ? 1 : 0);
