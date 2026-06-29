# Comp/Expander

A stereo-linked **dynamics processor**: a peak envelope follower tracks the
program level, then a static gain curve **compresses** levels above the
compressor threshold and **expands** (further attenuates) levels below the
expander threshold. A makeup gain restores output level.

## What it validates (Pulp SDK contract)

- A richer effect `Processor` with eight grouped parameters (two thresholds, two
  ratios, attack, release, makeup, bypass).
- Reuse of a Pulp `pulp::signal` primitive (`BallisticsFilter`) as the
  attack/release envelope follower.
- A stereo-linked detector (one gain across channels preserves the image).
- A `gain_reduction_db()` readout for a meter.
- Headless behavioral tests using `pulp/format/validation_assertions.hpp` that
  assert *level-dependent* behavior (loud is attenuated, quiet is not; signals
  below the expander threshold are pushed further down) — a fixed gain fails them.

## Algorithm

With the detected level `L` in dB:

```
above comp threshold T_c:  gain = (T_c - L) · (1 - 1/R_c)     # downward compression
below exp threshold T_e:   gain = (L - T_e) · (R_e - 1)       # downward expansion
otherwise:                 gain = 0                            # unity region
output = input · 10^((gain + makeup) / 20)
```

`T_e` is clamped at or below `T_c` so the unity region never inverts. This is
standard textbook dynamics processing.
