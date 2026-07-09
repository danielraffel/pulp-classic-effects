#!/usr/bin/env node
// gen-og-images.mjs — render a per-demo Open Graph preview image for every page.
//
// WHY: gen-og.mjs bakes og:image/twitter:card=summary_large_image tags ONLY when
// a <dir>/og.png exists beside the page. This script is what produces those PNGs.
// It drives each ASSEMBLED demo page (in docs/, not web/site/) through the real
// AudioWorklet path in headless Chrome, presses the player's `window.__start()`
// test seam, waits for the started editor, and screenshots the `#panel` element
// into <dir>/og.png. The gallery gets its own og.png (a screenshot of the card
// grid). Run this AFTER scripts/build-web.sh assembles the site, then re-run
// gen-og.mjs so the freshly-present images turn into og:image tags.
//
// The images are deploy artifacts (docs/ is gitignored), same as the wasm — they
// are never committed; CI regenerates them on every deploy and the weekly cron.
//
//   node web/gen-og-images.mjs <SITE_DIR>
//     SITE_DIR   the assembled site tree to screenshot (e.g. docs)
//
// Chrome is located via --browser, PLAYWRIGHT_CHROMIUM_PATH, CHROME_PATH, or a
// short list of common install paths (mirrors the SDK web-plugins lane). Requires
// playwright-core on the module path (CI: `npm install --no-save playwright-core`).

import { createServer } from "node:http";
import { readFile, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { join, extname, normalize } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";
import { chromium } from "playwright-core";

function arg(flag, dflt) {
  const i = process.argv.indexOf(flag);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : dflt;
}

const SITE_DIR = process.argv[2] && !process.argv[2].startsWith("--")
  ? process.argv[2]
  : arg("--site", "docs");
if (!existsSync(join(SITE_DIR, "index.html"))) {
  console.error(`error: ${SITE_DIR}/index.html not found — build the site first (scripts/build-web.sh).`);
  process.exit(2);
}

const PORT = 8791;
const BASE = `http://127.0.0.1:${PORT}`;

// Locate a Chrome/Chromium binary the same way the SDK's browser fixtures do.
const CANDIDATES = [
  arg("--browser", null),
  process.env.PLAYWRIGHT_CHROMIUM_PATH,
  process.env.CHROME_PATH,
  "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/Applications/Chromium.app/Contents/MacOS/Chromium",
  "/usr/bin/google-chrome",
  "/usr/bin/chromium-browser",
  "/usr/bin/chromium",
].filter(Boolean);
const CHROME = CANDIDATES.find((p) => existsSync(p));
if (!CHROME) {
  console.error("error: no Chrome/Chromium binary found (set CHROME_PATH or pass --browser).");
  process.exit(2);
}

// Minimal, correct-MIME static server rooted at SITE_DIR. The worklet + module
// imports need `text/javascript` on .js/.mjs or the page fails silently.
const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".wasm": "application/wasm",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".ttf": "font/ttf",
  ".woff2": "font/woff2",
  ".txt": "text/plain; charset=utf-8",
};
const server = createServer(async (req, res) => {
  try {
    let p = decodeURIComponent(new URL(req.url, BASE).pathname);
    if (p.endsWith("/")) p += "index.html";
    const file = join(SITE_DIR, normalize(p).replace(/^(\.\.[/\\])+/, ""));
    const body = await readFile(file);
    res.writeHead(200, { "content-type": MIME[extname(file)] || "application/octet-stream" });
    res.end(body);
  } catch {
    res.writeHead(404, { "content-type": "text/plain" });
    res.end("not found");
  }
});
await new Promise((r) => server.listen(PORT, "127.0.0.1", r));

// The gallery's `plugins` array is the single source of truth for the demo dirs
// (same array gen-og.mjs reads). Parse it out of the assembled gallery page.
const galleryHtml = await readFile(join(SITE_DIR, "index.html"), "utf8");
const arrText = (galleryHtml.match(/const plugins = (\[[\s\S]*?\]);/) || [])[1];
if (!arrText) { console.error("could not find `const plugins = [...]` in the gallery"); process.exit(1); }
// eslint-disable-next-line no-eval — trusted, our own assembled gallery file.
const plugins = eval(arrText);

const browser = await chromium.launch({
  executablePath: CHROME,
  headless: true,
  // --mute-audio: the demo builds a real AudioContext; keep CI silent.
  // --autoplay-policy: __start() runs without a user gesture, so let the context
  //   actually resume() (otherwise start() awaits a resume that never lands).
  args: ["--mute-audio", "--autoplay-policy=no-user-gesture-required"],
});

let failed = 0;
async function attempt(pageUrl, selector, outPath, label) {
  const context = await browser.newContext({
    viewport: { width: 960, height: 1200 },
    deviceScaleFactor: 2,   // crisp @2x preview cards
  });
  const page = await context.newPage();
  page.on("pageerror", (e) => console.log(`  [pageerror ${label}]`, e.message));
  try {
    await page.goto(pageUrl, { waitUntil: "load", timeout: 30000 });
    if (selector === "#panel") {
      // Press the player's test seam and wait for the started editor.
      await page.waitForFunction(() => typeof window.__start === "function", null, { timeout: 20000 });
      await page.evaluate(() => { window.__start(); });
      await page.waitForFunction(() => window.__demo && window.__demo.started === true, null, { timeout: 25000 });
      await sleep(700);   // let the scope/meter/widgets settle into their first paint
    }
    const el = await page.$(selector);
    if (!el) throw new Error(`selector ${selector} not found`);
    await el.screenshot({ path: outPath });
    const { width, height } = await el.boundingBox();
    console.log(`  ok   ${label} -> ${outPath} (${Math.round(width * 2)}x${Math.round(height * 2)})`);
  } finally {
    await context.close();
  }
}

// One retry absorbs transient headless worklet-load timing without weakening the
// "every page must produce an image" guarantee: a genuine failure still fails.
async function shoot(pageUrl, selector, outPath, label) {
  for (let tryNo = 1; tryNo <= 2; tryNo++) {
    try {
      await attempt(pageUrl, selector, outPath, label);
      return;
    } catch (e) {
      if (tryNo === 2) {
        failed++;
        console.log(`  FAIL ${label}: ${e && e.message ? e.message : e}`);
      } else {
        console.log(`  retry ${label} (${e && e.message ? e.message : e})`);
        await sleep(500);
      }
    }
  }
}

// Per-demo cards: the started editor panel.
for (const p of plugins) {
  await shoot(`${BASE}/${p.dir}/`, "#panel", join(SITE_DIR, p.dir, "og.png"), p.dir);
}
// Gallery card: the card grid itself (representative montage of the whole site).
await shoot(`${BASE}/`, ".wrap", join(SITE_DIR, "og.png"), "gallery");

await browser.close();
server.close();

if (failed) { console.error(`\ngen-og-images: ${failed} page(s) failed to render.`); process.exit(1); }
console.log(`\ngen-og-images: ${plugins.length} demo image(s) + gallery rendered into ${SITE_DIR}/`);
