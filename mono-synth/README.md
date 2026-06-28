# MonoSynth

A minimal **monophonic instrument**: one band-limited oscillator whose pitch
tracks the most recent MIDI note, shaped by an ADSR envelope. The smallest
honest "MIDI in → audio out" synth voice.

## What it validates (Pulp SDK contract)

- The instrument descriptor (`PluginCategory::Instrument`, `accepts_midi`, an
  audio output bus, no input bus).
- MIDI note-on/note-off handling at sample offsets inside `process()`.
- Reuse of `pulp::signal::Oscillator` + `pulp::signal::Adsr`.
- Headless behavioral tests via `pulp/format/validation_assertions.hpp`: silent
  before any note, sounds on note-on, decays after note-off, and state
  round-trips.

## Note on parameters

The ADSR controls use **linear** ranges so plugin state serialization is
bit-exact. A skewed/tapered range only round-trips through the host's normalized
domain to float precision, which a strict state-determinism check (and DAW A/B
recall) will notice. For a teaching example, deterministic state beats a tapered
control feel.
