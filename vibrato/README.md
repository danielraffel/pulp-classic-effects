# Vibrato

**Pitch modulation**: an LFO sweeps the read position of a short fractional
delay line, so the output pitch wavers up and down. Unlike chorus, vibrato is
100% wet — the effect *is* the modulated signal, with no dry blend.

## What it validates (Pulp SDK contract)

- A minimal effect `Processor` with Rate / Depth and a bypass.
- Reuse of two Pulp `pulp::signal` primitives (`DelayLine` + `Oscillator`).
- Headless behavioral tests using `pulp/format/validation_assertions.hpp`
  (output diverges from the dry input; a delay can't amplify).

## Algorithm

With LFO `m(t) ∈ [-1, 1]`, depth `d` samples, and base offset `b = d + 1`:

```
read_pos(t) = b + d · m(t)           # fractional, in [1, 2d+1]
y(t) = line.read(read_pos(t))        # interpolated read after pushing x(t)
```

Sweeping the delay continuously Doppler-shifts the pitch. This is standard
textbook vibrato.
