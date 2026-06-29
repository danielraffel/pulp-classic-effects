#pragma once

// Vibrato — pitch modulation via an LFO-swept fractional delay.
//
// Clean-room textbook vibrato: a low-frequency oscillator sweeps the read
// position of a short fractional delay line, so the output pitch wavers. 100%
// wet (the effect *is* the modulated signal). Built on Pulp's own
// pulp::signal::DelayLine + Oscillator; no third-party effect source was read.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/delay_line.hpp>
#include <pulp/signal/oscillator.hpp>

#include <algorithm>
#include <array>
#include <memory>

namespace pulp::examples::classic {

enum VibratoParams : state::ParamID {
    kVibRateHz  = 1,  // 0.1..12 Hz
    kVibDepthMs = 2,  // 0..10 ms
    kVibBypass  = 3,
};

// Defined out-of-line in vibrato_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
std::unique_ptr<view::View> build_vibrato_editor(state::StateStore& store);

class VibratoProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
    std::unique_ptr<view::View> create_view() override { return build_vibrato_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Vibrato", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.vibrato", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kVibRateHz, .name = "Rate", .unit = "Hz",
                             .range = state::ParamRange::with_centre(0.1f, 12.0f, 4.0f, 4.0f)});
        store.add_parameter({.id = kVibDepthMs, .name = "Depth", .unit = "ms",
                             .range = {0.0f, 10.0f, 3.0f, 0.0f}});
        store.add_parameter({.id = kVibBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        max_delay_ = static_cast<int>(sample_rate_ * 0.025f) + 4;  // ~25 ms
        for (auto& line : lines_) line.prepare(max_delay_);
        lfo_.set_sample_rate(sample_rate_);
        lfo_.set_waveform(signal::Oscillator::Waveform::sine);
        lfo_.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), lines_.size()});
        const std::size_t frames = output.num_samples();

        // Clear stale delay history on a transport seek/loop.
        if (ctx.should_reset_dsp_state())
            for (auto& line : lines_) line.reset();

        if (state().get_value(kVibBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        lfo_.set_frequency(state().get_value(kVibRateHz));
        float depth = state().get_value(kVibDepthMs) / 1000.0f * sample_rate_;
        depth = std::clamp(depth, 0.0f, static_cast<float>(max_delay_ / 2 - 2));
        const float base = depth + 1.0f;  // keep read position >= 1 sample

        // One LFO drives all channels in phase. Push first, then read so the
        // freshest sample is available.
        for (std::size_t i = 0; i < frames; ++i) {
            const float mod = lfo_.next();  // -1..1
            const float read_pos = base + depth * mod;  // in [1, 2*depth+1]
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto& line = lines_[ch];
                line.push(input.channel(ch)[i]);
                output.channel(ch)[i] = line.read(read_pos);
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
    int max_delay_ = 1204;
    std::array<signal::DelayLine, 8> lines_{};
    signal::Oscillator lfo_;
};

inline std::unique_ptr<format::Processor> create_vibrato() {
    return std::make_unique<VibratoProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_vibrato_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
#include "vibrato_editor.hpp"
