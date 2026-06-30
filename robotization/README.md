# Robotization

An **STFT phase-manipulation** effect. The short-time Fourier transform is taken
each frame, the bin phases are rewritten, and the frames are resynthesized by
overlap-add. `Effect` selects what happens to the phase.

| Param | Range | Notes |
|-------|-------|-------|
| Effect | Pass-Through / Robotization / Whisperization | phase treatment (default Robotization) |
| FFT Size | 32 … 4096 | STFT frame size (default 512) |
| Hop | 1/2 / 1/4 / 1/8 | analysis/synthesis overlap (default 1/8) |
| Window | Rectangular / Bartlett / Hann / Hamming | analysis window (default Hann) |

- **Pass-Through** — spectrum untouched; COLA reconstruction is a delayed identity.
- **Robotization** — every bin's phase is set to zero, so each frame restarts in
  phase at the hop rate → a steady monotone "robot" pitch while the magnitude
  spectrum (formants) is preserved.
- **Whisperization** — each bin's phase is randomized (conjugate symmetry
  preserved) → a breathy, pitchless "whisper".

The plugin reports its analysis latency (one FFT frame) so the host can
compensate. Clean-room implementation on Pulp's own `pulp::signal::Fft`
(analysis-window-only overlap-add per the book); no third-party effect source was
copied. References in the repo [README](../README.md#credits).
