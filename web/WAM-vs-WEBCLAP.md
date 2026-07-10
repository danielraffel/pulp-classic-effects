# WAM vs WebCLAP: how the web demos are built

Every effect in this repo has **two** live web demos, and both run the *same* C++
audio engine that ships in the native VST3 / AU / CLAP builds:

- **WAM gallery** — <https://danielraffel.github.io/pulp-classic-effects/>
- **WebCLAP gallery** — <https://pulp-wclap-demos.pages.dev/classic-effects/>

They are two different ways to get one `Processor` running in a browser. This
document explains what each one is, how it is built, what it shares with the
native plugin, and the honest tradeoffs — it is **not** an argument for one over
the other. Which one is "better" depends entirely on where you want to host it and
what you want to demonstrate.

---

## The one thing they have in common

A Pulp effect's audio engine is a single C++ `Processor` (a `*.hpp` file). That
same source is compiled — unchanged — to **five** targets:

| Target | Toolchain | ABI it speaks |
|---|---|---|
| VST3 | native compiler | VST3 |
| AU / AUv3 | native compiler | Audio Unit |
| CLAP (native) | native compiler | CLAP |
| **WAM** (web) | **Emscripten** | Web Audio Module v2 |
| **WebCLAP** (web) | **wasi-sdk** | CLAP (over WebAssembly) |

The DSP — waveshapers, filters, delay lines, FFT processing, state
(de)serialization — is byte-for-byte the same code in all five. What changes per
target is a thin **entry/adapter** layer, and (for the web) how the *editor* is
presented. Those differences are what the rest of this document is about.

---

## WAM (Web Audio Module v2)

