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

| Effect | DSP | Test | Notes |
|---|---|---|---|
| Tremolo | ✅ | ✅ | Periodic amplitude modulation (sine/triangle/square LFO) |
| MIDI Transpose | ✅ | ✅ | Pure MIDI effect — semitone note shifter, passes CC/bend/SysEx through |
| MonoSynth | ✅ | ✅ | Minimal monophonic instrument (oscillator + ADSR), MIDI in → audio out |
| gui-zoo | ✅ | ✅ | UI fixture — widgets/layout/Skia paint with a deterministic screenshot baseline |
| _more effects_ | planned | planned | delay, vibrato, flanger, chorus, ping-pong, parametric EQ, wah, phaser, ring mod, compressor, … |

## Credits

Inspired by [truce-audio/reiss-mcpherson-effects](https://github.com/truce-audio/reiss-mcpherson-effects)
and the textbook it draws from — Reiss & McPherson, *Audio Effects: Theory,
Implementation and Application* (CRC Press) — and the example catalog in
[truce-audio/truce](https://github.com/truce-audio/truce). See also
[Pulp licensing](https://www.generouscorp.com/pulp/licensing.html).

## License

MIT — see [LICENSE](LICENSE).

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
