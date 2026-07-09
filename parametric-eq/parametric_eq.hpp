#pragma once

// Parametric EQ — a single-band, selectable-type biquad equalizer.
//
// Clean-room textbook EQ modeled on Reiss & McPherson's *Audio Effects*
// "Parametric EQ" (and the JUCE reference of the same name): ONE band whose
// response is chosen from a Type dropdown — low-pass, high-pass, low-shelf,
// high-shelf, band-pass, band-stop, or peaking. Frequency, Q, and Gain shape
// the selected filter. Each channel runs its own second-order section using
// Pulp's pulp::signal::Biquad (RBJ audio-EQ-cookbook coefficients); no
// third-party effect source was copied.

#include <pulp/format/processor.hpp>
// Headless WASM DSP builds curate out core/view (canvas/Skia/text-shaping),
// so every editor reference below is gated on PULP_HEADLESS.
#if !PULP_HEADLESS
#include <pulp/view/view.hpp>
#endif
#include <pulp/signal/biquad.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum ParametricEqParams : state::ParamID {
    kEqFreq = 1,  // 10..20000 Hz (log), default 1500
    kQ    = 2,  // 0.1..20, default sqrt(2)
    kGain = 3,  // -12..12 dB, default 0 (used by shelf/peaking types)
    kType = 4,  // 0..6 filter type (see kFilterTypeLabels), default 6 = Peaking
};

// Filter-type dropdown options, in order. Index 6 (Peaking) is the default —
// matching the reference, where a parametric EQ defaults to a peaking bell.
// The seventh entry is "Peaking" (the reference JUCE plug-in labels it
// "Peaking/Notch"); the first six are the named shelving/pass/stop responses.
inline constexpr std::array<const char*, 7> kFilterTypeLabels = {
    "Low-Pass", "High-Pass", "Low-Shelf", "High-Shelf",
    "Band-Pass", "Band-Stop", "Peaking",
};
inline constexpr int kFilterTypeCount = static_cast<int>(kFilterTypeLabels.size());
inline constexpr int kFilterTypeDefault = 6;  // Peaking

// Defined out-of-line in parametric_eq_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
std::unique_ptr<view::View> build_parametric_eq_editor(state::StateStore& store);
#endif

class ParametricEqProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
    std::unique_ptr<view::View> create_view() override { return build_parametric_eq_editor(state()); }
#endif

    format::PluginDescriptor descriptor() const override {
        return {.name = "Parametric EQ", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.parametric-eq", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        // Frequency: log-skewed 10 Hz .. 20 kHz, geometric centre, default 1.5 kHz.
        store.add_parameter({.id = kEqFreq, .name = "Freq", .unit = "Hz",
                             .range = state::ParamRange::with_centre(
                                 10.0f, 20000.0f, std::sqrt(10.0f * 20000.0f), 1500.0f)});
        // Q: linear 0.1 .. 20, default sqrt(2) (Butterworth-ish width).
        store.add_parameter({.id = kQ, .name = "Q", .unit = "",
                             .range = {0.1f, 20.0f, 1.41421356f, 0.0f}});
        // Gain: linear -12 .. +12 dB, default 0 (shelf/peaking only).
        store.add_parameter({.id = kGain, .name = "Gain", .unit = "dB",
                             .range = {-12.0f, 12.0f, 0.0f, 0.0f}});
        // Type: discrete 0..6 selector, default 6 (Peaking).
        store.add_parameter({.id = kType, .name = "Type", .unit = "",
                             .range = {0.0f, static_cast<float>(kFilterTypeCount - 1),
                                       static_cast<float>(kFilterTypeDefault), 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        for (auto& b : filters_) b.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), filters_.size()});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state())
            for (auto& b : filters_) b.reset();

        const float nyq = sample_rate_ * 0.49f;
        const float freq = std::clamp(state().get_value(kEqFreq), 10.0f, nyq);
        const float q    = std::clamp(state().get_value(kQ), 0.1f, 20.0f);
        const float gain = std::clamp(state().get_value(kGain), -24.0f, 24.0f);
        const int   type = std::clamp(static_cast<int>(std::lround(state().get_value(kType))),
                                      0, kFilterTypeCount - 1);
        const auto bt = biquad_type(type);

        // Recompute coefficients per block (RT-safe, no allocation). Coefficient
        // assignment preserves each filter's running state, so no zipper reset.
        for (std::size_t ch = 0; ch < channels; ++ch)
            filters_[ch].set_coefficients(bt, freq, q, sample_rate_, gain);

        for (std::size_t ch = 0; ch < channels; ++ch) {
            auto in = input.channel(ch);
            auto out = output.channel(ch);
            auto& f = filters_[ch];
            for (std::size_t i = 0; i < frames; ++i) out[i] = f.process(in[i]);
        }
        clear_extra(output, channels);
    }

private:
    // Map the Type dropdown index to a Biquad response. Indices follow
    // kFilterTypeLabels. Band-Stop maps to the RBJ notch; Peaking to the RBJ
    // peaking (bell). Only the shelf and peaking types use the Gain parameter.
    static signal::Biquad::Type biquad_type(int index) {
        using T = signal::Biquad::Type;
        switch (index) {
            case 0: return T::lowpass;
            case 1: return T::highpass;
            case 2: return T::low_shelf;
            case 3: return T::high_shelf;
            case 4: return T::bandpass;
            case 5: return T::notch;       // Band-Stop
            case 6:
            default: return T::peaking;
        }
    }

    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }
    float sample_rate_ = 48000.0f;
    std::array<signal::Biquad, 8> filters_{};
};

inline std::unique_ptr<format::Processor> create_parametric_eq() {
    return std::make_unique<ParametricEqProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_parametric_eq_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
#include "parametric_eq_editor.hpp"
#endif