**What it is.** The effect's `Processor` compiled with
[Emscripten](https://emscripten.org/) to a WebAssembly module that runs on the
audio thread inside a Web Audio
[`AudioWorklet`](https://developer.mozilla.org/en-US/docs/Web/API/AudioWorklet),
packaged as a [WAM v2](https://www.webaudiomodules.com/) plugin. The compile
target is Emscripten's
[Wasm Audio Worklets](https://emscripten.org/docs/api_reference/wasm_audio_worklets.html).

**How it is built.**

- Toolchain: **Emscripten** → `wasm32` (single-threaded).
- No `SharedArrayBuffer`, no worker threads — the engine runs single-threaded on
  the worklet's audio thread.
- The SDK supplies a shared adapter that bridges Web Audio's block callback and
  parameter/MIDI messaging to the `Processor` — `core/format/src/wasm/wam_adapter.cpp`
  (~205 lines) plus its header (~118 lines). This is written **once in the SDK**
  and reused by every effect; an effect author writes none of it.

**Hosting.** Because it is a plain single-threaded wasm module with no special
memory model, it can be served from **any static host** — including plain GitHub
Pages — with **no special HTTP headers**. That is why the WAM gallery lives on
`github.io`.

---

## WebCLAP

**What it is.** The *same* `Processor`, but compiled to expose the real
**[CLAP](https://cleveraudio.org/) ABI** to a CLAP host that itself lives inside
the audio worklet. Instead of adapting the engine to Web Audio's own plugin shape
(as WAM does), WebCLAP loads the plugin the way a native CLAP host would — through
`clap_entry`, factory, `clap_plugin`, the CLAP extension structs — only the whole
thing is running in WebAssembly.

**How it is built.**

- Toolchain: **wasi-sdk**, target **`wasm32-wasi-threads`**, built with
  `-pthread --shared-memory`.
- It reuses the **exact same** native CLAP entry point the desktop CLAP build
  uses (`core/format/include/pulp/format/clap_entry.hpp`, ~663 lines, shared), and
  adds a small WebCLAP shim (`core/format/include/pulp/format/web/wclap_adapter.hpp`,
  ~83 lines) that exports the wasm memory-allocation symbols (`malloc` / `free` /
  `cabi_realloc`) the host needs. Both are SDK-level and written once.
- Per effect, the entry file is a ~4-line wrapper:

  ```cpp
  #include "my_processor.hpp"
  #include <pulp/format/web/wclap_adapter.hpp>
  #include <pulp/format/clap_entry.hpp>

  PULP_WCLAP_PLUGIN("com.example.myeffect", "My Effect",
                    "My Company", "1.0.0", my_ns::create_my_effect)
  ```

  The `PULP_WCLAP_PLUGIN(...)` macro **is** the native `PULP_CLAP_PLUGIN(...)`
  macro plus the three memory exports — so the CLAP surface is literally identical
  to native.

**Hosting.** Threads + shared memory mean the page must be **cross-origin
isolated**: it has to be served with `COOP: same-origin` + `COEP: require-corp`
(and appropriate CORP on subresources). That requires a host that can set those
headers — [Cloudflare Pages](https://pages.cloudflare.com/) does it directly (which
is why the WebCLAP gallery lives on `pages.dev`), or a
[`coi-serviceworker`](https://github.com/gzuidhof/coi-serviceworker) shim can add
them on a host like GitHub Pages that otherwise can't.

---

## Side by side

| | WAM v2 | WebCLAP |
|---|---|---|
| Web toolchain | Emscripten | wasi-sdk (`wasm32-wasi-threads`) |
| ABI presented in the browser | Web Audio Module v2 | real CLAP ABI |
| Threads / `SharedArrayBuffer` | no (single-threaded) | yes (`-pthread --shared-memory`) |
| Cross-origin-isolation headers required | no | yes (COOP + COEP + CORP) |
| Runs on plain static hosting / GitHub Pages | yes, directly | only with a `coi-serviceworker` shim |
| Purpose-built host | Cloudflare Pages, static host, GitHub Pages | Cloudflare Pages (or COI-capable host) |
| Same DSP as native | yes | yes |

### Honest tradeoffs

**WAM leans toward:**

- *Trivial hosting.* Any static host works, no headers, no service worker — it
  drops onto GitHub Pages as-is.
- *Simplicity.* Single-threaded, no shared-memory model to reason about.

...at the cost of:

- Not the CLAP ABI — it is Web Audio's own module shape, so it is a step removed
  from how a native CLAP host actually loads the plugin.
- Single-threaded only; no `SharedArrayBuffer`-based parallelism.

**WebCLAP leans toward:**

- *The real CLAP ABI.* The effect is loaded through the same `clap_entry` /
  factory / extension structs a native CLAP host uses — the closest web analog to
  the desktop load path, and useful for exercising the CLAP surface itself.
- *Threads + `SharedArrayBuffer`*, which more faithfully mirror a native
  real-time environment.

...at the cost of:

- Requires cross-origin isolation (COOP/COEP/CORP), so it needs a host that can
  set those headers (or a service-worker shim) — it will **not** run on a bare
  static host without that.
- More moving parts in the hosting/setup story.

Neither is the "production" answer and neither is a toy: they are two valid ways to
put the identical engine in a browser, trading **hosting simplicity** (WAM) against
**ABI fidelity + threading** (WebCLAP).

---

## How much is shared with the native plugin

The audio engine is **100% shared** — the `Processor` `.hpp` is the same source
compiled to every target, native and web. What is *not* shared to the web is the
**editor**: natively `create_view()` returns a Skia-rendered view; in the web
builds there is no Skia, so the on-page controls are recreated as HTML/canvas
widgets driven by a shared player runtime.

Concretely, measured from this repo (and the SDK it consumes):

### Example: Chorus

| Piece | File | Lines | Shared with native? | Shared across web targets? |
|---|---|---:|---|---|
| Audio engine (DSP) | `chorus/chorus.hpp` | 285 | **yes — identical source** | yes (WAM + WebCLAP) |
| Native CLAP entry | `chorus/clap_entry.cpp` | 3 | native only | — |
| WebCLAP entry wrapper | `PULP_WCLAP_PLUGIN(...)` | ~4 | reuses native CLAP entry | WebCLAP only |
| Native editor (Skia) | `chorus/chorus_editor.hpp` | 37 | native only (not compiled to web) | — |
| Web control recreation | `web/site/chorus/index.html` config | ~8 | — | shared runtime, per-page config |

The 285-line `chorus.hpp` is the entire sound; every target — native VST3, AU,
CLAP, plus WAM and WebCLAP — runs that exact file. Adding the WebCLAP target to an
effect that already ships native CLAP costs the **~4-line** `PULP_WCLAP_PLUGIN`
wrapper, nothing more.

### Example: Tremolo

| Piece | File | Lines | Shared with native? |
|---|---|---:|---|
| Audio engine (DSP) | `tremolo/tremolo.hpp` | 203 | **yes — identical source** |
| Native CLAP entry | `tremolo/clap_entry.cpp` | 3 | native only |
| Native editor (Skia) | `tremolo/tremolo_editor.hpp` | 27 | native only |

Same story at a slightly smaller scale: 203 lines of engine shared verbatim across
all five targets, a 3-line native CLAP entry, and the ~4-line WebCLAP wrapper.

### The web editor is a recreation, shared across demos

The web pages do **not** compile the native editor. In every WebAssembly build
`create_view()` yields no renderable Skia view, so each demo page rebuilds the
controls in HTML/canvas from the **same Ink & Signal design tokens**. That
recreation is itself shared: a single runtime,
`web/site/player/pulp-player.js` (~1,385 lines), drives every demo, and each
effect's page (`web/site/<effect>/index.html`) is a ~30-line file that is mostly
`<meta>` tags plus roughly **8 lines** of per-effect widget configuration.

### Net

- **Audio engine: 100% shared** across native VST3/AU/CLAP, WAM, and WebCLAP — one
  `.hpp` per effect, no per-target forks.
- **Per-target entry code is tiny** — 3 lines for native CLAP, ~4 for WebCLAP.
- **The editor is the one part not shared to the web**, and even there the web
  recreation is a shared ~1,385-line runtime plus ~8 lines of per-effect config,
  not a bespoke rebuild per effect.

So the choice between WAM and WebCLAP does not change *what the effect is* — the
same engine is running either way. It changes only the browser-side ABI and,
consequently, where you can host it.
