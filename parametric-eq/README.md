# Parametric EQ

A **single-band** equalizer with a selectable filter type — one biquad whose
response is chosen from six classic shapes plus a peaking bell.

| Param | Range | Notes |
|-------|-------|-------|
| Freq | 10–20000 Hz | centre / corner frequency (default 1500) |
| Q | 0.1–20 | resonance / bandwidth (default √2) |
| Gain | -12…+12 dB | boost/cut (used by the shelf and peaking types) |
| Type | Low-Pass / High-Pass / Low-Shelf / High-Shelf / Band-Pass / Band-Stop / Peaking | filter shape (default Peaking) |

## What it validates (Pulp SDK contract)

- An effect `Processor` with a continuous freq/Q/gain set plus a discrete filter
  Type selector.
- Reuse of a Pulp `pulp::signal` primitive (`Biquad`, RBJ-cookbook coefficients).
- Per-channel filter state held independently across blocks.
- Headless behavioral tests using `pulp/format/validation_assertions.hpp` that
  assert each type's response — low-pass rolls off highs, peaking boosts at the
  centre by Gain, band-stop notches, etc. — plus stability at extreme Q/gain.

## Algorithm

One biquad per channel; coefficients recomputed per block from the current
parameters (allocation-free) per the type, while each filter keeps its running
state so parameter moves don't reset it. Clean-room implementation from the
standard RBJ audio-EQ cookbook (Reiss & McPherson); no third-party effect source
was copied. References in the repo [README](../README.md#credits).
