#pragma once

// Compressor / Expander — a stereo-linked dynamics processor.
//
// Clean-room textbook dynamics: a peak envelope follower (with attack/release
// ballistics) tracks the program level, a static gain curve attenuates levels
// ABOVE the compressor threshold (downward compression) and levels BELOW the
// expander threshold (downward expansion), and a makeup gain restores level.
// The detector is stereo-linked (max across channels) so both channels share
// one gain and the stereo image is preserved. Built on Pulp's own
// pulp::signal::BallisticsFilter; no third-party effect source was read.

#include <pulp/format/processor.hpp>
#include <pulp/signal/ballistics_filter.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum CompExpParams : state::ParamID {
    kCompThreshold = 1,  // -60..0 dB
    kCompRatio     = 2,  // 1..20 : 1
    kExpThreshold  = 3,  // -80..-20 dB
    kExpRatio      = 4,  // 1..10 : 1
    kAttackMs      = 5,  // 0.1..100 ms
    kReleaseMs     = 6,  // 5..1000 ms
    kMakeupDb      = 7,  // -12..24 dB
    kCxBypass      = 8,
};

class CompressorExpanderProcessor : public format::Processor {
public:
    format::PluginDescriptor descriptor() const override {
        return {.name = "Comp/Expander", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.compressor-expander", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kCompThreshold, .name = "Comp Thr", .unit = "dB",
                             .range = {-60.0f, 0.0f, -18.0f, 0.0f}});
        store.add_parameter({.id = kCompRatio, .name = "Comp Ratio", .unit = ":1",
                             .range = state::ParamRange::with_centre(1.0f, 20.0f, 4.0f, 3.0f)});
        store.add_parameter({.id = kExpThreshold, .name = "Exp Thr", .unit = "dB",
                             .range = {-80.0f, -20.0f, -50.0f, 0.0f}});
        store.add_parameter({.id = kExpRatio, .name = "Exp Ratio", .unit = ":1",
                             .range = {1.0f, 10.0f, 2.0f, 0.0f}});
        store.add_parameter({.id = kAttackMs, .name = "Attack", .unit = "ms",
                             .range = state::ParamRange::with_centre(0.1f, 100.0f, 10.0f, 5.0f)});
        store.add_parameter({.id = kReleaseMs, .name = "Release", .unit = "ms",
                             .range = state::ParamRange::with_centre(5.0f, 1000.0f, 150.0f, 150.0f)});
        store.add_parameter({.id = kMakeupDb, .name = "Makeup", .unit = "dB",
                             .range = {-12.0f, 24.0f, 0.0f, 0.0f}});
        store.add_parameter({.id = kCxBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        detector_.prepare(sample_rate_);
        detector_.set_mode(signal::BallisticsFilter::Mode::peak);
        detector_.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min(output.num_channels(), input.num_channels());
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) detector_.reset();

        if (state().get_value(kCxBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            gain_reduction_db_ = 0.0f;  // don't freeze the meter while bypassed
            clear_extra(output, channels);
            return;
        }

        const float comp_thr = state().get_value(kCompThreshold);
        const float comp_ratio = std::max(1.0f, state().get_value(kCompRatio));
        // Expander threshold can never sit above the compressor threshold, or the
        // unity region between them would invert.
        const float exp_thr = std::min(state().get_value(kExpThreshold), comp_thr);
        const float exp_ratio = std::max(1.0f, state().get_value(kExpRatio));
        const float makeup = state().get_value(kMakeupDb);
        detector_.set_attack_ms(state().get_value(kAttackMs));
        detector_.set_release_ms(state().get_value(kReleaseMs));

        float min_gain_db = 0.0f;  // most reduction this block, for metering
        for (std::size_t i = 0; i < frames; ++i) {
            // Stereo-linked peak detector: feed the max magnitude across channels.
            float linked = 0.0f;
            for (std::size_t ch = 0; ch < channels; ++ch)
                linked = std::max(linked, std::fabs(input.channel(ch)[i]));
            const float env = detector_.process(linked);
            const float env_db = 20.0f * std::log10(std::max(env, 1e-9f));

            float gain_db = 0.0f;
            if (env_db > comp_thr)
                gain_db = (comp_thr - env_db) * (1.0f - 1.0f / comp_ratio);   // <= 0
            else if (env_db < exp_thr)
                gain_db = (env_db - exp_thr) * (exp_ratio - 1.0f);            // <= 0
            gain_db = std::max(gain_db, -80.0f);                              // floor
            min_gain_db = std::min(min_gain_db, gain_db);

            const float gain = std::pow(10.0f, (gain_db + makeup) / 20.0f);
            for (std::size_t ch = 0; ch < channels; ++ch)
                output.channel(ch)[i] = input.channel(ch)[i] * gain;
        }
        gain_reduction_db_ = -min_gain_db;  // positive dB of reduction
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
    float sample_rate_ = 48000.0f;
    float gain_reduction_db_ = 0.0f;
    signal::BallisticsFilter detector_;
};

inline std::unique_ptr<format::Processor> create_compressor_expander() {
    return std::make_unique<CompressorExpanderProcessor>();
}

} // namespace pulp::examples::classic
