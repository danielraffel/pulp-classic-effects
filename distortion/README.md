# Distortion

Drive the input into a `tanh` soft-clipper, shape the result with a one-pole
"tone" low-pass, then trim level and blend with the dry signal. The tanh curve
is odd-symmetric, so it adds the warm odd-harmonic series without the harsh
edges of hard clipping.

| Param | Range | Notes |
|-------|-------|-------|
| Drive | 1–50 | pre-gain into the saturator (skewed for fine low-drive control) |
| Tone | 0–100 % | one-pole low-pass, ~500 Hz (dark) → ~12 kHz (bright) |
| Level | 0–100 % | output trim after shaping |
| Mix | 0–100 % | dry/wet blend |
| Bypass | on/off | clean passthrough |

Clean-room implementation on `std::tanh` plus a one-pole filter; no third-party
effect source was read. References in the repo [README](../README.md#credits).
