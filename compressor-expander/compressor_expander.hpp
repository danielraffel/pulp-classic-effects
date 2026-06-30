#pragma once

// Compressor / Expander — a stereo-linked dynamics processor with a single
// Mode dropdown that switches between downward compression (attenuate ABOVE the
// threshold) and downward expansion (attenuate BELOW the threshold).
//
// Clean-room textbook dynamics, written from the decibel-domain "log-domain
// decoupled peak detector" recipe in Reiss & McPherson's *Audio Effects*:
//
//   1. Detect the program level from a mono mixdown of the inputs and convert
//      it to dBFS (xg).
//   2. Apply the static input/output gain curve (yg):
//        compressor:  yg = xg            if xg <  threshold     (unity below)
//                     yg = T + (xg-T)/R  if xg >= threshold     (compressed)
//        expander:    yg = xg            if xg >  threshold     (unity above)
//                     yg = T + (xg-T)*R  if xg <= threshold     (expanded)
//      The instantaneous gain reduction is xl = xg - yg (>= 0 dB).
//   3. Smooth xl with an asymmetric one-pole: the engaging side uses the
//      attack coefficient, the relaxing side uses the release coefficient.
//      Coefficients are exp(-1/(t*fs)) for a time constant t in seconds.
//   4. Apply makeup gain and the smoothed reduction:  g = 10^((makeup - yl)/20).
//
// The detector is stereo-linked (one mono mixdown drives one gain applied to
// every channel) so the stereo image is preserved. No third-party effect
// source was read; the algorithm is the published textbook recipe.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum CompExpParams : state::ParamID {
    kMode      = 1,  // 0 = Compressor, 1 = Expander
    kThreshold = 2,  // -60..0 dB
    kRatio     = 3,  // 1..100 : 1
    kAttack    = 4,  // 0.0001..0.1 s
    kRelease   = 5,  // 0.01..1.0 s
    kMakeup    = 6,  // -12..12 dB
    kCxBypass  = 7,  // toggle
};

// Defined out-of-line in compressor_expander_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
std::unique_ptr<view::View> build_compressor_expander_editor(state::StateStore& store);

class CompressorExpanderProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
    std::unique_ptr<view::View> create_view() override { return build_compressor_expander_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Compressor", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.compressor-expander", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        // Mode dropdown: index 0 = Compressor, 1 = Expander (stepped enum).
        store.add_parameter({.id = kMode, .name = "Mode", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
        store.add_parameter({.id = kThreshold, .name = "Threshold", .unit = "dB",
                             .range = {-60.0f, 0.0f, -24.0f, 0.0f}});
        store.add_parameter({.id = kRatio, .name = "Ratio", .unit = ":1",
                             .range = {1.0f, 100.0f, 50.0f, 0.0f}});
        store.add_parameter({.id = kAttack, .name = "Attack", .unit = "s",
                             .range = state::ParamRange::linear(0.0001f, 0.1f, 0.002f)});
        store.add_parameter({.id = kRelease, .name = "Release", .unit = "s",
                             .range = state::ParamRange::linear(0.01f, 1.0f, 0.3f)});
        store.add_parameter({.id = kMakeup, .name = "Makeup", .unit = "dB",
                             .range = {-12.0f, 12.0f, 0.0f, 0.0f}});
        store.add_parameter({.id = kCxBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        inv_sr_ = 1.0f / static_cast<float>(ctx.sample_rate);
        reset_state();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min(output.num_channels(), input.num_channels());
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) reset_state();

        if (state().get_value(kCxBypass) >= 0.5f) {
            // Pass input → output untouched. The detector state is intentionally
            // left as-is so the envelope resumes smoothly when un-bypassed.
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            gain_reduction_db_ = 0.0f;  // don't freeze the meter while bypassed
            clear_extra(output, channels);
            return;
        }

        const bool expander = state().get_value(kMode) >= 0.5f;
        const float thr   = state().get_value(kThreshold);
        const float ratio = std::max(1.0f, state().get_value(kRatio));
        const float makeup = state().get_value(kMakeup);
        const float alpha_a = time_coeff(state().get_value(kAttack));
        const float alpha_r = time_coeff(state().get_value(kRelease));

        const float inv_ch = channels > 0 ? 1.0f / static_cast<float>(channels) : 0.0f;

        float max_reduction_db = 0.0f;  // most reduction this block, for metering
        for (std::size_t i = 0; i < frames; ++i) {
            // Mono mixdown of the inputs drives one shared (stereo-linked) gain.
            float mix = 0.0f;
            for (std::size_t ch = 0; ch < channels; ++ch) mix += input.channel(ch)[i];
            mix *= inv_ch;

            const float in_sq = mix * mix;
            if (expander) {
                // Slow integration of the squared input: the book's remedy for
                // the gain pumping an instantaneous detector would cause on the
                // steep expander curve.
                constexpr float kAverage = 0.9999f;
                level_ = kAverage * level_ + (1.0f - kAverage) * in_sq;
            } else {
                level_ = in_sq;
            }

            const float xg = level_ <= 1e-6f ? -60.0f : 10.0f * std::log10(level_);

            // Static gain curve → instantaneous reduction xl (dB, >= 0).
            float yg;
            if (expander)
                yg = xg > thr ? xg : thr + (xg - thr) * ratio;
            else
                yg = xg < thr ? xg : thr + (xg - thr) / ratio;
            const float xl = xg - yg;

            // Asymmetric ballistics. Engaging (reduction growing for a
            // compressor / shrinking-toward-more-attenuation for an expander)
            // uses attack; relaxing uses release.
            const bool engaging = expander ? (xl < yl_prev_) : (xl > yl_prev_);
            const float alpha = engaging ? alpha_a : alpha_r;
            const float yl = alpha * yl_prev_ + (1.0f - alpha) * xl;
            yl_prev_ = yl;

            const float reduction_db = std::max(0.0f, yl);
            max_reduction_db = std::max(max_reduction_db, reduction_db);

            const float gain = std::pow(10.0f, (makeup - yl) * 0.05f);
            for (std::size_t ch = 0; ch < channels; ++ch)
                output.channel(ch)[i] = input.channel(ch)[i] * gain;
        }
        gain_reduction_db_ = max_reduction_db;  // positive dB of reduction
        clear_extra(output, channels);
    }

    /// Most recent block's peak gain reduction in dB (>= 0). For a meter.
    float gain_reduction_db() const { return gain_reduction_db_; }

private:
    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }
    void reset_state() { level_ = 0.0f; yl_prev_ = 0.0f; gain_reduction_db_ = 0.0f; }
    // One-pole coefficient exp(-1/(t*fs)) for a time constant t seconds.
    float time_coeff(float seconds) const {
        if (seconds <= 0.0f) return 0.0f;
        return std::exp(-inv_sr_ / seconds);
    }
    float inv_sr_ = 1.0f / 48000.0f;
    float level_ = 0.0f;             // detector level (squared domain)
    float yl_prev_ = 0.0f;           // smoothed gain reduction (dB)
    float gain_reduction_db_ = 0.0f; // last block's peak reduction (dB)
};

inline std::unique_ptr<format::Processor> create_compressor_expander() {
    return std::make_unique<CompressorExpanderProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_compressor_expander_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
#include "compressor_expander_editor.hpp"
