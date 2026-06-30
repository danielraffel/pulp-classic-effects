#pragma once

// Wah-Wah — a resonant filter whose centre frequency is moved by a manual
// position, a low-frequency oscillator, an input envelope follower, or a blend
// of the two automatic sources.
//
// Clean-room textbook wah: a second-order resonant filter (resonant lowpass,
// constant-peak bandpass, or peaking-EQ section) whose centre frequency is
// either set directly (Manual mode) or, in Automatic mode, driven by an LFO and
// an envelope follower blended together. Sweeping a resonant peak across the
// spectrum produces the classic vowel-like "wah". The biquad is Pulp's own
// pulp::signal::Biquad (RBJ cookbook coefficients); the LFO, envelope follower,
// and source-blend are written here from the standard formulas. No third-party
// effect source was read for the DSP.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/biquad.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum WahParams : state::ParamID {
    kWahMode       = 1,  // 0 = Manual, 1 = Automatic
    kWahMix        = 2,  // 0..1 wet/dry
    kWahFreq       = 3,  // 200..1300 Hz manual centre (log)
    kWahQ          = 4,  // 0.1..20 resonance
    kWahGain       = 5,  // 0..20 dB
    kWahFilterType = 6,  // 0 = Res. LP, 1 = Band-Pass, 2 = Peaking
    kWahLfoRate    = 7,  // 0..5 Hz LFO rate
    kWahLfoEnvMix  = 8,  // 0 = pure LFO, 1 = pure envelope
    kWahEnvAttack  = 9,  // 0.0001..0.1 s
    kWahEnvRelease = 10, // 0.01..1 s
};

