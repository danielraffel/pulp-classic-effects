#pragma once

// Auto-Pan — an LFO sweeps the stereo position with an equal-power law.
//
// Clean-room textbook auto-panner: a low-frequency oscillator drives the pan
// position between hard-left and hard-right. The per-side gains follow the
// equal-power (constant-power) law gL = cos θ, gR = sin θ with θ swept over
// [0, π/2], normalized so the centre position is unity — this keeps the
// perceived loudness constant as the image moves. Built on Pulp's own
// pulp::signal::Oscillator; no third-party effect source was read.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/oscillator.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum PanningParams : state::ParamID {
    kPanRate     = 1,  // 0.05..8 Hz
    kPanDepth    = 2,  // 0..100 %
    kPanWaveform = 3,  // 0 = sine, 1 = triangle, 2 = square
    kPanBypass   = 4,
};

// Defined out-of-line in panning_editor.hpp (included at the bottom).
std::unique_ptr<view::View> build_panning_editor(state::StateStore& store);

class PanningProcessor : public format::Processor {
public:
    std::unique_ptr<view::View> create_view() override { return build_panning_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Auto-Pan", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.panning", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kPanRate, .name = "Rate", .unit = "Hz",
                             .range = state::ParamRange::with_centre(0.05f, 8.0f, 1.0f, 1.0f)});
        store.add_parameter({.id = kPanDepth, .name = "Depth", .unit = "%",
                             .range = {0.0f, 100.0f, 80.0f, 0.0f}});
        store.add_parameter({.id = kPanWaveform, .name = "Wave", .unit = "",
                             .range = {0.0f, 2.0f, 0.0f, 1.0f}});  // stepped enum
        store.add_parameter({.id = kPanBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        lfo_.set_sample_rate(sample_rate_);
        lfo_.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t in_ch = input.num_channels();
        const std::size_t out_ch = output.num_channels();
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) lfo_.reset();

        // Pan is meaningless without a stereo bus; pass through otherwise.
        const bool bypass = state().get_value(kPanBypass) >= 0.5f;
        if (bypass || out_ch < 2) {
            for (std::size_t ch = 0; ch < std::min(in_ch, out_ch); ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, std::min(in_ch, out_ch));
            return;
        }

        lfo_.set_frequency(state().get_value(kPanRate));
        lfo_.set_waveform(waveform_from_param(state().get_value(kPanWaveform)));
        const float depth = std::clamp(state().get_value(kPanDepth) / 100.0f, 0.0f, 1.0f);
        constexpr float kQuarterPi = 0.785398163397448f;   // π/4
        constexpr float kSqrt2 = 1.4142135623730951f;

        for (std::size_t i = 0; i < frames; ++i) {
            const float inL = input.channel(0)[i];
            const float inR = (in_ch >= 2) ? input.channel(1)[i] : inL;
            // pos in [-1, 1]; depth 0 pins the image at centre (unity both sides).
            const float pos = depth * std::clamp(lfo_.next(), -1.0f, 1.0f);
            const float theta = (pos + 1.0f) * kQuarterPi;   // 0..π/2
            // ×√2 so centre (θ=π/4) gives unity gain on each side.
            const float gL = std::cos(theta) * kSqrt2;
            const float gR = std::sin(theta) * kSqrt2;
            output.channel(0)[i] = inL * gL;
            output.channel(1)[i] = inR * gR;
        }
        clear_extra(output, 2);
    }

private:
    static signal::Oscillator::Waveform waveform_from_param(float value) {
        switch (static_cast<int>(value + 0.5f)) {
            case 1:  return signal::Oscillator::Waveform::triangle;
            case 2:  return signal::Oscillator::Waveform::square;
            default: return signal::Oscillator::Waveform::sine;
        }
    }
    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }
    float sample_rate_ = 48000.0f;
    signal::Oscillator lfo_;
};

inline std::unique_ptr<format::Processor> create_panning() {
    return std::make_unique<PanningProcessor>();
}

} // namespace pulp::examples::classic

#include "panning_editor.hpp"
