# Parametric EQ

A three-band **equalizer**: a low shelf, a mid peaking (bell) filter with
adjustable Q, and a high shelf, applied in series. The shelves tilt the spectrum
at the band edges; the bell boosts or cuts a focused mid region.

## What it validates (Pulp SDK contract)

- An effect `Processor` with grouped per-band parameters (freq / gain / Q).
- Reuse of a Pulp `pulp::signal` primitive (`Biquad`, RBJ-cookbook coefficients).
- Per-channel filter state held independently across blocks.
- Headless behavioral tests using `pulp/format/validation_assertions.hpp` that
  assert *band selectivity* — boosting one band raises only that band's energy
  and leaves the others ~unity — plus coefficient stability at extreme settings.

## Algorithm

Each band is a biquad. For a sample `x` and channel `c`:

```
y = high_shelf( mid_bell( low_shelf( x ) ) )
```

Coefficients are recomputed per block from the current parameters (allocation-
free), while each filter keeps its running state so parameter moves don't reset
the filter. This is standard textbook cascaded-biquad EQ.