// Centre-frequency span swept by the automatic (LFO / envelope) sources.
inline constexpr float kWahMinHz = 200.0f;
inline constexpr float kWahMaxHz = 1300.0f;

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
        return {.name = "Wah-Wah", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.wah", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        // Order mirrors the truce reference parameter list.
        store.add_parameter({.id = kWahMode, .name = "Mode", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});  // 0=Manual, 1=Automatic
        store.add_parameter({.id = kWahMix, .name = "Mix", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 1.0f, 0.5f)});
        // Logarithmic frequency sweep: centre the normalized midpoint on the
        // geometric mean of [200, 1300] so the curve behaves like truce's log range.
        store.add_parameter({.id = kWahFreq, .name = "Frequency", .unit = "Hz",
                             .range = state::ParamRange::with_centre(200.0f, 1300.0f, 510.0f, 300.0f)});
        store.add_parameter({.id = kWahQ, .name = "Q", .unit = "",
                             .range = state::ParamRange::linear(0.1f, 20.0f, 10.0f)});
        store.add_parameter({.id = kWahGain, .name = "Gain", .unit = "dB",
                             .range = state::ParamRange::linear(0.0f, 20.0f, 20.0f)});
        store.add_parameter({.id = kWahFilterType, .name = "Filter Type", .unit = "",
                             .range = {0.0f, 2.0f, 0.0f, 1.0f}});  // 0=Res.LP, 1=Band-Pass, 2=Peaking
        store.add_parameter({.id = kWahLfoRate, .name = "LFO Rate", .unit = "Hz",
                             .range = state::ParamRange::linear(0.0f, 5.0f, 2.0f)});
        store.add_parameter({.id = kWahLfoEnvMix, .name = "LFO/Env Mix", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 1.0f, 0.8f)});
        store.add_parameter({.id = kWahEnvAttack, .name = "Env Attack", .unit = "s",
                             .range = state::ParamRange::linear(0.0001f, 0.1f, 0.002f)});
        store.add_parameter({.id = kWahEnvRelease, .name = "Env Release", .unit = "s",
                             .range = state::ParamRange::linear(0.01f, 1.0f, 0.3f)});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        for (auto& f : filters_) f.reset();
        envelopes_.fill(0.0f);
        lfo_phase_ = 0.0f;
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
            envelopes_.fill(0.0f);
            lfo_phase_ = 0.0f;
        }

        const bool automatic   = state().get_value(kWahMode) >= 0.5f;
        const int  type_index  = static_cast<int>(std::lround(state().get_value(kWahFilterType)));
        const auto type         = filter_type(type_index);
        const float manual_freq = std::clamp(state().get_value(kWahFreq), 20.0f, sample_rate_ * 0.45f);
        const float q           = std::max(0.1f, state().get_value(kWahQ));
        const float gain_db     = std::clamp(state().get_value(kWahGain), 0.0f, 40.0f);
        const float mix         = std::clamp(state().get_value(kWahMix), 0.0f, 1.0f);
        const float rate        = std::max(0.0f, state().get_value(kWahLfoRate));
        const float lfo_env_mix = std::clamp(state().get_value(kWahLfoEnvMix), 0.0f, 1.0f);
        const float attack_s    = std::max(0.0f, state().get_value(kWahEnvAttack));
        const float release_s   = std::max(0.0f, state().get_value(kWahEnvRelease));

        // The RBJ peaking section bakes the boost into its coefficients; for the
        // resonant-lowpass and bandpass shapes the Gain knob is a wet makeup gain
        // so it stays audible in every mode.
        const float biquad_gain_db = (type == signal::Biquad::Type::peaking) ? gain_db : 0.0f;
        const float makeup =
            (type == signal::Biquad::Type::peaking) ? 1.0f : std::pow(10.0f, gain_db / 20.0f);

        const float fmax = sample_rate_ * 0.45f;
        const float attack_co  = one_pole_coeff(attack_s);
        const float release_co = one_pole_coeff(release_s);
        const float lfo_inc    = rate / sample_rate_;  // cycles per sample

        if (!automatic) {
            // Manual mode: the centre is fixed, so tune each filter once.
            const float freq = std::clamp(manual_freq, 20.0f, fmax);
            for (std::size_t ch = 0; ch < channels; ++ch)
                filters_[ch].set_coefficients(type, freq, q, sample_rate_, biquad_gain_db);
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch);
                auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) {
                    const float dry = in[i];
                    const float wet = filters_[ch].process(dry) * makeup;
                    out[i] = dry * (1.0f - mix) + wet * mix;
                }
            }
        } else {
            // Automatic mode: the LFO is shared across channels; the envelope
            // follower is per-channel. Blend them, map into [min, max] Hz, and
            // retune each filter every sample.
            for (std::size_t i = 0; i < frames; ++i) {
                const float lfo_norm = 0.5f + 0.5f * std::sin(2.0f * kPi * lfo_phase_);
                lfo_phase_ += lfo_inc;
                if (lfo_phase_ >= 1.0f) lfo_phase_ -= 1.0f;

                for (std::size_t ch = 0; ch < channels; ++ch) {
                    const float dry = input.channel(ch)[i];
                    const float abs_in = std::fabs(dry);
                    float env = envelopes_[ch];
                    const float co = (abs_in > env) ? attack_co : release_co;
                    env = co * env + (1.0f - co) * abs_in;
                    envelopes_[ch] = env;

                    const float env_norm = std::clamp(env, 0.0f, 1.0f);
                    const float blended = lfo_norm + lfo_env_mix * (env_norm - lfo_norm);
                    const float freq =
                        std::clamp(kWahMinHz + blended * (kWahMaxHz - kWahMinHz), 20.0f, fmax);
                    filters_[ch].set_coefficients(type, freq, q, sample_rate_, biquad_gain_db);
                    const float wet = filters_[ch].process(dry) * makeup;
                    output.channel(ch)[i] = dry * (1.0f - mix) + wet * mix;
                }
            }
        }
        clear_extra(output, channels);
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    static signal::Biquad::Type filter_type(int index) {
        switch (index) {
            case 1:  return signal::Biquad::Type::bandpass;
            case 2:  return signal::Biquad::Type::peaking;
            case 0:
            default: return signal::Biquad::Type::lowpass;  // Res. LP
        }
    }

    // Standard one-pole smoothing coefficient for a time constant in seconds.
    // value_s == 0 yields an instantaneous follower (coeff 0).
    float one_pole_coeff(float value_s) const {
        if (value_s <= 0.0f) return 0.0f;
        return std::exp(-1.0f / (value_s * sample_rate_));
    }

    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }

    float sample_rate_ = 48000.0f;
    std::array<signal::Biquad, 8> filters_{};
    std::array<float, 8> envelopes_{};
    float lfo_phase_ = 0.0f;
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
