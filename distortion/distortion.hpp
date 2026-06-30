#pragma once

// Distortion — a selectable waveshaper followed by a one-pole "tone" tilt, with
// input- and output-gain trims around it.
//
// Clean-room textbook distortion (Reiss & McPherson, "Audio Effects"): the input
// is scaled by an Input Gain, run through one of four memoryless transfer
// functions (hard clip, soft clip, full-wave rectify, half-wave rectify), passed
// through a first-order shelving "tone" tilt, then scaled by an Output Gain. The
// four shapers, the gain conversions, and the shelf coefficients are all derived
// here from the standard formulas; no third-party effect source was copied. See
// the README.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum DistortionParams : state::ParamID {
    kDistType   = 1,  // 0=Hard Clip 1=Soft Clip 2=Exponential 3=Full Rect 4=Half Rect
    kDistInGain = 2,  // -24..+24 dB pre-shaper drive ("In")
    kDistOutGain= 3,  // -24..+24 dB post-shaper trim ("Out")
    kDistTone   = 4,  // -24..+24 dB high-shelf tilt
};

// Distortion-type indices, matching the Type combo's option order and truce's
// DistortionType enum (Reiss & McPherson / Juan Gil JUCE original).
enum class DistType : int {
    HardClip = 0,
    SoftClip = 1,
    Exponential = 2,
    FullRect = 3,
    HalfRect = 4,
};

// Defined out-of-line in distortion_editor.hpp (included at the bottom).
std::unique_ptr<view::View> build_distortion_editor(state::StateStore& store);

// Four memoryless transfer functions. Each shapes a single sample (already
// scaled by the input gain) and bounds/folds it differently:
//   Hard Clip — clamp to ±0.5 (a flat ceiling, abrupt corners).
//   Soft Clip — two-sided cubic soft knee: linear in [-1/3, 1/3], a parabolic
//               shoulder through 2/3, hard saturation past 2/3, renormalised to
//               ±0.5 (rounded corners, same ceiling as hard clip).
//   Exponential — sgn(x)·(1 − e^−|x|): smooth exponential saturation, bounded ±1.
//   Full Rect — |x| (full-wave rectifier, output never negative).
//   Half Rect — max(x, 0) (half-wave rectifier, negatives zeroed).
inline float dist_shape(float x, DistType type) {
    switch (type) {
        case DistType::HardClip: {
            constexpr float kT = 0.5f;
            return std::clamp(x, -kT, kT);
        }
        case DistType::SoftClip: {
            constexpr float t1 = 1.0f / 3.0f;
            constexpr float t2 = 2.0f / 3.0f;
            float raw;
            if (x > t2) {
                raw = 1.0f;
            } else if (x > t1) {
                const float inner = 2.0f - 3.0f * x;
                raw = 1.0f - (inner * inner) / 3.0f;
            } else if (x < -t2) {
                raw = -1.0f;
            } else if (x < -t1) {
                const float inner = 2.0f + 3.0f * x;
                raw = -1.0f + (inner * inner) / 3.0f;
            } else {
                raw = 2.0f * x;
            }
            return raw * 0.5f;
        }
        case DistType::Exponential: {
            const float ax = std::fabs(x);
            return (x < 0.0f ? -1.0f : 1.0f) * (1.0f - std::exp(-ax));
        }
        case DistType::FullRect:
            return std::fabs(x);
        case DistType::HalfRect:
        default:
            return std::max(x, 0.0f);
    }
}

// First-order high-shelf "tone" tilt. The discrete cut-off is pinned low (the
// textbook PI * 0.01) so the control reads as a broad bright/dark tilt that is
// flat at unity (0 dB) and only moves the shelf gain. Standard bilinear shelf
// design — one pole, one zero, per-channel state.
struct DistToneShelf {
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;

    void update(float tone_db) {
        constexpr float kDiscrete = 3.14159265358979f * 0.01f;
        const float gain_lin = std::pow(10.0f, tone_db * 0.05f);
        const float tan_half = std::tan(kDiscrete * 0.5f);
        const float sqrt_g = std::sqrt(gain_lin);
        const float a0 = sqrt_g * tan_half + 1.0f;
        const float inv = 1.0f / a0;
        b0 = (sqrt_g * tan_half + gain_lin) * inv;
        b1 = (sqrt_g * tan_half - gain_lin) * inv;
        a1 = (sqrt_g * tan_half - 1.0f) * inv;
    }
    float process(float x) {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() { x1 = 0.0f; y1 = 0.0f; }
};

class DistortionProcessor : public format::Processor {
public:
    std::unique_ptr<view::View> create_view() override { return build_distortion_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Distortion", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.distortion", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        // Order and ranges mirror the truce reference parameter list. The Type
        // combo has 5 options and defaults to index 3 (Full Rect, per truce);
        // In/Tone default to +12 dB and Out to -24 dB to tame the shaper output.
        store.add_parameter({.id = kDistType, .name = "Type", .unit = "",
                             .range = {0.0f, 4.0f, 3.0f, 1.0f}});
        store.add_parameter({.id = kDistInGain, .name = "Input Gain", .unit = "dB",
                             .range = state::ParamRange::linear(-24.0f, 24.0f, 12.0f)});
        store.add_parameter({.id = kDistOutGain, .name = "Output Gain", .unit = "dB",
                             .range = state::ParamRange::linear(-24.0f, 24.0f, -24.0f)});
        store.add_parameter({.id = kDistTone, .name = "Tone", .unit = "dB",
                             .range = state::ParamRange::linear(-24.0f, 24.0f, 12.0f)});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        for (auto& s : shelves_) s.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), shelves_.size()});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state())
            for (auto& s : shelves_) s.reset();

        const int type_index = std::clamp(
            static_cast<int>(std::lround(state().get_value(kDistType))), 0, 4);
        const auto type = static_cast<DistType>(type_index);
        const float in_lin  = std::pow(10.0f, state().get_value(kDistInGain)  * 0.05f);
        const float out_lin = std::pow(10.0f, state().get_value(kDistOutGain) * 0.05f);
        const float tone_db = state().get_value(kDistTone);

        for (auto& s : shelves_) s.update(tone_db);

        for (std::size_t ch = 0; ch < channels; ++ch) {
            auto in = input.channel(ch);
            auto out = output.channel(ch);
            DistToneShelf& shelf = shelves_[ch];
            for (std::size_t i = 0; i < frames; ++i) {
                const float shaped = dist_shape(in[i] * in_lin, type);
                out[i] = shelf.process(shaped) * out_lin;
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
    std::array<DistToneShelf, 8> shelves_{};  // per-channel tone-shelf state
};

inline std::unique_ptr<format::Processor> create_distortion() {
    return std::make_unique<DistortionProcessor>();
}

} // namespace pulp::examples::classic

#include "distortion_editor.hpp"
