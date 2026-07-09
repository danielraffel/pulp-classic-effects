#pragma once

// Delay — a feedback delay line with wet/dry mix.
//
// Clean-room textbook delay: per-channel circular delay line, feedback path, and
// a dry/wet blend. Built on Pulp's own pulp::signal::DelayLine; no third-party
// effect source was read. See the repo README for the algorithmic reference.

#include <pulp/format/processor.hpp>
// Headless WASM DSP builds curate out core/view (canvas/Skia/text-shaping),
// so every editor reference below is gated on PULP_HEADLESS.
#if !PULP_HEADLESS
#include <pulp/view/view.hpp>
#endif
#include <pulp/signal/delay_line.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum DelayParams : state::ParamID {
    // The reference parameter set is seconds / 0..0.9 feedback / 0..1 wet mix.
    // The kTimeMs / kDelayMix symbol names predate the switch to the reference's
    // units (seconds and a 0..1 mix); the names are kept to avoid churning the
    // editor/test call sites, but the values they carry now match the reference.
    kTimeMs   = 1,   // delay time, seconds, 0..5
    kFeedback = 2,   // 0..0.9
    kDelayMix = 3,   // 0..1 wet
};

// Defined out-of-line in delay_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
std::unique_ptr<view::View> build_delay_editor(state::StateStore& store);
#endif

class DelayProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
    std::unique_ptr<view::View> create_view() override { return build_delay_editor(state()); }
#endif

    format::PluginDescriptor descriptor() const override {
        return {.name = "Delay", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.delay", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}},
                // Advisory feedback-tail length so hosts drain echoes on stop
                // (max delay time; the actual tail also depends on feedback).
                .tail_samples = 240000};
    }

    void define_parameters(state::StateStore& store) override {
        // Reference parameter set: time in seconds (display name "Delay Time",
        // short "Time"), feedback 0..0.9, mix 0..1 wet. All linear.
        store.add_parameter({.id = kTimeMs, .name = "Time", .unit = "s",
                             .range = state::ParamRange::linear(0.0f, 5.0f, 0.1f)});
        store.add_parameter({.id = kFeedback, .name = "Feedback", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 0.9f, 0.7f)});
        store.add_parameter({.id = kDelayMix, .name = "Mix", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 1.0f, 1.0f)});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        // 5 s of delay line to cover the full Time range, plus a little headroom.
        max_delay_ = static_cast<int>(sample_rate_ * 5.0f) + 4;
        for (auto& line : lines_) {
            line.prepare(max_delay_);
        }
        delay_init_ = false;  // snap the smoothed delay to the target on frame 0
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), lines_.size()});
        const std::size_t frames = output.num_samples();

        // Clear stale echo history on a transport seek/loop so a previous
        // location's tail doesn't leak into the new one, and re-snap the
        // smoothed delay to the target on the next frame.
        if (ctx.should_reset_dsp_state()) {
            for (auto& line : lines_) line.reset();
            delay_init_ = false;
        }

        // Mix and Feedback are also smoothed per sample (below) so turning
        // those knobs doesn't step the wet level / tail gain and click.
        const float mix_target = std::clamp(state().get_value(kDelayMix), 0.0f, 1.0f);
        const float fb_target = std::clamp(state().get_value(kFeedback), 0.0f, 0.9f);
        // Time parameter is in seconds. This is the *target* delay length: the
        // read position is smoothed toward it per sample (below) so turning the
        // Time knob glides the delay instead of stepping it once per block.
        // A per-block step teleports the fractional read position and clicks /
        // zippers the wet tail; a per-sample glide only bends its pitch.
        float delay_target = state().get_value(kTimeMs) * sample_rate_;
        delay_target = std::clamp(delay_target, 1.0f,
                                  static_cast<float>(max_delay_ - 1));
        // One-pole coefficient that reaches the target with a ~30 ms time
        // constant; at steady state smoothed_delay_ == delay_target, so the
        // default sound is unchanged.
        const float smooth = 1.0f - std::exp(-1.0f / (kSmoothSecs * sample_rate_));
        if (!delay_init_) {
            smoothed_delay_ = delay_target;
            smoothed_mix_ = mix_target;
            smoothed_fb_ = fb_target;
            delay_init_ = true;
        }

        for (std::size_t i = 0; i < frames; ++i) {
            // Advance the shared smoothed params once per frame, before the
            // per-channel taps, so every channel reads the same values.
            smoothed_delay_ += smooth * (delay_target - smoothed_delay_);
            smoothed_mix_ += smooth * (mix_target - smoothed_mix_);
            smoothed_fb_ += smooth * (fb_target - smoothed_fb_);
            const float d = smoothed_delay_;
            const float mix = smoothed_mix_;
            const float fb = smoothed_fb_;
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch);
                auto out = output.channel(ch);
                auto& line = lines_[ch];
                const float dry = in[i];
                const float wet = line.read(d);  // fractional (interpolated) read
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
    // Time constant for the per-sample delay-length glide (see process()).
    static constexpr float kSmoothSecs = 0.03f;
    float sample_rate_ = 48000.0f;
    int max_delay_ = 240004;
    float smoothed_delay_ = 0.0f;  // current (glided) delay length, in samples
    float smoothed_mix_ = 0.0f;    // glided wet/dry mix
    float smoothed_fb_ = 0.0f;     // glided feedback gain
    bool delay_init_ = false;      // false → snap to targets on the next frame
    std::array<signal::DelayLine, 8> lines_{};
};

inline std::unique_ptr<format::Processor> create_delay() {
    return std::make_unique<DelayProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_delay_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
#include "delay_editor.hpp"
#endif
