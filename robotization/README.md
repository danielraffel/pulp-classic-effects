# Robotization

Take the short-time Fourier transform, discard each bin's phase (keep only the
magnitude), and resynthesize. Forcing zero phase restarts every frame in phase
at the analysis-hop rate, so the output adopts a steady "robot voice" pitch at
that rate while the magnitude spectrum (the formants) is preserved.

| Param | Range | Notes |
|-------|-------|-------|
| Mix | 0–100 % | dry/wet blend (dry is delay-compensated to the wet path) |
| Bypass | on/off | true latency-free passthrough |

1024-point FFT, 256-sample hop. The plugin reports its analysis latency so the
host can compensate. Clean-room implementation on Pulp's own
`pulp::signal::SpectralFrameEngine` (exact overlap-add resynthesis); no
third-party effect source was read. References in the repo
[README](../README.md#credits).
