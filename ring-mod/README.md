# Ring Mod

**Ring modulation**: multiply the input by a sine *carrier*, producing the
metallic, inharmonic timbre used for bells, robot voices, and classic sci-fi
effects. A wet/dry **Mix** blends the modulated signal back with the dry input.

## What it validates (Pulp SDK contract)

- A minimal effect `Processor` with a wet/dry mix and a bypass.
- Reuse of a Pulp `pulp::signal` primitive (`Oscillator`) as the carrier.
- Headless behavioral tests using `pulp/format/validation_assertions.hpp`.

## Algorithm

With carrier `c(t) = sin(2π f t)`, dry input `x(t)`, and mix `m ∈ [0,1]`:

```
y(t) = (1 - m)·x(t) + m·(x(t)·c(t))
```

At `m = 1` the output is pure ring modulation (`x·c`); at `m = 0` it is the dry
signal. This is standard textbook amplitude (ring) modulation.
