#pragma once

// Pitch Shift — a two-tap crossfading delay-line pitch shifter.
//
// Clean-room textbook pitch shift: the read position of a delay line is swept at
// a rate set by the pitch ratio (2^(semitones/12)), so the output is resampled
// to a new pitch without changing duration. Two read taps half a cycle apart are
// crossfaded with raised-sine windows so the discontinuity when a tap wraps is
// masked. Built on Pulp's own pulp::signal::DelayLine; no third-party effect
// source was read. (Pulp also ships a phase-vocoder pitch/time engine for
// high-fidelity work; this example deliberately uses the simpler delay-line
// method to stay legible.)

#include <pulp/format/processor.hpp>
#include <pulp/signal/delay_line.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum PitchShiftParams : state::ParamID {
    kPitchSemitones = 1,  // -12..12 semitones
    kPitchMix       = 2,  // 0..100 %
    kPitchBypass    = 3,
};

class PitchShiftProcessor : public format::Processor {
public:
    format::PluginDescriptor descriptor() const override {
        return {.name = "Pitch Shift", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.pitch-shift", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kPitchSemitones, .name = "Pitch", .unit = "st",
                             .range = {-12.0f, 12.0f, 0.0f, 0.0f}});
        store.add_parameter({.id = kPitchMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 100.0f, 0.0f}});
        store.add_parameter({.id = kPitchBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    // Latency is deliberately reported as 0 (the base Processor default): the
    // wet signal is a *different pitch*, so no fixed PDC value can time-align it
    // to dry. Declaring a latency would itself be misleading for this algorithm.

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        window_ = 2048.0f;  // ~43 ms @ 48k — longer = smoother, shorter = tighter
        for (auto& line : lines_) line.prepare(static_cast<int>(window_) + 8);
        phase_ = 0.0f;
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), lines_.size()});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) {
            for (auto& line : lines_) line.reset();
            phase_ = 0.0f;
        }

        const float semis = state().get_value(kPitchSemitones);
        const float mix = std::clamp(state().get_value(kPitchMix) / 100.0f, 0.0f, 1.0f);

        if (state().get_value(kPitchBypass) >= 0.5f || std::fabs(semis) < 0.01f) {
            // Bypass and unison are exact passthrough (the delay-line method is
            // not bit-identity at ratio 1, so short-circuit it).
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        const float ratio = std::pow(2.0f, semis / 12.0f);
        const float inc = (1.0f - ratio) / window_;   // phase step per sample
        constexpr float kPi = 3.14159265358979323846f;

        for (std::size_t i = 0; i < frames; ++i) {
            for (std::size_t ch = 0; ch < channels; ++ch)
                lines_[ch].push(input.channel(ch)[i]);

            const float ph0 = phase_ - std::floor(phase_);
            float ph1 = phase_ + 0.5f; ph1 -= std::floor(ph1);
            const float w0 = std::sin(kPi * ph0);
            const float w1 = std::sin(kPi * ph1);
            const float d0 = ph0 * window_;
            const float d1 = ph1 * window_;

            for (std::size_t ch = 0; ch < channels; ++ch) {
                const float wet = lines_[ch].read(d0) * w0 + lines_[ch].read(d1) * w1;
                const float dry = input.channel(ch)[i];
                output.channel(ch)[i] = dry * (1.0f - mix) + wet * mix;
            }
            phase_ += inc;
            phase_ -= std::floor(phase_);   // keep in [0, 1)
        }
        clear_extra(output, channels);
    }

private:
    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }
    float sample_rate_ = 48000.0f;
    float window_ = 2048.0f;
    float phase_ = 0.0f;
    std::array<signal::DelayLine, 8> lines_{};
};

inline std::unique_ptr<format::Processor> create_pitch_shift() {
    return std::make_unique<PitchShiftProcessor>();
}

} // namespace pulp::examples::classic
