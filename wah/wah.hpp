#pragma once

// Wah — a resonant bandpass swept in frequency.
//
// Clean-room textbook wah: a state-variable bandpass with adjustable resonance
// whose centre frequency is either set directly (manual / pedal mode) or driven
// by the input envelope (envelope / auto-wah mode). Sweeping a resonant peak
// across the spectrum produces the classic vowel-like "wah". Built on Pulp's own
// pulp::signal::Svf + BallisticsFilter; no third-party effect source was read.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/svf.hpp>
#include <pulp/signal/ballistics_filter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum WahParams : state::ParamID {
    kWahMode        = 1,  // 0 = manual, 1 = envelope (auto-wah)
    kWahFreq        = 2,  // 200..3000 Hz (manual centre / envelope base)
    kWahResonance   = 3,  // 1..20
    kWahSensitivity = 4,  // 0..4000 Hz envelope sweep span
    kWahMix         = 5,  // 0..100 %
    kWahBypass      = 6,
};

// Defined out-of-line in wah_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
std::unique_ptr<view::View> build_wah_editor(state::StateStore& store);

class WahProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
    std::unique_ptr<view::View> create_view() override { return build_wah_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Wah", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.wah", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kWahMode, .name = "Mode", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});   // stepped: 0=manual, 1=envelope
        store.add_parameter({.id = kWahFreq, .name = "Freq", .unit = "Hz",
                             .range = state::ParamRange::with_centre(200.0f, 3000.0f, 600.0f, 600.0f)});
        store.add_parameter({.id = kWahResonance, .name = "Resonance", .unit = "",
                             .range = state::ParamRange::with_centre(1.0f, 20.0f, 6.0f, 6.0f)});
        store.add_parameter({.id = kWahSensitivity, .name = "Sensitivity", .unit = "Hz",
                             .range = {0.0f, 4000.0f, 3000.0f, 0.0f}});
        store.add_parameter({.id = kWahMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 100.0f, 0.0f}});
        store.add_parameter({.id = kWahBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        for (auto& f : filters_) {
            f.set_sample_rate(sample_rate_);
            f.set_mode(signal::Svf::Mode::bandpass);
            f.reset();
        }
        detector_.prepare(sample_rate_);
        detector_.set_mode(signal::BallisticsFilter::Mode::peak);
        detector_.set_attack_ms(5.0f);
        detector_.set_release_ms(80.0f);
        detector_.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), filters_.size()});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) {
            for (auto& f : filters_) f.reset();
            detector_.reset();
        }

        if (state().get_value(kWahBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        const bool envelope_mode = state().get_value(kWahMode) >= 0.5f;
        const float base = state().get_value(kWahFreq);
        const float q = std::max(0.5f, state().get_value(kWahResonance));
        const float sens = std::max(0.0f, state().get_value(kWahSensitivity));
        const float mix = std::clamp(state().get_value(kWahMix) / 100.0f, 0.0f, 1.0f);
        const float fmax = sample_rate_ * 0.45f;
        for (std::size_t ch = 0; ch < channels; ++ch) filters_[ch].set_resonance(q);

        if (!envelope_mode) {
            const float freq = std::clamp(base, 100.0f, fmax);
            for (std::size_t ch = 0; ch < channels; ++ch) filters_[ch].set_frequency(freq);
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i)
                    out[i] = in[i] * (1.0f - mix) + filters_[ch].process(in[i]) * mix;
            }
        } else {
            for (std::size_t i = 0; i < frames; ++i) {
                float linked = 0.0f;
                for (std::size_t ch = 0; ch < channels; ++ch)
                    linked = std::max(linked, std::fabs(input.channel(ch)[i]));
                const float env = detector_.process(linked);    // ~0..1
                const float freq = std::clamp(base + env * sens, 100.0f, fmax);
                // The centre is identical across channels; set_frequency recomputes
                // per channel for clarity (an SVF coeff-copy path would avoid the
                // duplicate tan() if this were a hot production effect).
                for (std::size_t ch = 0; ch < channels; ++ch) {
                    filters_[ch].set_frequency(freq);
                    const float dry = input.channel(ch)[i];
                    output.channel(ch)[i] = dry * (1.0f - mix) + filters_[ch].process(dry) * mix;
                }
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
    std::array<signal::Svf, 8> filters_{};
    signal::BallisticsFilter detector_;
};

inline std::unique_ptr<format::Processor> create_wah() {
    return std::make_unique<WahProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_wah_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
#include "wah_editor.hpp"
