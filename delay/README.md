# Delay

A **feedback delay line** with wet/dry mix: each sample is read back after a set
**Time**, fed back into the line by a **Feedback** amount, and blended with the
dry signal by **Mix**. This is the classic echo effect.

## What it validates (Pulp SDK contract)

- A minimal effect `Processor` with continuous Time / Feedback / Mix and a bypass.
- Reuse of a Pulp `pulp::signal` primitive (`DelayLine`) with fractional read.
- Headless behavioral tests using `pulp/format/validation_assertions.hpp`
  (impulse lands at the set delay; feedback stays bounded).

## Algorithm

With delay `D` samples, feedback `f ∈ [0, 0.95]`, and mix `m ∈ [0,1]`:

```
w(n) = line.read(D)                 # delayed sample
line.push(x(n) + f · w(n))          # feed back into the line
y(n) = (1 - m)·x(n) + m·w(n)
```

Feedback is capped below 1 so the echo tail always decays. This is standard
textbook delay.
