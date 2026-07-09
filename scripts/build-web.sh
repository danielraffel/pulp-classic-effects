#!/usr/bin/env bash
# build-web.sh — reproduce the GitHub Pages build locally (and in CI).
#
# Cross-compiles every classic-effect plugin to a headless WAMv2 DSP module
# (via web/CMakeLists.txt + PulpWam.cmake) and assembles a static site tree
# under docs/ that GitHub Pages serves as-is.
#
# Prerequisites (the caller sets these up; CI pins them — see
# .github/workflows/pages.yml and web/README.md for WHY the versions matter):
#   * Emscripten on PATH (pinned to 6.0.2: 3.1.74's SINGLE_FILE glue decodes the
#     embedded wasm with atob(), which does not exist in AudioWorkletGlobalScope,
#     so the worklet aborts silently). `source <emsdk>/emsdk_env.sh` first.
#   * A pulp SOURCE checkout (PulpWam.cmake compiles core/*/src directly).
#   * choc headers (dir CONTAINING choc/).
#
# Usage:
#   PULP_ROOT=/path/to/pulp CHOC_INCLUDE=/path/to/choc-parent ./scripts/build-web.sh
# or positionally:
#   ./scripts/build-web.sh <PULP_ROOT> <CHOC_INCLUDE> [OUT_DIR]
#
# Works on macOS and Linux (no GNU-only coreutils flags; portable cp/mkdir).

set -euo pipefail

# --- Resolve repo root (this script lives in <repo>/scripts) -----------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# --- Inputs ------------------------------------------------------------------
PULP_ROOT="${1:-${PULP_ROOT:-}}"
CHOC_INCLUDE="${2:-${CHOC_INCLUDE:-${PULP_WAM_CHOC_INCLUDE:-}}}"
OUT_DIR="${3:-${OUT_DIR:-${REPO_ROOT}/docs}}"

if [[ -z "${PULP_ROOT}" ]]; then
    echo "error: PULP_ROOT is required (a pulp SOURCE checkout)." >&2
    echo "usage: PULP_ROOT=/path/to/pulp CHOC_INCLUDE=/path/to/choc-parent $0" >&2
    exit 2
fi
PULP_ROOT="$(cd "${PULP_ROOT}" && pwd)"

if [[ ! -f "${PULP_ROOT}/tools/cmake/PulpWam.cmake" ]]; then
    echo "error: ${PULP_ROOT}/tools/cmake/PulpWam.cmake not found — PULP_ROOT is not a pulp source tree." >&2
    exit 2
fi

if ! command -v emcmake >/dev/null 2>&1; then
    echo "error: emcmake not on PATH. Run: source <emsdk>/emsdk_env.sh" >&2
    exit 2
fi

# choc: honour an explicit path; otherwise let PulpWam.cmake try to auto-locate a
# sibling build tree and fail loudly if it cannot.
CHOC_ARG=()
if [[ -n "${CHOC_INCLUDE}" ]]; then
    CHOC_INCLUDE="$(cd "${CHOC_INCLUDE}" && pwd)"
    if [[ ! -f "${CHOC_INCLUDE}/choc/audio/choc_MIDI.h" ]]; then
        echo "error: ${CHOC_INCLUDE} does not contain choc/audio/choc_MIDI.h (pass the dir CONTAINING choc/)." >&2
        exit 2
    fi
    CHOC_ARG=(-DPULP_WAM_CHOC_INCLUDE="${CHOC_INCLUDE}")
fi

BUILD_DIR="${REPO_ROOT}/web/build"
WASM_SRC="${PULP_ROOT}/core/format/src/wasm"

echo "==> pulp source : ${PULP_ROOT}"
echo "==> choc        : ${CHOC_INCLUDE:-<auto>}"
echo "==> build dir   : ${BUILD_DIR}"
echo "==> output      : ${OUT_DIR}"
echo "==> emcc        : $(emcc --version | head -1)"

