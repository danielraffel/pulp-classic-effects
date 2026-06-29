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

| Effect | Editor | DSP | Test | Notes |
|---|---|---|---|---|
| Tremolo | <img src="screenshots/tremolo.png" width="220"> | ✅ | ✅ | Periodic amplitude modulation (sine/triangle/square LFO) |
| Ring Mod | <img src="screenshots/ring-mod.png" width="220"> | ✅ | ✅ | Sine-carrier ring modulation with wet/dry mix |
| Delay | <img src="screenshots/delay.png" width="220"> | ✅ | ✅ | Feedback delay line with wet/dry mix |
| Vibrato | <img src="screenshots/vibrato.png" width="220"> | ✅ | ✅ | LFO-swept fractional delay (pitch modulation) |
| Chorus | <img src="screenshots/chorus.png" width="220"> | ✅ | ✅ | LFO-swept short delay blended with the dry signal |
| Comp/Expander | <img src="screenshots/compressor-expander.png" width="220"> | ✅ | ✅ | Stereo-linked dynamics — compress above / expand below, with makeup |
| Parametric EQ | <img src="screenshots/parametric-eq.png" width="220"> | ✅ | ✅ | Three-band EQ (low shelf / mid bell / high shelf), per-band freq/gain/Q |
| Wah | <img src="screenshots/wah.png" width="220"> | ✅ | ✅ | Resonant bandpass swept manually or by the input envelope (auto-wah) |
| Pitch Shift | <img src="screenshots/pitch-shift.png" width="220"> | ✅ | ✅ | ±12-semitone two-tap crossfading delay-line pitch shifter |
| _more effects_ | | planned | planned | flanger, ping-pong, phaser, distortion, panning, robotization, … |

Each effect also ships a dark **Ink & Signal** editor (see `*/*_editor.hpp` and
`test_editors.cpp`). Companion non-effect examples (a MIDI utility, a minimal
instrument, a UI fixture) live in
[pulp-example-plugins](https://github.com/danielraffel/pulp-example-plugins).

## Credits

Inspired by truce-audio's [reiss-mcpherson-effects](https://github.com/truce-audio/reiss-mcpherson-effects)
and [truce](https://github.com/truce-audio/truce), and the *Audio Effects* book
by Reiss & McPherson.

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
