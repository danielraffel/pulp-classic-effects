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

## Attribution & clean-room policy

These effects implement **standard, well-documented DSP from the academic
literature**. The canonical algorithmic reference is:

> Joshua D. Reiss & Andrew P. McPherson, *Audio Effects: Theory, Implementation
> and Application* (CRC Press, 2014).

Pulp's implementations are **independent and clean-room**: each effect is written
from the published textbook math and Pulp's own `pulp::signal` primitives
(oscillators, delay lines, filters, dynamics). No third-party effect source code
was read, transcribed, or ported — we learned the *shape* of these classic
effects; the implementation is our own. Where a specific effect leans on a public
algorithm, that lineage is noted in the effect's own `README.md`.

This mirrors [Pulp's own licensing and clean-room discipline](https://www.generouscorp.com/pulp/licensing.html):
"Implementation is from specs, documentation, and original design — never from
studying proprietary or restrictively-licensed source code."

**Acknowledgement.** We learned about this textbook and this curated set of
classic effects from [truce-audio/reiss-mcpherson-effects](https://github.com/truce-audio/reiss-mcpherson-effects),
which catalogues the same effects on a different framework. We used it (and the
book it points to) as inspiration for *which* effects to build and how a clean
example suite feels — not as a code source. Our DSP is written independently
from the textbook math and Pulp's own primitives.

## License

MIT — see [LICENSE](LICENSE). The textbook above is a published reference work;
citing it is scholarship, not a code dependency. This repository bundles no
third-party effect code.

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
