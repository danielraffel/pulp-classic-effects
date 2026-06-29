# Chorus

A short **LFO-swept delay** blended with the dry signal. The sweep continuously
detunes the delayed copy, and mixing it back with the dry input produces the
shimmering "many voices" thickening.

## What it validates (Pulp SDK contract)

- A minimal effect `Processor` with Rate / Depth / Mix and a bypass.
- Reuse of two Pulp `pulp::signal` primitives (`DelayLine` + `Oscillator`).
- Headless behavioral tests using `pulp/format/validation_assertions.hpp`
  (the swept output diverges from a fixed-delay reference; mix=0 is exact dry).

## Algorithm

With a base delay `b ≈ 15 ms`, LFO `m(t) ∈ [-1, 1]`, depth `d`, and mix `x`:

```
read_pos(t) = b + d · m(t)
y(t) = (1 - x)·dry + x·line.read(read_pos(t))
```

`b > d` keeps the read position positive. At `x = 0` the output is the dry
signal; at `x = 1` it is the pure modulated copy. This is standard textbook
chorus (vibrato's swept delay, blended with the dry path).
