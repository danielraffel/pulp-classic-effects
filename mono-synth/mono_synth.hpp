#pragma once

// MonoSynth — a minimal monophonic subtractive-style synth voice.
//
// Clean-room instrument example: one band-limited oscillator whose pitch tracks
// the most recent MIDI note, shaped by an ADSR envelope. It demonstrates the
// Pulp instrument contract (PluginCategory::Instrument, accepts_midi, MIDI
// note-on/off -> audio) using only Pulp's own `pulp::signal` primitives
// (Oscillator + Adsr). There is no novel DSP here — it is the textbook
// "oscillator * envelope" voice, kept deliberately small as a teaching example.

#include <pulp/format/processor.hpp>
#include <pulp/signal/adsr.hpp>
#include <pulp/signal/oscillator.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum MonoSynthParams : state::ParamID {
    kWaveform = 1,  // 0 sine, 1 saw, 2 square, 3 triangle
    kAttack   = 2,  // seconds
    kDecay    = 3,
    kSustain  = 4,  // 0..1
    kRelease  = 5,
};

class MonoSynthProcessor : public format::Processor {
public:
    format::PluginDescriptor descriptor() const override {
        return {
            .name = "MonoSynth",
            .manufacturer = "Pulp Examples",
            .bundle_id = "com.pulp.examples.mono-synth",
            .version = "0.1.0",
            .category = format::PluginCategory::Instrument,
            .input_buses = {},
            .output_buses = {{"Audio Out", 2}},
            .accepts_midi = true,
            .produces_midi = false,
        };
    }

    void define_parameters(state::StateStore& store) override {
        // Linear ranges keep state serialization bit-exact (a skewed range
        // round-trips through the host's normalized domain only to float
        // precision); for a simple example that determinism is worth more than
        // a tapered control feel.
        store.add_parameter({.id = kWaveform, .name = "Waveform", .unit = "",
                             .range = {0.0f, 3.0f, 1.0f, 1.0f}});
        store.add_parameter({.id = kAttack, .name = "Attack", .unit = "s",
                             .range = {0.001f, 2.0f, 0.05f, 0.0f}});
        store.add_parameter({.id = kDecay, .name = "Decay", .unit = "s",
                             .range = {0.001f, 2.0f, 0.1f, 0.0f}});
        store.add_parameter({.id = kSustain, .name = "Sustain", .unit = "",
                             .range = {0.0f, 1.0f, 0.7f, 0.0f}});
        store.add_parameter({.id = kRelease, .name = "Release", .unit = "s",
                             .range = {0.001f, 3.0f, 0.2f, 0.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        const float sr = static_cast<float>(ctx.sample_rate);
        osc_.set_sample_rate(sr);
        env_.set_sample_rate(sr);
        osc_.reset();
        env_.reset();
        active_note_ = -1;
    }

    void release() override {
        osc_.reset();
        env_.reset();
        active_note_ = -1;
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>&,
                 midi::MidiBuffer& midi_in,
                 midi::MidiBuffer&,
                 const format::ProcessContext&) override {
        osc_.set_waveform(waveform_from_param(state().get_value(kWaveform)));
        env_.set_params({state().get_value(kAttack), state().get_value(kDecay),
                         std::clamp(state().get_value(kSustain), 0.0f, 1.0f),
                         state().get_value(kRelease)});

        const std::size_t frames = output.num_samples();
        // Sort by sample offset ourselves — HeadlessHost (and not every adapter)
        // guarantees ordering. Then fire each event once its (clamped) offset
        // has been reached, using <= so an out-of-range/negative offset still
        // fires at sample 0 rather than being silently dropped.
        midi_in.sort();
        std::size_t event = 0;
        const std::size_t n_events = midi_in.size();

        for (std::size_t i = 0; i < frames; ++i) {
            while (event < n_events &&
                   static_cast<std::size_t>(std::max(0, midi_in[event].sample_offset)) <= i) {
                apply_event(midi_in[event]);
                ++event;
            }
            const float s = osc_.next() * env_.next() * velocity_gain_;
            for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
                output.channel(ch)[i] = s;
            }
        }
        // Drain any remaining events (offsets past the block end).
        for (; event < n_events; ++event) apply_event(midi_in[event]);
    }

private:
    void apply_event(const midi::MidiEvent& ev) {
        if (ev.is_note_on() && ev.velocity() > 0) {
            active_note_ = ev.note();
            active_channel_ = ev.channel();
            osc_.set_frequency(note_to_hz(ev.note()));
            velocity_gain_ = ev.velocity() / 127.0f;
            env_.note_on();
        } else if (ev.is_note_off() ||
                   (ev.is_note_on() && ev.velocity() == 0)) {
            // Only release if the note-off matches the sounding note AND channel
            // (a note-off for the same number on another channel must not steal
            // this voice's release).
            if (ev.note() == active_note_ && ev.channel() == active_channel_) {
                env_.note_off();
                active_note_ = -1;
            }
        }
    }

    static float note_to_hz(int note) {
        return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
    }

    static signal::Oscillator::Waveform waveform_from_param(float v) {
        switch (static_cast<int>(v + 0.5f)) {
            case 0:  return signal::Oscillator::Waveform::sine;
            case 2:  return signal::Oscillator::Waveform::square;
            case 3:  return signal::Oscillator::Waveform::triangle;
            default: return signal::Oscillator::Waveform::saw;
        }
    }

    signal::Oscillator osc_;
    signal::Adsr env_;
    int active_note_ = -1;
    int active_channel_ = -1;
    float velocity_gain_ = 0.0f;
};

inline std::unique_ptr<format::Processor> create_mono_synth() {
    return std::make_unique<MonoSynthProcessor>();
}

} // namespace pulp::examples::classic
