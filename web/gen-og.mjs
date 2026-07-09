#!/usr/bin/env node
// gen-og.mjs — inject static Open Graph / Twitter meta into every demo page so
// links unfurl with the plugin's name + description in Messages / Discord / etc.
//
// WHY STATIC: social crawlers fetch the raw HTML and DO NOT run the page's
// JavaScript, so the tags must be present in each page's <head> at rest — they
// cannot be injected by the player at runtime. The single source of truth for
// each plugin's name + description is the gallery's `plugins` array, so this
// script reads that and rewrites each <dir>/index.html's <head>, preserving the
// page's existing <script type="module"> body (the mountDemo call).
//
// Re-run after editing a name/description in web/site/index.html. It is also run
// by scripts/build-web.sh so the deployed docs/ always has current tags.
//
//   node web/gen-og.mjs <SITE_BASE> [SITE_DIR]
//     SITE_BASE  absolute base URL, e.g. https://www.generouscorp.com/pulp-example-plugins
//     SITE_DIR   the web/site dir to rewrite (default: alongside this script)

import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const HERE = dirname(fileURLToPath(import.meta.url));
const SITE_BASE = (process.argv[2] || "").replace(/\/+$/, "");
const SITE_DIR = process.argv[3] || join(HERE, "site");
if (!SITE_BASE) { console.error("usage: node gen-og.mjs <SITE_BASE> [SITE_DIR]"); process.exit(2); }

const galleryHtml = readFileSync(join(SITE_DIR, "index.html"), "utf8");

// Pull the site title + intro paragraph and the plugins array out of the gallery.
const siteTitle = (galleryHtml.match(/<title>([^<]+)<\/title>/) || [])[1] || "Pulp Web Demos";
const siteDesc = ((galleryHtml.match(/<header>[\s\S]*?<p>([\s\S]*?)<\/p>/) || [])[1] || "")
  .replace(/\s+/g, " ").trim();
const arrText = (galleryHtml.match(/const plugins = (\[[\s\S]*?\]);/) || [])[1];
if (!arrText) { console.error("could not find `const plugins = [...]` in the gallery"); process.exit(1); }
// eslint-disable-next-line no-eval — trusted, our own committed gallery file.
const plugins = eval(arrText);

const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;")
  .replace(/>/g, "&gt;").replace(/"/g, "&quot;");

// The OG/Twitter block for one page. `image` optional (added once per-page
// renders exist); without it we emit a text "summary" card, which still unfurls
// with the title + description.
function ogBlock({ title, desc, url, image }) {
  const L = [
    `<meta property="og:type" content="website">`,
    `<meta property="og:site_name" content="Pulp">`,
    `<meta property="og:title" content="${esc(title)}">`,
    `<meta property="og:description" content="${esc(desc)}">`,
    `<meta property="og:url" content="${esc(url)}">`,
    `<meta name="twitter:title" content="${esc(title)}">`,
    `<meta name="twitter:description" content="${esc(desc)}">`,
  ];
  if (image) {
    L.push(`<meta property="og:image" content="${esc(image)}">`);
    L.push(`<meta name="twitter:card" content="summary_large_image">`);
    L.push(`<meta name="twitter:image" content="${esc(image)}">`);
  } else {
    L.push(`<meta name="twitter:card" content="summary">`);
  }
  return L.map((l) => "  " + l).join("\n");
}

// Rewrite one per-plugin page: keep its <script>…</script> body, give it a full
// <head> with charset + viewport + title + description + OG. Idempotent.
function rewritePluginPage(dir, name, desc) {
  const file = join(SITE_DIR, dir, "index.html");
  if (!existsSync(file)) { console.warn(`skip (missing): ${dir}/index.html`); return false; }
  const src = readFileSync(file, "utf8");
  const script = (src.match(/<script[\s\S]*<\/script>/) || [])[0];
  if (!script) { console.warn(`skip (no <script>): ${dir}/index.html`); return false; }
  const title = `${name} — Pulp web demo`;
  const url = `${SITE_BASE}/${dir}/`;
  const ogImage = existsSync(join(SITE_DIR, dir, "og.png")) ? `${url}og.png` : null;
  const html = `<!doctype html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <title>${esc(title)}</title>
  <meta name="description" content="${esc(desc)}">
${ogBlock({ title, desc, url, image: ogImage })}
</head>
<body>
<div id="app"></div>
${script}
</body>
</html>
`;
  writeFileSync(file, html);
  return true;
}

let n = 0;
for (const p of plugins) if (rewritePluginPage(p.dir, p.name, p.desc)) n++;

// Gallery page: inject the site-level OG block into its <head> (idempotent —
// replace any prior generated block, marked by comment fences).
const galleryUrl = `${SITE_BASE}/`;
const galleryOg = `  <!-- og:begin -->\n  <meta name="description" content="${esc(siteDesc)}">\n${ogBlock({ title: siteTitle, desc: siteDesc, url: galleryUrl, image: existsSync(join(SITE_DIR, "og.png")) ? `${galleryUrl}og.png` : null })}\n  <!-- og:end -->`;
let g = galleryHtml.replace(/\n?\s*<!-- og:begin -->[\s\S]*?<!-- og:end -->/, "");
g = g.replace(/(<title>[^<]*<\/title>)/, `$1\n${galleryOg}`);
writeFileSync(join(SITE_DIR, "index.html"), g);

console.log(`gen-og: ${n} plugin page(s) + gallery updated (base ${SITE_BASE})`);
