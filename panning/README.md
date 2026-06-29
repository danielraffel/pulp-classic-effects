# Auto-Pan

An LFO sweeps the stereo position between hard-left and hard-right using the
equal-power law (gL = cos θ, gR = sin θ), normalized so the centre is unity —
the perceived loudness stays constant as the image moves.

| Param | Range | Notes |
|-------|-------|-------|
| Rate | 0.05–8 Hz | sweep speed |
| Depth | 0–100 % | how far the image travels (0 % pins it to centre) |
| Wave | sine / triangle / square | LFO shape (square = hard auto-pan) |
| Bypass | on/off | clean passthrough |

Requires a stereo bus; a mono bus passes through unchanged. Clean-room
implementation on Pulp's own `pulp::signal::Oscillator`; no third-party effect
source was read. References in the repo [README](../README.md#credits).
