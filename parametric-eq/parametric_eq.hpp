#pragma once

// Parametric EQ — a three-band equalizer (low shelf, mid bell, high shelf).
//
// Clean-room textbook EQ: each band is a biquad (RBJ cookbook coefficients),
// applied in series per channel. The low and high shelves tilt the spectrum at
// the band edges; the mid is a peaking (bell) filter with adjustable Q. Built
// on Pulp's own pulp::signal::Biquad; no third-party effect source was read.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/biquad.hpp>

#include <algorithm>
#include <array>
#include <memory>

namespace pulp::examples::classic {

enum ParametricEqParams : state::ParamID {
    kLowFreq   = 1,  // 50..500 Hz
    kLowGain   = 2,  // -18..18 dB
    kMidFreq   = 3,  // 200..5000 Hz
    kMidGain   = 4,  // -18..18 dB
    kMidQ      = 5,  // 0.2..10
    kHighFreq  = 6,  // 2000..16000 Hz
    kHighGain  = 7,  // -18..18 dB
    kEqBypass  = 8,
};

// Defined out-of-line in parametric_eq_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
std::unique_ptr<view::View> build_parametric_eq_editor(state::StateStore& store);

class ParametricEqProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
    std::unique_ptr<view::View> create_view() override { return build_parametric_eq_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Parametric EQ", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.parametric-eq", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kLowFreq, .name = "Low Freq", .unit = "Hz",
                             .range = state::ParamRange::with_centre(50.0f, 500.0f, 150.0f, 200.0f)});
        store.add_parameter({.id = kLowGain, .name = "Low Gain", .unit = "dB",
                             .range = {-18.0f, 18.0f, 0.0f, 0.0f}});
        store.add_parameter({.id = kMidFreq, .name = "Mid Freq", .unit = "Hz",
                             .range = state::ParamRange::with_centre(200.0f, 5000.0f, 1000.0f, 1000.0f)});
        store.add_parameter({.id = kMidGain, .name = "Mid Gain", .unit = "dB",
                             .range = {-18.0f, 18.0f, 0.0f, 0.0f}});
        store.add_parameter({.id = kMidQ, .name = "Mid Q", .unit = "",
                             .range = state::ParamRange::with_centre(0.2f, 10.0f, 1.0f, 1.0f)});
        store.add_parameter({.id = kHighFreq, .name = "High Freq", .unit = "Hz",
                             .range = state::ParamRange::with_centre(2000.0f, 16000.0f, 5000.0f, 5000.0f)});
        store.add_parameter({.id = kHighGain, .name = "High Gain", .unit = "dB",
                             .range = {-18.0f, 18.0f, 0.0f, 0.0f}});
        store.add_parameter({.id = kEqBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        for (auto& ch : bands_) for (auto& b : ch) b.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), bands_.size()});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state())
            for (auto& ch : bands_) for (auto& b : ch) b.reset();

        if (state().get_value(kEqBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        const float nyq = sample_rate_ * 0.49f;
        const float low_f  = std::clamp(state().get_value(kLowFreq), 20.0f, nyq);
        const float mid_f  = std::clamp(state().get_value(kMidFreq), 20.0f, nyq);
        const float high_f = std::clamp(state().get_value(kHighFreq), 20.0f, nyq);
        // Read plain values (this example ignores CLAP modulation). Gains are
        // already range-clamped on write, but clamp defensively in case a future
        // path writes the store unclamped.
        const float low_g  = std::clamp(state().get_value(kLowGain), -24.0f, 24.0f);
        const float mid_g  = std::clamp(state().get_value(kMidGain), -24.0f, 24.0f);
        const float high_g = std::clamp(state().get_value(kHighGain), -24.0f, 24.0f);
        const float mid_q  = std::clamp(state().get_value(kMidQ), 0.1f, 20.0f);
        using T = signal::Biquad::Type;

        // Recompute coefficients per block (RT-safe, no allocation). Coefficient
        // assignment preserves each filter's running state, so no zipper reset.
        for (std::size_t ch = 0; ch < channels; ++ch) {
            bands_[ch][0].set_coefficients(T::low_shelf, low_f, 0.707f, sample_rate_, low_g);
            bands_[ch][1].set_coefficients(T::peaking,   mid_f, mid_q,  sample_rate_, mid_g);
            bands_[ch][2].set_coefficients(T::high_shelf, high_f, 0.707f, sample_rate_, high_g);
        }
        for (std::size_t ch = 0; ch < channels; ++ch) {
            auto in = input.channel(ch);
            auto out = output.channel(ch);
            auto& band = bands_[ch];
            for (std::size_t i = 0; i < frames; ++i) {
                float x = in[i];
                x = band[0].process(x);
                x = band[1].process(x);
                x = band[2].process(x);
                out[i] = x;
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
    std::array<std::array<signal::Biquad, 3>, 8> bands_{};
};

inline std::unique_ptr<format::Processor> create_parametric_eq() {
    return std::make_unique<ParametricEqProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_parametric_eq_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
#include "parametric_eq_editor.hpp"
