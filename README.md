# Pulp Classic Effects

A growing suite of **classic audio effects** built as [Pulp](https://www.generouscorp.com/pulp/)
SDK example plugins — each a small, self-contained `Processor` that compiles to
VST3 / CLAP / standalone (and AU / AUv3 / AAX where the platform SDKs are
available), with a headless test that asserts the actual DSP behavior.

The goal is a legible, recognizable showcase of "write one `Processor`, get
every plugin format" — using effects everyone knows (tremolo, delay, chorus,
flanger, phaser, wah, compressor, …) as the worked examples, alongside a couple
of small companion examples (a MIDI utility and a minimal instrument) that round
out the SDK contract surface.

## Status

Effects are listed alphabetically. Each parameter set, name, and editor layout
mirrors the corresponding Reiss & McPherson / Juan Gil reference effect.

| Effect | Editor | Demo | Notes |
|---|---|---|---|
| Chorus | <a href="https://danielraffel.github.io/pulp-classic-effects/chorus/"><img src="screenshots/chorus.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/chorus/) | Multi-voice (2–5) LFO-swept delay ensemble — delay/width/depth, waveform + interpolation, stereo spread |
| Compressor | <a href="https://danielraffel.github.io/pulp-classic-effects/compressor-expander/"><img src="screenshots/compressor-expander.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/compressor-expander/) | Stereo-linked dynamics — Compressor/Expander mode, threshold/ratio/attack/release/makeup |
| Delay | <a href="https://danielraffel.github.io/pulp-classic-effects/delay/"><img src="screenshots/delay.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/delay/) | Feedback delay line — time, feedback, wet/dry mix |
| Distortion | <a href="https://danielraffel.github.io/pulp-classic-effects/distortion/"><img src="screenshots/distortion.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/distortion/) | Five waveshapers (hard/soft clip, exponential, full/half rectify) with input/output gain + tone tilt |
| Flanger | <a href="https://danielraffel.github.io/pulp-classic-effects/flanger/"><img src="screenshots/flanger.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/flanger/) | Short LFO-swept delay with feedback + inverted comb — waveform/interpolation, stereo |
| Panning | <a href="https://danielraffel.github.io/pulp-classic-effects/panning/"><img src="screenshots/panning.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/panning/) | Static stereo panner — equal-power+precedence or ITD+ILD binaural law, pan position |
| Parametric EQ | <a href="https://danielraffel.github.io/pulp-classic-effects/parametric-eq/"><img src="screenshots/parametric-eq.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/parametric-eq/) | Single-band biquad with selectable type (LP/HP/shelves/band-pass/-stop/peaking), freq/Q/gain |
| Phaser | <a href="https://danielraffel.github.io/pulp-classic-effects/phaser/"><img src="screenshots/phaser.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/phaser/) | 2–10 cascaded all-pass stages swept by an LFO — depth/feedback, min freq + sweep width, stereo |
| Ping-Pong Delay | <a href="https://danielraffel.github.io/pulp-classic-effects/ping-pong/"><img src="screenshots/ping-pong.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/ping-pong/) | Stereo cross-coupled delay with L/R balance — echoes bounce side to side as they decay |
| Pitch Shift | <a href="https://danielraffel.github.io/pulp-classic-effects/pitch-shift/"><img src="screenshots/pitch-shift.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/pitch-shift/) | Phase-vocoder pitch shift (±12 st) with selectable FFT size / hop / window |
| Ring Mod | <a href="https://danielraffel.github.io/pulp-classic-effects/ring-mod/"><img src="screenshots/ring-mod.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/ring-mod/) | Carrier ring modulation — depth, carrier frequency, selectable carrier waveform |
| Robotization | <a href="https://danielraffel.github.io/pulp-classic-effects/robotization/"><img src="screenshots/robotization.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/robotization/) | STFT phase manipulation — pass-through / robotization / whisperization, FFT size/hop/window |
| Tremolo | <a href="https://danielraffel.github.io/pulp-classic-effects/tremolo/"><img src="screenshots/tremolo.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/tremolo/) | Periodic amplitude modulation — depth, rate, six LFO waveforms |
| Vibrato | <a href="https://danielraffel.github.io/pulp-classic-effects/vibrato/"><img src="screenshots/vibrato.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/vibrato/) | LFO-swept fractional delay (pitch modulation) — width, rate, waveform, interpolation |
| Wah-Wah | <a href="https://danielraffel.github.io/pulp-classic-effects/wah/"><img src="screenshots/wah.png" width="220"></a> | [▶ Live demo](https://danielraffel.github.io/pulp-classic-effects/wah/) | Resonant SVF (res. low-pass / band-pass / peaking) swept manually or by LFO + envelope, with Q/gain |

Each effect also ships a dark **Ink & Signal** editor (see `*/*_editor.hpp` and
`test_editors.cpp`). Companion non-effect examples (a MIDI utility, a minimal
instrument, a UI fixture) live in
[pulp-example-plugins](https://github.com/danielraffel/pulp-example-plugins).

## Credits

These effects trace back to the worked examples in Joshua D. Reiss & Andrew
McPherson's *Audio Effects: Theory, Implementation and Application*, which Juan
Gil ported to JUCE ([juandagilc/Audio-Effects](https://github.com/juandagilc/Audio-Effects)).
truce-audio reimplemented them on the [truce](https://github.com/truce-audio/truce)
framework as [reiss-mcpherson-effects](https://github.com/truce-audio/reiss-mcpherson-effects).

This suite is an independent, clean-room reimplementation on the Pulp SDK —
derived from the book's algorithms, not copied from any of those codebases — so
the same recognizable effects show off "write one `Processor`, get every plugin
format."

## License

MIT — see [LICENSE](LICENSE). See also [Pulp licensing](https://www.generouscorp.com/pulp/licensing.html).

## Building

These examples consume the Pulp SDK. The simplest path is to scaffold against an
existing Pulp checkout/install with `pulp create`, which pins the SDK and wires
the build for you. To build this repo directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/path/to/pulp/install
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Each effect ships its behavioral test (built by default); the tests use the
Pulp SDK's reusable `pulp/format/validation_assertions.hpp` helpers
(`check_finite`, `check_peak_below`, `check_param_round_trip`,
`check_state_round_trip`, …).

### Editor screenshots

Each effect exposes its editor through `Processor::create_view()`, so the dark
Ink & Signal panel shown below is what loads in any VST3 / AU / CLAP host or the
standalone app — no extra wiring per format.

The `Editor` column above is rendered from the baselines in `screenshots/`.
`test_editors.cpp` builds each editor through `create_view()` (the real host
path), re-renders it with Skia, and compares it pixel-wise against its committed
baseline, so an unintended UI change fails CI. It also pushes every parameter
off its default and asserts the render visibly changes, proving the editor is
bound to live plugin state. After a deliberate editor change, rebake the
baselines:

```bash
PULP_BAKE_SCREENSHOTS=1 ctest --test-dir build -R editors --output-on-failure
git add screenshots && git commit -m "chore: rebake editor screenshots"
```

The test skips cleanly when the SDK build has no Skia raster backend.
