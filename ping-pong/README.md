# Ping-Pong Delay

A stereo delay whose echoes bounce between the left and right channels. Two
delay lines are cross-coupled — each line's feedback feeds the *other* line — so
a repeat alternates sides as it decays.

| Param | Range | Notes |
|-------|-------|-------|
| Time | 1–2000 ms | one bounce (L→R or R→L) |
| Feedback | 0–0.9 | how many bounces before the tail dies |
| Mix | 0–100 % | dry/wet blend |
| Bypass | on/off | clean passthrough |

Mono input/output degrades gracefully to a single feedback delay. Clean-room
implementation on Pulp's own `pulp::signal::DelayLine`; no third-party effect
source was read. References in the repo [README](../README.md#credits).
