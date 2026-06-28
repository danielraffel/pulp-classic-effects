#pragma once

// Delay — a feedback delay line with wet/dry mix.
//
// Clean-room textbook delay: per-channel circular delay line, feedback path, and
// a dry/wet blend. Built on Pulp's own pulp::signal::DelayLine; no third-party
// effect source was read. See the repo README for the algorithmic reference.

#include <pulp/format/processor.hpp>
#include <pulp/signal/delay_line.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum DelayParams : state::ParamID {
    kTimeMs   = 1,   // 1..2000 ms
    kFeedback = 2,   // 0..0.95
    kDelayMix = 3,   // 0..100 %
    kDelayBypass = 4,
};

class DelayProcessor : public format::Processor {
public:
    format::PluginDescriptor descriptor() const override {
        return {.name = "Delay", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.delay", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}},
                // Advisory feedback-tail length so hosts drain echoes on stop
                // (max delay time; the actual tail also depends on feedback).
                .tail_samples = 96000};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kTimeMs, .name = "Time", .unit = "ms",
                             .range = state::ParamRange::with_centre(1.0f, 2000.0f, 250.0f, 250.0f)});
        store.add_parameter({.id = kFeedback, .name = "Feedback", .unit = "",
                             .range = {0.0f, 0.95f, 0.3f, 0.0f}});
        store.add_parameter({.id = kDelayMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 35.0f, 0.0f}});
        store.add_parameter({.id = kDelayBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        max_delay_ = static_cast<int>(sample_rate_ * 2.0f) + 4;  // 2 s headroom
        for (auto& line : lines_) {
            line.prepare(max_delay_);
        }
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), lines_.size()});
        const std::size_t frames = output.num_samples();

        // Clear stale echo history on a transport seek/loop so a previous
        // location's tail doesn't leak into the new one.
        if (ctx.should_reset_dsp_state())
            for (auto& line : lines_) line.reset();

        if (state().get_value(kDelayBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        const float mix = std::clamp(state().get_value(kDelayMix) / 100.0f, 0.0f, 1.0f);
        const float fb = std::clamp(state().get_value(kFeedback), 0.0f, 0.95f);
        float delay_samples = state().get_value(kTimeMs) / 1000.0f * sample_rate_;
        delay_samples = std::clamp(delay_samples, 1.0f,
                                   static_cast<float>(max_delay_ - 1));

        for (std::size_t ch = 0; ch < channels; ++ch) {
            auto in = input.channel(ch);
            auto out = output.channel(ch);
            auto& line = lines_[ch];
            for (std::size_t i = 0; i < frames; ++i) {
                const float dry = in[i];
                const float wet = line.read(delay_samples);
                // Flush denormals: a decaying feedback tail drifts into
                // subnormal range and stalls the audio thread on real hardware.
                float fed = dry + wet * fb;
                if (std::fabs(fed) < 1e-30f) fed = 0.0f;
                line.push(fed);  // feedback into the line
                out[i] = dry * (1.0f - mix) + wet * mix;
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
    int max_delay_ = 96004;
    std::array<signal::DelayLine, 8> lines_{};
};

inline std::unique_ptr<format::Processor> create_delay() {
    return std::make_unique<DelayProcessor>();
}

} // namespace pulp::examples::classic
