#pragma once

// Ping-Pong Delay — a stereo delay whose echoes bounce between the channels.
//
// Clean-room textbook ping-pong: two delay lines are cross-coupled so the left
// line's feedback feeds the right line and vice versa. A signal entering one
// side reappears on the other after each delay period, panning the repeats
// left↔right as they decay. Balance biases which input channel feeds the taps,
// so the bounce can lead from either side. Built on Pulp's own
// pulp::signal::DelayLine; no third-party effect source was read. See the
// README for the reference.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/delay_line.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

// Reference parameter set (display name / short name / range / default):
//   Balance     "Bal"   0..1        0.25   — L/R input bias of the taps
//   Delay Time  "Time"  0..5 s      0.10   — one bounce length
//   Feedback    "Fbk"   0..0.9      0.70   — echo regeneration
//   Mix         "Mix"   0..1        1.00   — dry↔wet blend
enum PingPongParams : state::ParamID {
    kPingBalance  = 1,
    kPingTime     = 2,
    kPingFeedback = 3,
    kPingMix      = 4,
};

constexpr float kPingMaxDelaySecs = 5.0f;

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
                // Advisory feedback-tail length (max delay time; the real tail
                // also depends on feedback).
                .tail_samples = 240000};
    }

    void define_parameters(state::StateStore& store) override {
        // Mirrors the reference set: balance + time(s) + feedback + mix, all
        // linear. No bypass — a ping-pong is defined by its wet bounce.
        store.add_parameter({.id = kPingBalance, .name = "Balance", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 1.0f, 0.25f)});
        store.add_parameter({.id = kPingTime, .name = "Time", .unit = "s",
                             .range = state::ParamRange::linear(0.0f, kPingMaxDelaySecs, 0.1f)});
        store.add_parameter({.id = kPingFeedback, .name = "Feedback", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 0.9f, 0.7f)});
        store.add_parameter({.id = kPingMix, .name = "Mix", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 1.0f, 1.0f)});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        max_delay_ = static_cast<int>(sample_rate_ * kPingMaxDelaySecs) + 4;
        left_.prepare(max_delay_);
        right_.prepare(max_delay_);
        delay_init_ = false;  // snap the smoothed bounce length on frame 0
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t in_ch = input.num_channels();
        const std::size_t out_ch = output.num_channels();
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) {
            left_.reset(); right_.reset();
            delay_init_ = false;  // re-snap the smoothed bounce length
        }

        const float balance = std::clamp(state().get_value(kPingBalance), 0.0f, 1.0f);
        const float mix = std::clamp(state().get_value(kPingMix), 0.0f, 1.0f);
        const float fb = std::clamp(state().get_value(kPingFeedback), 0.0f, 0.9f);
        // Target bounce length; smoothed toward per sample so turning the Time
        // knob glides the tap position instead of stepping it once per block
        // (a per-block step jumps the fractional read and clicks the bounce).
        float d_target = state().get_value(kPingTime) * sample_rate_;
        d_target = std::clamp(d_target, 1.0f, static_cast<float>(max_delay_ - 1));
        const float smooth = 1.0f - std::exp(-1.0f / (kSmoothSecs * sample_rate_));
        if (!delay_init_) { smoothed_delay_ = d_target; delay_init_ = true; }

        // Stereo cross-coupled path needs both channels updated in lock-step, so
        // process per-sample across channels rather than channel-by-channel.
        if (out_ch >= 2 && in_ch >= 1) {
            for (std::size_t i = 0; i < frames; ++i) {
                smoothed_delay_ += smooth * (d_target - smoothed_delay_);
                const float d = smoothed_delay_;
                const float inL = input.channel(0)[i];
                const float inR = (in_ch >= 2) ? input.channel(1)[i] : inL;
                // Balance biases which input channel drives the taps:
                // 0 → only L feeds, 1 → only R feeds.
                const float inLb = (1.0f - balance) * inL;
                const float inRb = balance * inR;
                const float wetL = left_.read(d);
                const float wetR = right_.read(d);
                // Cross-couple: each line is fed the OTHER line's delayed output,
                // so energy alternates sides on every bounce.
                float fedL = inLb + fb * wetR;
                float fedR = inRb + fb * wetL;
                if (std::fabs(fedL) < 1e-30f) fedL = 0.0f;
                if (std::fabs(fedR) < 1e-30f) fedR = 0.0f;
                left_.push(fedL);
                right_.push(fedR);
                output.channel(0)[i] = inLb * (1.0f - mix) + wetL * mix;
                output.channel(1)[i] = inRb * (1.0f - mix) + wetR * mix;
            }
            clear_extra(output, 2);
        } else {
            // Mono fallback: behaves as a single feedback delay on the left line.
            for (std::size_t i = 0; i < frames; ++i) {
                smoothed_delay_ += smooth * (d_target - smoothed_delay_);
                const float d = smoothed_delay_;
                const float dry = (1.0f - balance) * input.channel(0)[i];
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
    // Time constant for the per-sample bounce-length glide (see process()).
    static constexpr float kSmoothSecs = 0.03f;
    float sample_rate_ = 48000.0f;
    int max_delay_ = 240004;
    float smoothed_delay_ = 0.0f;  // current (glided) bounce length, in samples
    bool delay_init_ = false;      // false → snap to target on the next frame
    signal::DelayLine left_, right_;
};

inline std::unique_ptr<format::Processor> create_ping_pong() {
    return std::make_unique<PingPongProcessor>();
}

} // namespace pulp::examples::classic

#include "ping_pong_editor.hpp"
