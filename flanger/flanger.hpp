#pragma once

// Flanger — a short, LFO-swept fractional delay summed with the dry signal, with
// a feedback path that sharpens the swept comb-filter notches into the
// characteristic "jet plane" whoosh.
//
// Clean-room textbook flanger built on the Pulp SDK. A low-frequency oscillator
// sweeps the read position of a very short fractional delay line (a few ms). The
// delayed copy is added to the dry input (output = dry + delayed·depth·polarity),
// and is fed back into the line (line = dry + delayed·feedback) to make the comb
// resonant. The control surface follows Reiss & McPherson's *Audio Effects*
// flanger discussion and the JUCE "Flanger" reference layout:
//
//   Delay (s)   base read offset — sets the centre comb frequency
//   Width (s)   sweep amount added on top of Delay by the LFO
//   Depth       wet add amount (0 = dry only, 1 = full comb)
//   Feedback    recirculation gain into the delay line (resonance)
//   Inverted    flips the polarity of the wet add (notches <-> peaks)
//   LFO Rate    sweep frequency in Hz
//   Waveform    LFO shape: Sine / Triangle / Sawtooth / Inv. Sawtooth
//   Interp      fractional-read interpolation: Nearest / Linear / Cubic
//   Stereo      quarter-cycle LFO phase offset on the right channel
//
// The LFO and the interpolating circular buffer are implemented here by hand
// (the SDK DelayLine only offers linear interpolation, and we need the four LFO
// shapes plus nearest/linear/cubic reads). No third-party effect source was
// copied; see the README for the algorithmic reference.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace pulp::examples::classic {

enum FlangerParams : state::ParamID {
    kFlangerDelay    = 1,  // 0.001..0.02 s   base delay
    kFlangerWidth    = 2,  // 0.001..0.02 s   sweep width
    kFlangerDepth    = 3,  // 0..1            wet add amount
    kFlangerFeedback = 4,  // 0..0.5          recirculation gain
    kFlangerInverted = 5,  // toggle          wet polarity
    kFlangerRate     = 6,  // 0.05..2 Hz      LFO frequency
    kFlangerWaveform = 7,  // 0..3            Sine/Triangle/Saw/Inv.Saw
    kFlangerInterp   = 8,  // 0..2            Nearest/Linear/Cubic
    kFlangerStereo   = 9,  // toggle          per-channel LFO phase offset
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
        store.add_parameter({.id = kFlangerDelay, .name = "Delay", .unit = "s",
                             .range = state::ParamRange::linear(0.001f, 0.02f, 0.0025f)});
        store.add_parameter({.id = kFlangerWidth, .name = "Width", .unit = "s",
                             .range = state::ParamRange::linear(0.001f, 0.02f, 0.01f)});
        store.add_parameter({.id = kFlangerDepth, .name = "Depth", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 1.0f, 1.0f)});
        store.add_parameter({.id = kFlangerFeedback, .name = "Feedback", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 0.5f, 0.0f)});
        store.add_parameter({.id = kFlangerInverted, .name = "Inverted", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
        store.add_parameter({.id = kFlangerRate, .name = "LFO Rate", .unit = "Hz",
                             .range = state::ParamRange::linear(0.05f, 2.0f, 0.2f)});
        store.add_parameter({.id = kFlangerWaveform, .name = "Waveform", .unit = "",
                             .range = {0.0f, 3.0f, 0.0f, 1.0f}});  // Sine/Tri/Saw/Inv.Saw
        store.add_parameter({.id = kFlangerInterp, .name = "Interp", .unit = "",
                             .range = {0.0f, 2.0f, 1.0f, 1.0f}});  // Nearest/Linear/Cubic
        store.add_parameter({.id = kFlangerStereo, .name = "Stereo", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        // Delay (max 0.02 s) + Width (max 0.02 s) → 0.04 s of read range; +4 for
        // cubic's r0-1..r0+2 neighbourhood and the fractional ceiling.
        buf_len_ = static_cast<int>(kMaxDelaySecs * sample_rate_) + 4;
        if (buf_len_ < 8) buf_len_ = 8;
        for (auto& line : lines_) {
            line.assign(static_cast<std::size_t>(buf_len_), 0.0f);
        }
        write_pos_ = 0;
        phase_ = 0.0f;
        delay_init_ = false;  // snap smoothed base delay / width on frame 0
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), lines_.size()});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) {
            for (auto& line : lines_) std::fill(line.begin(), line.end(), 0.0f);
            write_pos_ = 0;
            phase_ = 0.0f;
            delay_init_ = false;  // re-snap the smoothed base delay / width
        }

        // Base Delay and Width are *targets*: they are smoothed toward per
        // sample (below) so turning either knob glides the comb centre / sweep
        // amount instead of stepping the base read position once per block. The
        // LFO still rides on top of the smoothed base, so the swept sound at
        // steady state is unchanged.
        const float delay_s = state().get_value(kFlangerDelay);
        const float width_s = state().get_value(kFlangerWidth);
        const float depth   = std::clamp(state().get_value(kFlangerDepth), 0.0f, 1.0f);
        const float fb      = std::clamp(state().get_value(kFlangerFeedback), 0.0f, 0.5f);
        const float invert  = state().get_value(kFlangerInverted) >= 0.5f ? -1.0f : 1.0f;
        const float rate    = state().get_value(kFlangerRate);
        const int   wave    = static_cast<int>(std::lround(state().get_value(kFlangerWaveform)));
        const int   interp  = static_cast<int>(std::lround(state().get_value(kFlangerInterp)));
        const bool  stereo  = state().get_value(kFlangerStereo) >= 0.5f;

        const float buf_len_f = static_cast<float>(buf_len_);
        // Keep the read fully inside the line: leave a 3-sample margin for the
        // cubic neighbourhood and never let the read collide with the write head.
        const float max_d = buf_len_f - 3.0f;
        const float phase_inc = rate / sample_rate_;
        // One-pole coefficient reaching the target with a ~30 ms time constant.
        const float smooth = 1.0f - std::exp(-1.0f / (kSmoothSecs * sample_rate_));
        if (!delay_init_) {
            smoothed_delay_s_ = delay_s;
            smoothed_width_s_ = width_s;
            delay_init_ = true;
        }

        for (std::size_t i = 0; i < frames; ++i) {
            // Glide the base Delay / Width once per frame, shared across channels.
            smoothed_delay_s_ += smooth * (delay_s - smoothed_delay_s_);
            smoothed_width_s_ += smooth * (width_s - smoothed_width_s_);
            for (std::size_t ch = 0; ch < channels; ++ch) {
                const float ph = (stereo && ch != 0) ? wrap01(phase_ + 0.25f) : phase_;
                float d_samp = (smoothed_delay_s_ + smoothed_width_s_ * lfo(ph, wave)) * sample_rate_;
                d_samp = std::clamp(d_samp, 1.0f, max_d);

                float read_pos = static_cast<float>(write_pos_) - d_samp;
                read_pos -= buf_len_f * std::floor(read_pos / buf_len_f);  // → [0, buf_len)

                const float delayed = interp_read(lines_[ch], read_pos, interp);
                const float dry = input.channel(ch)[i];
                output.channel(ch)[i] = dry + delayed * depth * invert;

                float fed = dry + delayed * fb;
                if (std::fabs(fed) < 1e-30f) fed = 0.0f;  // flush feedback denormals
                lines_[ch][static_cast<std::size_t>(write_pos_)] = fed;
            }
            if (++write_pos_ >= buf_len_) write_pos_ = 0;
            phase_ += phase_inc;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
        }

        for (std::size_t ch = channels; ch < output.num_channels(); ++ch) {
            auto o = output.channel(ch);
            for (std::size_t i = 0; i < frames; ++i) o[i] = 0.0f;
        }
    }

