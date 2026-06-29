#pragma once

// Flanger — a short, LFO-swept delay with feedback, blended with the dry signal.
//
// Clean-room textbook flanger: a low-frequency oscillator sweeps the read
// position of a very short fractional delay (sub-millisecond up to a few ms).
// Unlike the chorus, a feedback path routes the delayed copy back into the
// line, sharpening the swept comb-filter notches into the characteristic
// "jet plane" whoosh. Built on Pulp's own pulp::signal::DelayLine + Oscillator;
// no third-party effect source was read. See the README for the algorithmic
// reference.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/delay_line.hpp>
#include <pulp/signal/oscillator.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum FlangerParams : state::ParamID {
    kFlangerRate     = 1,  // 0.05..8 Hz
    kFlangerDepth    = 2,  // 0..4 ms sweep
    kFlangerFeedback = 3,  // 0..0.9
    kFlangerMix      = 4,  // 0..100 %
    kFlangerBypass   = 5,
};

// Defined out-of-line in flanger_editor.hpp (included at the bottom of this
// file). Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
std::unique_ptr<view::View> build_flanger_editor(state::StateStore& store);

class FlangerProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
    std::unique_ptr<view::View> create_view() override { return build_flanger_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Flanger", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.flanger", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kFlangerRate, .name = "Rate", .unit = "Hz",
                             .range = state::ParamRange::with_centre(0.05f, 8.0f, 0.5f, 0.5f)});
        store.add_parameter({.id = kFlangerDepth, .name = "Depth", .unit = "ms",
                             .range = {0.0f, 4.0f, 2.0f, 0.0f}});
        store.add_parameter({.id = kFlangerFeedback, .name = "Feedback", .unit = "",
                             .range = {0.0f, 0.9f, 0.5f, 0.0f}});
        store.add_parameter({.id = kFlangerMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 50.0f, 0.0f}});
        store.add_parameter({.id = kFlangerBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        base_samples_ = 0.001f * sample_rate_;                    // ~1 ms centre
        max_delay_ = static_cast<int>(sample_rate_ * 0.006f) + 4; // 6 ms headroom
        for (auto& line : lines_) line.prepare(max_delay_);
        lfo_.set_sample_rate(sample_rate_);
        lfo_.set_waveform(signal::Oscillator::Waveform::sine);
        lfo_.reset();
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
            lfo_.reset();
        }

        if (state().get_value(kFlangerBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        lfo_.set_frequency(state().get_value(kFlangerRate));
        const float mix = std::clamp(state().get_value(kFlangerMix) / 100.0f, 0.0f, 1.0f);
        const float fb = std::clamp(state().get_value(kFlangerFeedback), 0.0f, 0.9f);
        float depth = state().get_value(kFlangerDepth) / 1000.0f * sample_rate_;
        // The unipolar sweep rides on top of the base delay: read_pos =
        // base + depth·mod with mod ∈ [0,1], so the minimum read position is
        // always `base` (no underrun). Cap the maximum so base + depth stays
        // inside the allocated line rather than at the old, far-too-tight
        // `base - 1` (which silently throttled the 0–4 ms knob to ~1 ms).
        depth = std::clamp(depth, 0.0f,
                           std::max(0.0f, static_cast<float>(max_delay_) - base_samples_ - 2.0f));

        for (std::size_t i = 0; i < frames; ++i) {
            const float mod = 0.5f * (lfo_.next() + 1.0f);   // 0..1 unipolar
            const float read_pos = base_samples_ + depth * mod;
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto& line = lines_[ch];
                const float dry = input.channel(ch)[i];
                const float wet = line.read(read_pos);
                float fed = dry + wet * fb;
                if (std::fabs(fed) < 1e-30f) fed = 0.0f;   // flush feedback denormals
                line.push(fed);
                output.channel(ch)[i] = dry * (1.0f - mix) + wet * mix;
            }
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
    float base_samples_ = 48.0f;
    int max_delay_ = 292;
    std::array<signal::DelayLine, 8> lines_{};
    signal::Oscillator lfo_;
};

inline std::unique_ptr<format::Processor> create_flanger() {
    return std::make_unique<FlangerProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_flanger_editor (declared above) so the
// create_view() override links in every TU that uses the processor.
#include "flanger_editor.hpp"
