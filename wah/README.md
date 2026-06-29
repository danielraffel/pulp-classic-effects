# Wah

A resonant **bandpass swept in frequency**. In *manual* mode the centre
frequency follows the Freq parameter (a pedal); in *envelope* mode the input
level sweeps the centre (auto-wah). Sweeping a resonant peak across the spectrum
gives the classic vowel-like "wah".

## What it validates (Pulp SDK contract)

- An effect `Processor` with a stepped mode parameter plus freq/resonance/
  sensitivity/mix and a bypass.
- Reuse of two Pulp `pulp::signal` primitives (`Svf` bandpass + `BallisticsFilter`).
- A stereo-linked envelope detector driving per-channel filters.
- Headless behavioral tests using `pulp/format/validation_assertions.hpp` that
  assert *frequency mapping* (the centre tracks the param; off-band is rejected),
  that resonance raises the peak, and that envelope mode sweeps the centre with
  input level — a level-independent filter fails the last one.

## Algorithm

```
manual:    centre = Freq
envelope:  centre = Freq + level · Sensitivity        # level from a peak follower
y = (1 - mix)·dry + mix · bandpass(dry; centre, resonance)
```

`centre` is clamped to `[100 Hz, 0.45·sr]`. This is standard textbook wah /
envelope-filter processing.
