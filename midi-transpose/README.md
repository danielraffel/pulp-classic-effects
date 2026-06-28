# MIDI Transpose

A pure **MIDI effect**: shift incoming note pitches up or down by a semitone
offset; everything else (CC, pitch bend, program change, SysEx) passes through
unchanged. Notes that would transpose outside the 0–127 range are dropped rather
than wrapped.

## What it validates (Pulp SDK contract)

- The MIDI-effect descriptor (`PluginCategory::MidiEffect`, `accepts_midi` +
  `produces_midi`, no audio buses).
- The `midi_in -> midi_out` path in `process()`.
- A stepped integer parameter (`Semitones`, −24..+24).
- Headless MIDI assertions via `pulp/format/validation_assertions.hpp`
  (`check_midi_events_equal`, including the SysEx sidecar pass-through).

## Reference-Lineage

Clean-room. Transposition is trivially standard MIDI processing (add an offset to
the note number); this is an independent implementation on Pulp's `midi` types.
No third-party source was read or transcribed.