# --- Configure + build -------------------------------------------------------
emcmake cmake -S "${REPO_ROOT}/web" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPULP_ROOT="${PULP_ROOT}" \
    "${CHOC_ARG[@]}"

cmake --build "${BUILD_DIR}" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

# --- Assemble the site tree --------------------------------------------------
# Map each SINGLE_FILE CMake target's <Name>.js to a demo folder. The folder
# names match the plugin source dirs so the site (authored separately) can
# reference them 1:1. (The *Raw targets are the Node-verification flavour and
# are not shipped.)
#   <TargetName>:<demo-folder>
DEMOS=(
    "Chorus:chorus"
    "CompressorExpander:compressor-expander"
    "Delay:delay"
    "Distortion:distortion"
    "Flanger:flanger"
    "Panning:panning"
    "ParametricEq:parametric-eq"
    "Phaser:phaser"
    "PingPong:ping-pong"
    "PitchShift:pitch-shift"
    "RingMod:ring-mod"
    "Robotization:robotization"
    "Tremolo:tremolo"
    "Vibrato:vibrato"
    "Wah:wah"
)

# Fresh output tree (docs/ is gitignored — never committed).
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/player"

# The shared main-thread WAM host is served ONCE at player/wam-plugin.js. It is
# loaded by the site page and told each demo's dsp+processor URLs, so it does not
# need to sit next to the DSP.
#
# player/ needs THREE siblings of wam-plugin.js:
#   * wam-runtime.mjs — wam-plugin.js imports processorNameForUrl() from it, so
#     the AudioWorkletNode it creates asks for the same per-module processor name
#     the worklet registered. A missing sibling 404s.
#   * wam-scope.mjs   — the player imports it for the analyser/scope UI. Same
#     404-if-missing hazard, so it must ship beside wam-plugin.js too.
cp "${WASM_SRC}/wam-plugin.js"   "${OUT_DIR}/player/wam-plugin.js"
cp "${WASM_SRC}/wam-runtime.mjs" "${OUT_DIR}/player/wam-runtime.mjs"
cp "${WASM_SRC}/wam-scope.mjs"   "${OUT_DIR}/player/wam-scope.mjs"

for entry in "${DEMOS[@]}"; do
    target="${entry%%:*}"
    folder="${entry##*:}"
    dsp_js="${BUILD_DIR}/${target}.js"
    if [[ ! -f "${dsp_js}" ]]; then
        echo "error: expected build output ${dsp_js} is missing." >&2
        exit 1
    fi
    dest="${OUT_DIR}/${folder}"
    mkdir -p "${dest}"

    # The SINGLE_FILE DSP module, renamed to the fixed name the processor imports.
    cp "${dsp_js}" "${dest}/wam-dsp.js"

    # WHY EACH DEMO GETS ITS OWN COPY of wam-processor.js + wam-runtime.mjs:
    # wam-processor.js runs inside the AudioWorkletGlobalScope and HARD-IMPORTS
    # its DSP by the fixed relative specifier `./wam-dsp.js` (worklet scope has
    # no import map / bare-specifier resolution). So the processor MUST be
    # co-located with THAT demo's wam-dsp.js — a single shared processor could
    # only import one demo's DSP. wam-runtime.mjs is imported by the processor
    # the same relative way, so it rides along per-demo too.
    cp "${WASM_SRC}/wam-processor.js" "${dest}/wam-processor.js"
    cp "${WASM_SRC}/wam-runtime.mjs"  "${dest}/wam-runtime.mjs"
done

echo
echo "==> Assembled site tree at ${OUT_DIR}:"
find "${OUT_DIR}" -type f | sort | while read -r f; do
    printf '    %8s  %s\n' "$(wc -c < "$f" | tr -d ' ')" "${f#"${OUT_DIR}/"}"
done
echo
echo "Done. Serve with:  python3 -m http.server -d \"${OUT_DIR}\" <port>"
