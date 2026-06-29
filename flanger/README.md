# Flanger

A short, LFO-swept delay with feedback, mixed with the dry signal. Sweeping a
sub-millisecond-to-few-millisecond delay produces a moving comb filter; the
feedback path sharpens the notches into the classic "jet plane" whoosh.

| Param | Range | Notes |
|-------|-------|-------|
| Rate | 0.05–8 Hz | LFO sweep speed (skewed toward slow rates) |
| Depth | 0–4 ms | sweep excursion around the ~1 ms base delay |
| Feedback | 0–0.9 | recirculation; higher = sharper, more resonant notches |
| Mix | 0–100 % | dry/wet blend |
| Bypass | on/off | clean passthrough |

Clean-room implementation built on Pulp's own `pulp::signal::DelayLine` and
`Oscillator`. No third-party effect source was read or transcribed. The
algorithm is the standard swept-comb flanger described in the references in the
repository [README](../README.md#credits).
