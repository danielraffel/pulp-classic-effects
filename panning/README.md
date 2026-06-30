# Panning

A **static stereo panner** with a selectable panning law. The position is fixed
by the `Pan` control; `Method` chooses how that position is realized.

| Param | Range | Notes |
|-------|-------|-------|
| Method | Pan+Pre / ITD+ILD | panning law (default ITD+ILD) |
| Pan | 0–1 | 0 = hard left, 0.5 = centre, 1 = hard right |

- **Pan+Pre** — constant-power amplitude pan (gL = cos θ, gR = sin θ) plus a
  short precedence/Haas delay on the attenuated side so the louder side leads.
- **ITD+ILD** — a spherical-head binaural model: an interaural *time* difference
  (per-ear fractional delay, near ear leads) and an interaural *level* difference
  (per-ear Brown–Duda head-shadow shelf, near ear bright / far ear dull).

Clean-room implementation derived from the textbook panning laws (Reiss &
McPherson); no third-party effect source was copied. References in the repo
[README](../README.md#credits).
