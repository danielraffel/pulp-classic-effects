# Pitch Shift

Shifts pitch by **±12 semitones** without changing duration, using a **phase
vocoder**: a short-time Fourier transform estimates each bin's true frequency,
the phases are advanced by the pitch ratio, and the frames are resynthesized and
overlap-added.

| Param | Range | Notes |
|-------|-------|-------|
| Shift | -12…+12 st | pitch shift in semitones (0 = unison) |
| FFT Size | 256 / 512 / 1024 / 2048 / 4096 | STFT frame size (default 512) |
| Hop | 1/2 / 1/4 / 1/8 | analysis/synthesis overlap (default 1/8) |
| Window | Bartlett / Hann / Hamming | analysis window (default Hann) |

## What it validates (Pulp SDK contract)

- An effect `Processor` with a semitone parameter plus discrete FFT/hop/window
  selectors, and a reported latency of one FFT frame.
- Reuse of a Pulp `pulp::signal` primitive (`Fft`, radix-2, vDSP-accelerated).
- Headless behavioral tests using `pulp/format/validation_assertions.hpp` that
  verify the *actual pitch change*: shifting up an octave halves the detected
  period; shifting down doubles it; Shift = 0 preserves the period.

## Algorithm

STFT analysis with a √window → per-bin true-frequency estimate via the
principal-argument of the heterodyned phase deviation → output phase advanced by
`ratio` → inverse FFT → resample by 1/ratio with the √synthesis window →
overlap-add. The pitch ratio is snapped to the integer-hop grid to suppress
inter-hop phase drift. Clean-room implementation from the textbook phase-vocoder
recipe (Reiss & McPherson); no third-party effect source was copied. References
in the repo [README](../README.md#credits).
