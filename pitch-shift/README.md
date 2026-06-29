# Pitch Shift

Shifts pitch by **±12 semitones** without changing duration, using a two-tap
crossfading delay line. The read position is swept at a rate set by the pitch
ratio (`2^(semitones/12)`); two taps half a cycle apart are crossfaded so the
discontinuity when a tap wraps is masked.

## What it validates (Pulp SDK contract)

- An effect `Processor` with a semitone parameter, wet/dry mix, and bypass.
- Reuse of a Pulp `pulp::signal` primitive (`DelayLine`, interpolated read).
- Headless behavioral tests using `pulp/format/validation_assertions.hpp` that
  verify the *actual pitch change* by autocorrelation: shifting up an octave
  halves the detected period; shifting down doubles it. A passthrough fails them.

## Algorithm

```
ratio = 2^(semitones / 12)
phase += (1 - ratio) / window        # per sample, wrapped to [0, 1)
tap k (k = 0, 1):  ph_k = frac(phase + 0.5·k)
                   y += line.read(ph_k · window) · sin(pi · ph_k)
output = (1 - mix)·dry + mix·y
```

Unison (0 st) and bypass short-circuit to exact passthrough (the delay-line
method is not bit-identity at ratio 1). Pulp also ships a higher-fidelity
phase-vocoder pitch/time engine; this example uses the simpler, legible
delay-line method. This is a standard textbook pitch shifter.
