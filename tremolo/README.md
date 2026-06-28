# Tremolo

Periodic **amplitude modulation**: a low-frequency oscillator (LFO) sweeps the
signal's gain, producing the familiar pulsing tremolo effect.

## What it validates (Pulp SDK contract)

- A minimal effect `Processor`: `descriptor()`, `define_parameters()`,
  `prepare()`, `process()`, factory.
- A skewed parameter range (`Rate`, more resolution at slow rates), a percentage
  parameter (`Depth`), a stepped enum (`Waveform`), and a bypass.
- Reuse of a Pulp `pulp::signal` primitive (`Oscillator`) as the LFO.
- Headless behavioral tests using `pulp/format/validation_assertions.hpp`.

## Algorithm

With depth `d ∈ [0,1]` and a unipolar LFO `m(t) ∈ [0,1]`, the per-sample gain is:

```
g(t) = (1 - d) + d · m(t)
```

so `d = 0` is unity (no-op) and `d = 1` sweeps the gain over the full `[0,1]`
range each LFO cycle. One LFO drives all channels in phase. This is standard
textbook amplitude modulation.
