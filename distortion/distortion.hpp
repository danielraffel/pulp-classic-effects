#pragma once

// Distortion — drive into a smooth saturator, a tone tilt, and a dry/wet blend.
//
// Clean-room textbook waveshaping distortion: the input is scaled by a drive
// gain and passed through a tanh soft-clipper (odd-symmetric, so it adds the
// warm odd-harmonic series without the hard edges of a clip), then a one-pole
// low-pass "tone" control tames the fizz, and a level trim + dry/wet blend set
// the balance. Built on std::tanh and a one-pole filter on Pulp's Processor
// interface; no third-party effect source was read. See the README.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum DistortionParams : state::ParamID {
    kDistDrive  = 1,  // 1..50 (linear pre-gain)
    kDistTone   = 2,  // 0..100 % (dark..bright low-pass)
    kDistLevel  = 3,  // 0..100 % (output trim)
    kDistMix    = 4,  // 0..100 %
    kDistBypass = 5,
};

// Defined out-of-line in distortion_editor.hpp (included at the bottom).
std::unique_ptr<view::View> build_distortion_editor(state::StateStore& store);

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
        store.add_parameter({.id = kDistDrive, .name = "Drive", .unit = "",
                             .range = state::ParamRange::with_centre(1.0f, 50.0f, 8.0f, 8.0f)});
        store.add_parameter({.id = kDistTone, .name = "Tone", .unit = "%",
                             .range = {0.0f, 100.0f, 70.0f, 0.0f}});
        store.add_parameter({.id = kDistLevel, .name = "Level", .unit = "%",
                             .range = {0.0f, 100.0f, 80.0f, 0.0f}});
        store.add_parameter({.id = kDistMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 100.0f, 0.0f}});
        store.add_parameter({.id = kDistBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        lp_.fill(0.0f);
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), lp_.size()});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) lp_.fill(0.0f);

        if (state().get_value(kDistBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        const float drive = std::clamp(state().get_value(kDistDrive), 1.0f, 50.0f);
        const float level = std::clamp(state().get_value(kDistLevel) / 100.0f, 0.0f, 1.0f);
        const float mix   = std::clamp(state().get_value(kDistMix) / 100.0f, 0.0f, 1.0f);
        const float tone  = std::clamp(state().get_value(kDistTone) / 100.0f, 0.0f, 1.0f);

        // Tone maps to a one-pole low-pass cutoff, ~500 Hz (dark) .. ~12 kHz
        // (bright), geometric so the control feels even. alpha is the pole.
        const float fc = 500.0f * std::pow(24.0f, tone);
        const float alpha = std::clamp(std::exp(-2.0f * 3.14159265358979f * fc / sample_rate_),
                                       0.0f, 0.9999f);
        // No makeup gain is needed: tanh saturates near ±1, so the shaped signal
        // is already bounded before the level/mix stage.

        for (std::size_t ch = 0; ch < channels; ++ch) {
            auto in = input.channel(ch);
            auto out = output.channel(ch);
            float& z = lp_[ch];
            for (std::size_t i = 0; i < frames; ++i) {
                const float dry = in[i];
                const float shaped = std::tanh(drive * dry);
                z = (1.0f - alpha) * shaped + alpha * z;   // one-pole low-pass tone
                const float wet = z * level;
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
    std::array<float, 8> lp_{};   // per-channel one-pole tone state
};

inline std::unique_ptr<format::Processor> create_distortion() {
    return std::make_unique<DistortionProcessor>();
}

} // namespace pulp::examples::classic

#include "distortion_editor.hpp"