private:
    static constexpr float kMaxDelaySecs = 0.04f;
    static constexpr float kTau = 6.283185307179586f;
    // Time constant for the per-sample base Delay / Width glide (see process()).
    static constexpr float kSmoothSecs = 0.03f;

    // Unipolar LFO in [0, 1]; phase is in [0, 1). Shapes follow the standard
    // textbook flanger sweep waveforms.
    static float lfo(float phase, int waveform) {
        switch (waveform) {
            case 1:  // Triangle
                if (phase < 0.25f) return 0.5f + 2.0f * phase;
                if (phase < 0.75f) return 1.0f - 2.0f * (phase - 0.25f);
                return 2.0f * (phase - 0.75f);
            case 2:  // Sawtooth (rising, wrapped to start mid-scale)
                return phase < 0.5f ? 0.5f + phase : phase - 0.5f;
            case 3:  // Inverse sawtooth (falling)
                return phase < 0.5f ? 0.5f - phase : 1.5f - phase;
            case 0:  // Sine
            default:
                return 0.5f + 0.5f * std::sin(kTau * phase);
        }
    }

    // Read the circular line at a fractional position using the selected
    // interpolation. read_pos is already wrapped into [0, buf_len).
    float interp_read(const std::vector<float>& line, float read_pos, int interp) const {
        const int len = buf_len_;
        int r0 = static_cast<int>(std::floor(read_pos)) % len;
        if (r0 < 0) r0 += len;
        const float frac = read_pos - std::floor(read_pos);

        switch (interp) {
            case 0: {  // Nearest (floor)
                return line[static_cast<std::size_t>(r0)];
            }
            case 2: {  // Cubic (Catmull-Rom)
                const float sm1 = line[static_cast<std::size_t>((r0 - 1 + len) % len)];
                const float s0  = line[static_cast<std::size_t>(r0)];
                const float s1  = line[static_cast<std::size_t>((r0 + 1) % len)];
                const float s2  = line[static_cast<std::size_t>((r0 + 2) % len)];
                const float a0 = -0.5f * sm1 + 1.5f * s0 - 1.5f * s1 + 0.5f * s2;
                const float a1 = sm1 - 2.5f * s0 + 2.0f * s1 - 0.5f * s2;
                const float a2 = -0.5f * sm1 + 0.5f * s1;
                return ((a0 * frac + a1) * frac + a2) * frac + s0;
            }
            case 1:  // Linear
            default: {
                const float s0 = line[static_cast<std::size_t>(r0)];
                const float s1 = line[static_cast<std::size_t>((r0 + 1) % len)];
                return s0 + frac * (s1 - s0);
            }
        }
    }

    static float wrap01(float x) {
        x -= std::floor(x);
        return x;
    }

    float sample_rate_ = 48000.0f;
    int buf_len_ = 8;
    int write_pos_ = 0;
    float phase_ = 0.0f;
    float smoothed_delay_s_ = 0.0f;  // glided base delay, in seconds
    float smoothed_width_s_ = 0.0f;  // glided sweep width, in seconds
    bool delay_init_ = false;        // false → snap to targets on the next frame
    std::array<std::vector<float>, 8> lines_{};
};

inline std::unique_ptr<format::Processor> create_flanger() {
    return std::make_unique<FlangerProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_flanger_editor (declared above) so the
// create_view() override links in every TU that uses the processor.
#include "flanger_editor.hpp"
