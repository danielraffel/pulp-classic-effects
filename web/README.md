# Web demos (WAMv2 / WebAssembly)

This directory cross-compiles the 15 classic-effect plugins to **WAMv2** (Web
Audio Modules v2) — headless WebAssembly DSP modules that run in an
`AudioWorklet` — and the CI workflow (`.github/workflows/pages.yml`) publishes
them to GitHub Pages. No cross-origin isolation is required: the build is
single-threaded, so there is no `SharedArrayBuffer`, so no COOP/COEP headers are
needed, which is exactly why stock GitHub Pages (which cannot set custom
headers) can host it.

`-sSINGLE_FILE` base64-embeds the wasm into the JS module, so there is not even
a separate `.wasm` fetch — the whole DSP is one `wam-dsp.js` ES module.

## What's here

- `CMakeLists.txt` — a **standalone** `emcmake` project (deliberately NOT wired
  into the repo-root `CMakeLists.txt`, which is the native plugin build). It
  `include()`s pulp's `tools/cmake/PulpWam.cmake` and declares, per effect, two
  targets:
  - `<Name>` (`SINGLE_FILE`) — the base64-embedded module the site loads.
  - `<Name>Raw` (separate `.wasm`, `ENVIRONMENT=web,worker,node`) — the flavour
    the Node verification harness instantiates directly. A `SINGLE_FILE` module
    is `web,worker` only and will not load in Node, so the raw target exists
    solely for headless testing; `build-web.sh` does not ship it.
- `*_wasm.cpp` — one tiny entry file per effect, each just `#include`s the
  plugin header and defines `pulp_wam_make_processor()` from the effect's
  `create_<effect>()` factory.
- `../scripts/build-web.sh` — configures, builds, and assembles the site tree
  (used by both local dev and CI).

## Build & serve locally

You need three things: Emscripten (pinned — see below), a pulp **source**
checkout, and choc headers.

```sh
# 1. Activate the pinned Emscripten toolchain.
source /path/to/emsdk/emsdk_env.sh   # emsdk must be installed at 6.0.2

# 2. Build + assemble docs/ (choc arg is the dir CONTAINING choc/).
PULP_ROOT=/path/to/pulp CHOC_INCLUDE=/path/to/choc ./scripts/build-web.sh

# 3. Serve it (module scripts require http://, not file://).
python3 -m http.server -d docs 8080
```

The script writes a static tree under `docs/` — one folder per demo, each with
`wam-dsp.js` (the SINGLE_FILE DSP module), `wam-processor.js`, and
`wam-runtime.mjs`, plus a shared `player/` holding `wam-plugin.js`,
`wam-runtime.mjs`, and `wam-scope.mjs`. `docs/` is **git-ignored** — no wasm or
generated JS is ever committed.

### Why the player needs three sibling modules

`player/wam-plugin.js` is the shared main-thread host, served once. It imports
`processorNameForUrl()` from `wam-runtime.mjs`, and the player imports the
analyser UI from `wam-scope.mjs`; both must sit beside `wam-plugin.js` or the
browser 404s the sibling. Each demo dir additionally gets its own
`wam-processor.js` + `wam-runtime.mjs` because `wam-processor.js` runs in the
`AudioWorkletGlobalScope` and hard-imports `./wam-dsp.js` and `./wam-runtime.mjs`
by fixed relative specifier (worklet scope has no import map).

## Toolchain pins — and why they are mandatory

**These pins are not theoretical; the emscripten failure mode was observed.**

- **Emscripten `6.0.2`.** emsdk `3.1.74`'s `-sSINGLE_FILE` glue decodes the
  embedded wasm with `atob()`, which **does not exist in
  `AudioWorkletGlobalScope`**. The worklet aborts on load and the plugin makes
  no sound — while the UI still looks fine, so it is a silent failure. emcc
  `6.0.2` emits its own base64 decoder and works. (pulp's own
  `web-plugins.yml` uses `version: latest` — a live trap; do not copy it.)
- **pulp ref pinned** (see `PULP_REF` in `pages.yml`). pulp's
  `wam-runtime.mjs` hardcodes an emscripten-version-specific `env` import-stub
  list; a mismatched emcc emits extra imports (e.g. `_emscripten_memcpy_js`
  under 3.1.74) that are absent from that list and instantiation fails. Pinning
  both the toolchain and the pulp ref keeps them in lockstep. The workflow also
  runs weekly so drift against the pin surfaces as a red build, not a dead site.
  **The pinned ref must contain `core/format/src/wasm/wam-scope.mjs`** (the
  player imports it); see the `PULP_REF` caveat in `pages.yml`.

## Caveats (honest limitations)

- **The web editor is not the native editor.** Because `create_view()` returns
  `nullptr` in every wasm build (`core/format/src/wasm/headless_defaults.cpp`),
  the on-page UI is a token-faithful HTML/canvas recreation of each effect's
  controls driven by the plugin's parameter metadata over the worklet port, not
  the native Skia-rendered editor. The DSP is identical to the native build; the
  editor headers are compiled out under `#if !PULP_HEADLESS`.
- **All 15 are stereo audio effects** (`CATEGORY Effect`, audio in → audio out,
  no MIDI, no instrument voice), so there is no keyboard/MIDI input surface.

## One-time repo setup (manual)

GitHub Pages must be switched to **"GitHub Actions"** as the publishing source:
repo **Settings → Pages → Build and deployment → Source → GitHub Actions**.
This is a one-time manual toggle — until it is flipped, the `deploy-pages` step
fails even when the build is green.
