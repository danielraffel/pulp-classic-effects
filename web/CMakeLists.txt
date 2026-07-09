# WASM build for the pulp-classic-effects web demos.
#
# This is a STANDALONE emcmake project — it is deliberately NOT included by the
# repo root CMakeLists.txt. The root build is the NATIVE plugin build (Skia
# editors, VST3/AU/etc.); this one cross-compiles the same plugin headers to
# headless WebAssembly WAMv2 (Web Audio Modules v2) DSP modules for GitHub Pages.
#
# Build (see ../scripts/build-web.sh for the packaging step CI runs):
#   emcmake cmake -S web -B web/build -DCMAKE_BUILD_TYPE=Release \
#       -DPULP_ROOT=<pulp source checkout> \
#       -DPULP_WAM_CHOC_INCLUDE=<dir containing choc/>
#   cmake --build web/build
#
# Two target flavours per effect:
#   * <Name>     SINGLE_FILE — the wasm is BASE64-embedded into <Name>.js so it
#                runs inside an AudioWorkletGlobalScope (no fetch/async compile
#                there). This is what the GitHub Pages site loads.
#   * <Name>Raw  separate <Name>Raw.wasm (ENVIRONMENT=web,worker,node) — the
#                flavour the Node verification harness instantiates directly
#                (a SINGLE_FILE module is web,worker only and won't load in Node).

cmake_minimum_required(VERSION 3.20)
project(pulp-classic-effects-web-demos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# PULP_ROOT must point at a pulp SOURCE checkout: PulpWam.cmake compiles a
# curated DSP subset straight out of ${PULP_ROOT}/core/*/src, so an installed
# SDK is NOT enough. choc headers come from -DPULP_WAM_CHOC_INCLUDE=<dir
# containing choc/> (handled inside PulpWam.cmake).
if(NOT PULP_ROOT)
    message(FATAL_ERROR
        "pulp-classic-effects-web-demos: pass -DPULP_ROOT=<pulp source checkout>. "
        "PulpWam.cmake builds the headless DSP subset from ${PULP_ROOT}/core/*/src.")
endif()
if(NOT EXISTS "${PULP_ROOT}/tools/cmake/PulpWam.cmake")
    message(FATAL_ERROR
        "pulp-classic-effects-web-demos: ${PULP_ROOT}/tools/cmake/PulpWam.cmake not found — "
        "is -DPULP_ROOT a real pulp source tree?")
endif()
include(${PULP_ROOT}/tools/cmake/PulpWam.cmake)

# The plugin headers guard their native editor bodies with `#if !PULP_HEADLESS`
# (create_view() etc. pull in the Skia/view stack that does not exist in wasm).
# pulp_add_wam_plugin does not expose target_compile_definitions, so define the
# guard macro globally before declaring any target.
add_compile_definitions(PULP_HEADLESS=1)

# All 15 classic effects are stereo audio effects (CATEGORY Effect): audio in,
# audio out, no MIDI, no instrument voice. INCLUDES points at the plugin's
# source dir (one level up) so its header resolves.

# --- SINGLE_FILE (worklet / GitHub Pages) -----------------------------------
pulp_add_wam_plugin(Chorus              SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/chorus_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../chorus)
pulp_add_wam_plugin(CompressorExpander  SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/compressor_expander_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../compressor-expander)
pulp_add_wam_plugin(Delay               SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/delay_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../delay)
pulp_add_wam_plugin(Distortion          SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/distortion_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../distortion)
pulp_add_wam_plugin(Flanger             SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/flanger_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../flanger)
pulp_add_wam_plugin(Panning             SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/panning_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../panning)
pulp_add_wam_plugin(ParametricEq        SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/parametric_eq_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../parametric-eq)
pulp_add_wam_plugin(Phaser              SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/phaser_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../phaser)
pulp_add_wam_plugin(PingPong            SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/ping_pong_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../ping-pong)
pulp_add_wam_plugin(PitchShift          SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/pitch_shift_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../pitch-shift)
pulp_add_wam_plugin(RingMod             SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/ring_mod_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../ring-mod)
pulp_add_wam_plugin(Robotization        SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/robotization_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../robotization)
pulp_add_wam_plugin(Tremolo             SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/tremolo_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../tremolo)
pulp_add_wam_plugin(Vibrato             SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/vibrato_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../vibrato)
pulp_add_wam_plugin(Wah                 SINGLE_FILE ENTRY ${CMAKE_SOURCE_DIR}/wah_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../wah)

# --- Raw separate-wasm (Node verification harness) --------------------------
pulp_add_wam_plugin(ChorusRaw           ENTRY ${CMAKE_SOURCE_DIR}/chorus_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../chorus)
pulp_add_wam_plugin(CompressorExpanderRaw ENTRY ${CMAKE_SOURCE_DIR}/compressor_expander_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../compressor-expander)
pulp_add_wam_plugin(DelayRaw            ENTRY ${CMAKE_SOURCE_DIR}/delay_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../delay)
pulp_add_wam_plugin(DistortionRaw       ENTRY ${CMAKE_SOURCE_DIR}/distortion_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../distortion)
pulp_add_wam_plugin(FlangerRaw          ENTRY ${CMAKE_SOURCE_DIR}/flanger_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../flanger)
pulp_add_wam_plugin(PanningRaw          ENTRY ${CMAKE_SOURCE_DIR}/panning_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../panning)
pulp_add_wam_plugin(ParametricEqRaw     ENTRY ${CMAKE_SOURCE_DIR}/parametric_eq_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../parametric-eq)
pulp_add_wam_plugin(PhaserRaw           ENTRY ${CMAKE_SOURCE_DIR}/phaser_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../phaser)
pulp_add_wam_plugin(PingPongRaw         ENTRY ${CMAKE_SOURCE_DIR}/ping_pong_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../ping-pong)
pulp_add_wam_plugin(PitchShiftRaw       ENTRY ${CMAKE_SOURCE_DIR}/pitch_shift_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../pitch-shift)
pulp_add_wam_plugin(RingModRaw          ENTRY ${CMAKE_SOURCE_DIR}/ring_mod_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../ring-mod)
pulp_add_wam_plugin(RobotizationRaw     ENTRY ${CMAKE_SOURCE_DIR}/robotization_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../robotization)
pulp_add_wam_plugin(TremoloRaw          ENTRY ${CMAKE_SOURCE_DIR}/tremolo_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../tremolo)
pulp_add_wam_plugin(VibratoRaw          ENTRY ${CMAKE_SOURCE_DIR}/vibrato_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../vibrato)
pulp_add_wam_plugin(WahRaw              ENTRY ${CMAKE_SOURCE_DIR}/wah_wasm.cpp INCLUDES ${CMAKE_SOURCE_DIR}/../wah)
