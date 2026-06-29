#pragma once

// Robotization — zero the short-time phase to impose a fixed, monotone pitch.
//
// Clean-room textbook robotization (Zölzer DAFX, "robotization"): take the
// short-time Fourier transform, discard the phase of every bin (set it to
// zero, keeping the magnitude), and resynthesize. Forcing zero phase makes
// every frame restart in phase at the analysis-hop rate, so the output takes on
// a steady "robot voice" pitch at that rate regardless of the input pitch,
// while the magnitude spectrum (the formants) is preserved. Built on Pulp's own
// pulp::signal::SpectralFrameEngine (analysis→modify→resynthesis with exact
// overlap-add); no third-party effect source was read. See the README.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/spectral_frame_engine.hpp>
#include <pulp/signal/delay_line.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <memory>

namespace pulp::examples::classic {

enum RobotizationParams : state::ParamID {
    kRobotMix    = 1,   // 0..100 %
    kRobotBypass = 2,
};

// Defined out-of-line in robotization_editor.hpp (included at the bottom).
std::unique_ptr<view::View> build_robotization_editor(state::StateStore& store);

class RobotizationProcessor : public format::Processor {
public:
    std::unique_ptr<view::View> create_view() override { return build_robotization_editor(state()); }

    static constexpr int kFftSize = 1024;
    static constexpr int kHop     = 256;
    static constexpr int kChannels = 2;

    format::PluginDescriptor descriptor() const override {
        return {.name = "Robotization", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.robotization", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kRobotMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 100.0f, 0.0f}});
        store.add_parameter({.id = kRobotBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        signal::SpectralFrameEngineConfig cfg;
        cfg.fft_size = kFftSize;
        cfg.analysis_hop = kHop;
        cfg.channels = kChannels;
        cfg.max_block = std::max(1, ctx.max_buffer_size);
        engine_.prepare(cfg);
        latency_ = engine_.latency_samples();
        // Dry path is delayed by the engine latency so the wet (robotized) and
        // dry signals stay phase-aligned in the mix.
        for (auto& d : dry_) d.prepare(latency_ + cfg.max_block + 4);
        for (auto& d : dry_) d.reset();
        engine_.reset();
    }

    void release() override { engine_.reset(); for (auto& d : dry_) d.reset(); }

    int latency_samples() const override { return latency_; }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t in_ch = input.num_channels();
        const std::size_t out_ch = output.num_channels();
        const int frames = static_cast<int>(output.num_samples());

        if (ctx.should_reset_dsp_state()) { engine_.reset(); for (auto& d : dry_) d.reset(); }

        // True (latency-free) passthrough on bypass, or when the bus can't host
        // the stereo spectral path.
        if (state().get_value(kRobotBypass) >= 0.5f || out_ch < 2 || in_ch < 1) {
            for (std::size_t ch = 0; ch < std::min(in_ch, out_ch); ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (int i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, std::min(in_ch, out_ch));
            return;
        }

        const float mix = std::clamp(state().get_value(kRobotMix) / 100.0f, 0.0f, 1.0f);

        // Build the channel pointer arrays the engine expects. A mono input
        // feeds both engine channels.
        const float* in_ptrs[kChannels];
        float* out_ptrs[kChannels];
        in_ptrs[0] = input.channel(0).data();
        in_ptrs[1] = (in_ch >= 2) ? input.channel(1).data() : in_ptrs[0];
        out_ptrs[0] = output.channel(0).data();
        out_ptrs[1] = output.channel(1).data();

        // Analysis → zero-phase → resynthesis. The callback runs once per
        // completed frame and may modify the bins in place.
        engine_.process(in_ptrs, out_ptrs, frames,
                        [](std::complex<float>* const* bins, int num_bins) {
                            for (int ch = 0; ch < kChannels; ++ch)
                                for (int k = 0; k < num_bins; ++k)
                                    bins[ch][k] = std::complex<float>(std::abs(bins[ch][k]), 0.0f);
                        });

        // Blend with the latency-aligned dry signal.
        for (std::size_t ch = 0; ch < 2; ++ch) {
            auto in = input.channel((ch < in_ch) ? ch : 0);
            auto out = output.channel(ch);
            auto& line = dry_[ch];
            for (int i = 0; i < frames; ++i) {
                line.push(in[i]);
                const float dry_delayed = line.read(static_cast<float>(latency_));
                out[i] = dry_delayed * (1.0f - mix) + out[i] * mix;
            }
        }
        clear_extra(output, 2);
    }

private:
    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }
    float sample_rate_ = 48000.0f;
    int latency_ = 0;
    signal::SpectralFrameEngine engine_;
    std::array<signal::DelayLine, 2> dry_{};
};

inline std::unique_ptr<format::Processor> create_robotization() {
    return std::make_unique<RobotizationProcessor>();
}

} // namespace pulp::examples::classic

#include "robotization_editor.hpp"
