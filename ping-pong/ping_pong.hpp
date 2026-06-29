#pragma once

// Ping-Pong Delay — a stereo delay whose echoes bounce between the channels.
//
// Clean-room textbook ping-pong: two delay lines are cross-coupled so the left
// line's feedback feeds the right line and vice versa. A signal entering one
// side reappears on the other after each delay period, panning the repeats
// left↔right as they decay. Built on Pulp's own pulp::signal::DelayLine; no
// third-party effect source was read. See the README for the reference.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/delay_line.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum PingPongParams : state::ParamID {
    kPingTime     = 1,   // 1..2000 ms (one bounce)
    kPingFeedback = 2,   // 0..0.9
    kPingMix      = 3,   // 0..100 %
    kPingBypass   = 4,
};

// Defined out-of-line in ping_pong_editor.hpp (included at the bottom).
std::unique_ptr<view::View> build_ping_pong_editor(state::StateStore& store);

class PingPongProcessor : public format::Processor {
public:
    std::unique_ptr<view::View> create_view() override { return build_ping_pong_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Ping-Pong Delay", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.pingpong", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}},
                .tail_samples = 96000};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kPingTime, .name = "Time", .unit = "ms",
                             .range = state::ParamRange::with_centre(1.0f, 2000.0f, 350.0f, 350.0f)});
        store.add_parameter({.id = kPingFeedback, .name = "Feedback", .unit = "",
                             .range = {0.0f, 0.9f, 0.4f, 0.0f}});
        store.add_parameter({.id = kPingMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 35.0f, 0.0f}});
        store.add_parameter({.id = kPingBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        max_delay_ = static_cast<int>(sample_rate_ * 2.0f) + 4;
        left_.prepare(max_delay_);
        right_.prepare(max_delay_);
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t in_ch = input.num_channels();
        const std::size_t out_ch = output.num_channels();
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) { left_.reset(); right_.reset(); }

        if (state().get_value(kPingBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < std::min(in_ch, out_ch); ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, std::min(in_ch, out_ch));
            return;
        }

        const float mix = std::clamp(state().get_value(kPingMix) / 100.0f, 0.0f, 1.0f);
        const float fb = std::clamp(state().get_value(kPingFeedback), 0.0f, 0.9f);
        float d = state().get_value(kPingTime) / 1000.0f * sample_rate_;
        d = std::clamp(d, 1.0f, static_cast<float>(max_delay_ - 1));

        // Stereo cross-coupled path needs both channels updated in lock-step, so
        // process per-sample across channels rather than channel-by-channel.
        if (out_ch >= 2 && in_ch >= 1) {
            for (std::size_t i = 0; i < frames; ++i) {
                const float inL = input.channel(0)[i];
                const float inR = (in_ch >= 2) ? input.channel(1)[i] : inL;
                const float wetL = left_.read(d);
                const float wetR = right_.read(d);
                // Cross-couple: each line is fed the OTHER line's delayed output,
                // so energy alternates sides on every bounce.
                float fedL = inL + fb * wetR;
                float fedR = inR + fb * wetL;
                if (std::fabs(fedL) < 1e-30f) fedL = 0.0f;
                if (std::fabs(fedR) < 1e-30f) fedR = 0.0f;
                left_.push(fedL);
                right_.push(fedR);
                output.channel(0)[i] = inL * (1.0f - mix) + wetL * mix;
                output.channel(1)[i] = inR * (1.0f - mix) + wetR * mix;
            }
            clear_extra(output, 2);
        } else {
            // Mono fallback: behaves as a single feedback delay on the left line.
            for (std::size_t i = 0; i < frames; ++i) {
                const float dry = input.channel(0)[i];
                const float wet = left_.read(d);
                float fed = dry + fb * wet;
                if (std::fabs(fed) < 1e-30f) fed = 0.0f;
                left_.push(fed);
                output.channel(0)[i] = dry * (1.0f - mix) + wet * mix;
            }
            clear_extra(output, 1);
        }
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
    signal::DelayLine left_, right_;
};

inline std::unique_ptr<format::Processor> create_ping_pong() {
    return std::make_unique<PingPongProcessor>();
}

} // namespace pulp::examples::classic

#include "ping_pong_editor.hpp"
