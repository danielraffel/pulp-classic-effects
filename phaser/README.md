# Phaser

Cascaded first-order all-pass stages swept by an LFO, summed with the dry
signal. The all-pass chain imposes a frequency-dependent phase shift; mixing it
with the dry signal creates a series of moving notches. Feedback deepens them.

| Param | Range | Notes |
|-------|-------|-------|
| Rate | 0.05–8 Hz | LFO sweep speed (skewed toward slow rates) |
| Depth | 0–100 % | how far the all-pass coefficient sweeps |
| Feedback | 0–0.9 | notch depth / resonance |
| Mix | 0–100 % | dry/wet blend (50 % gives the deepest notches) |
| Bypass | on/off | clean passthrough |

Six all-pass stages, one LFO driving all channels in phase. Clean-room
implementation on Pulp's own `pulp::signal::Oscillator` plus a one-multiply
all-pass; no third-party effect source was read. References in the repo
[README](../README.md#credits).
