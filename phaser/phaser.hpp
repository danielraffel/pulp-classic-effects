#pragma once

// Phaser — cascaded first-order all-pass stages swept by an LFO, mixed with dry.
//
// Clean-room textbook phaser: a chain of first-order all-pass filters imposes a
// frequency-dependent phase shift; summing that phase-shifted copy with the dry
// signal produces a series of moving notches as an LFO sweeps the all-pass
// coefficient. A feedback path deepens the notches. Built on Pulp's own
// pulp::signal::Oscillator plus a one-multiply all-pass; no third-party effect
// source was read. See the README for the algorithmic reference.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/oscillator.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum PhaserParams : state::ParamID {
    kPhaserRate     = 1,  // 0.05..8 Hz
    kPhaserDepth    = 2,  // 0..100 %
    kPhaserFeedback = 3,  // 0..0.9
    kPhaserMix      = 4,  // 0..100 %
    kPhaserBypass   = 5,
};

// Defined out-of-line in phaser_editor.hpp (included at the bottom).
std::unique_ptr<view::View> build_phaser_editor(state::StateStore& store);

class PhaserProcessor : public format::Processor {
public:
    std::unique_ptr<view::View> create_view() override { return build_phaser_editor(state()); }

    static constexpr int kStages = 6;

    format::PluginDescriptor descriptor() const override {
        return {.name = "Phaser", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.phaser", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kPhaserRate, .name = "Rate", .unit = "Hz",
                             .range = state::ParamRange::with_centre(0.05f, 8.0f, 0.5f, 0.5f)});
        store.add_parameter({.id = kPhaserDepth, .name = "Depth", .unit = "%",
                             .range = {0.0f, 100.0f, 70.0f, 0.0f}});
        store.add_parameter({.id = kPhaserFeedback, .name = "Feedback", .unit = "",
                             .range = {0.0f, 0.9f, 0.3f, 0.0f}});
        store.add_parameter({.id = kPhaserMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 50.0f, 0.0f}});
        store.add_parameter({.id = kPhaserBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        lfo_.set_sample_rate(sample_rate_);
        lfo_.set_waveform(signal::Oscillator::Waveform::sine);
        lfo_.reset();
        reset_state();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), z_.size()});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) { reset_state(); lfo_.reset(); }

        if (state().get_value(kPhaserBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        lfo_.set_frequency(state().get_value(kPhaserRate));
        const float mix = std::clamp(state().get_value(kPhaserMix) / 100.0f, 0.0f, 1.0f);
        const float fb = std::clamp(state().get_value(kPhaserFeedback), 0.0f, 0.9f);
        const float depth = std::clamp(state().get_value(kPhaserDepth) / 100.0f, 0.0f, 1.0f);

        for (std::size_t i = 0; i < frames; ++i) {
            // One LFO sweeps the all-pass coefficient for every channel in phase.
            const float mod = 0.5f * (lfo_.next() + 1.0f);   // 0..1
            // Keep the coefficient inside (0, 1) for all-pass stability.
            const float g = std::clamp(0.5f + depth * 0.45f * (2.0f * mod - 1.0f),
                                       0.02f, 0.97f);
            for (std::size_t ch = 0; ch < channels; ++ch) {
                const float dry = input.channel(ch)[i];
                float x = dry + fb_[ch] * fb;
                auto& z = z_[ch];
                for (int s = 0; s < kStages; ++s) {
                    const float t = x - g * z[s];   // one-multiply all-pass
                    const float y = g * t + z[s];
                    z[s] = t;
                    x = y;
                }
                if (std::fabs(x) < 1e-30f) x = 0.0f;  // flush feedback denormals
                fb_[ch] = x;
                output.channel(ch)[i] = dry * (1.0f - mix) + x * mix;
            }
        }
        clear_extra(output, channels);
    }

private:
    void reset_state() {
        for (auto& z : z_) z.fill(0.0f);
        fb_.fill(0.0f);
    }
    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }
    float sample_rate_ = 48000.0f;
    std::array<std::array<float, kStages>, 8> z_{};
    std::array<float, 8> fb_{};
    signal::Oscillator lfo_;
};

inline std::unique_ptr<format::Processor> create_phaser() {
    return std::make_unique<PhaserProcessor>();
}

} // namespace pulp::examples::classic

#include "phaser_editor.hpp"
