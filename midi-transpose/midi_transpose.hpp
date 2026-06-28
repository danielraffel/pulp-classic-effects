#pragma once

// MIDI Transpose — shift incoming note pitches by a semitone offset.
//
// A minimal MIDI effect: it reads note-on/note-off messages from the
// input, adds a transpose offset (in semitones, clamped to the 0..127 note
// range) and emits them on the output; every other message (CC, pitch bend,
// program change, SysEx) passes through unchanged. There is no creative DSP
// here — it is a transparent demonstration of Pulp's MIDI-effect contract
// (accepts_midi / produces_midi, the midi_in -> midi_out path in process()).

#include <pulp/format/processor.hpp>

#include <algorithm>
#include <memory>

namespace pulp::examples::classic {

enum MidiTransposeParams : state::ParamID {
    kSemitones = 1,  // -24 .. +24
};

class MidiTransposeProcessor : public format::Processor {
public:
    format::PluginDescriptor descriptor() const override {
        return {
            .name = "MidiTranspose",
            .manufacturer = "Pulp Examples",
            .bundle_id = "com.pulp.examples.midi-transpose",
            .version = "0.1.0",
            .category = format::PluginCategory::MidiEffect,
            .input_buses = {},
            .output_buses = {},
            .accepts_midi = true,
            .produces_midi = true,
        };
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({
            .id = kSemitones,
            .name = "Semitones",
            .unit = "st",
            .range = {-24.0f, 24.0f, 0.0f, 1.0f},  // stepped, integer semitones
        });
    }

    void prepare(const format::PrepareContext&) override {}

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer& midi_in,
                 midi::MidiBuffer& midi_out,
                 const format::ProcessContext&) override {
        // Pure MIDI effect: pass audio through untouched (if any bus exists).
        for (std::size_t ch = 0;
             ch < output.num_channels() && ch < input.num_channels(); ++ch) {
            auto in = input.channel(ch);
            auto out = output.channel(ch);
            for (std::size_t i = 0; i < out.size(); ++i) out[i] = in[i];
        }

        const int offset = static_cast<int>(state().get_value(kSemitones));

        for (const auto& ev : midi_in) {
            if ((ev.is_note_on() || ev.is_note_off()) && offset != 0) {
                const int shifted = static_cast<int>(ev.note()) + offset;
                if (shifted < 0 || shifted > 127) {
                    // Transposed out of range — drop the note rather than wrap.
                    continue;
                }
                auto shifted_ev =
                    ev.is_note_on()
                        ? midi::MidiEvent::note_on(ev.channel(),
                                                   static_cast<uint8_t>(shifted),
                                                   ev.velocity())
                        : midi::MidiEvent::note_off(ev.channel(),
                                                    static_cast<uint8_t>(shifted),
                                                    ev.velocity());
                shifted_ev.sample_offset = ev.sample_offset;
                midi_out.add(shifted_ev);
            } else {
                midi_out.add(ev);
            }
        }

        // SysEx and other sidecar payloads pass through verbatim.
        for (const auto& sx : midi_in.sysex()) {
            midi_out.add_sysex_copy(sx.data.data(), sx.data.size(),
                                    sx.sample_offset, sx.timestamp);
        }
    }

private:
};

inline std::unique_ptr<format::Processor> create_midi_transpose() {
    return std::make_unique<MidiTransposeProcessor>();
}

} // namespace pulp::examples::classic
