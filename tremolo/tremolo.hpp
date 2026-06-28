#pragma once

// Tremolo — periodic amplitude modulation.
//
// Clean-room implementation of the classic tremolo effect: the input signal's
// amplitude is modulated by a low-frequency oscillator. With depth d in [0,1]
// and a unipolar LFO m(t) in [0,1], the gain applied per sample is
//
//     g(t) = (1 - d) + d * m(t)
//
// so depth 0 is a no-op (unity gain) and depth 1 sweeps the gain between 0 and
// 1 over each LFO cycle. This is standard textbook amplitude modulation; see the
// algorithmic reference cited in this folder's README. The implementation is
// independent and built on Pulp's own signal primitives (no third-party DSP
// source was read or transcribed).

#include <pulp/format/processor.hpp>
#include <pulp/signal/oscillator.hpp>

#include <algorithm>
#include <array>
#include <memory>

namespace pulp::examples::classic {

enum TremoloParams : state::ParamID {
    kRate     = 1,  // LFO rate in Hz
    kDepth    = 2,  // modulation depth, 0..100 %
    kWaveform = 3,  // 0 = sine, 1 = triangle, 2 = square
    kBypass   = 4,
};

class TremoloProcessor : public format::Processor {
public:
    format::PluginDescriptor descriptor() const override {
        return {
            .name = "Tremolo",
            .manufacturer = "Pulp Examples",
            .bundle_id = "com.pulp.examples.tremolo",
            .version = "0.1.0",
            .category = format::PluginCategory::Effect,
            .input_buses = {{"Audio In", 2}},
            .output_buses = {{"Audio Out", 2}},
        };
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({
            .id = kRate,
            .name = "Rate",
            .unit = "Hz",
            // Shaped so the musically useful low rates get more resolution.
            .range = state::ParamRange::with_centre(0.1f, 20.0f, 4.0f, 4.0f),
        });
        store.add_parameter({
            .id = kDepth,
            .name = "Depth",
            .unit = "%",
            .range = {0.0f, 100.0f, 50.0f, 0.0f},
        });
        store.add_parameter({
            .id = kWaveform,
            .name = "Waveform",
            .unit = "",
            .range = {0.0f, 2.0f, 0.0f, 1.0f},  // stepped enum
        });
        store.add_parameter({
            .id = kBypass,
            .name = "Bypass",
            .unit = "",
            .range = {0.0f, 1.0f, 0.0f, 1.0f},
        });
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        lfo_.set_sample_rate(sample_rate_);
        lfo_.reset();
    }

    void release() override { lfo_.reset(); }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext&) override {
        const std::size_t channels =
            std::min(output.num_channels(), input.num_channels());
        const std::size_t frames = output.num_samples();

        if (state().get_value(kBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch);
                auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra_outputs(output, channels);
            return;
        }

        const float depth =
            std::clamp(state().get_value(kDepth) / 100.0f, 0.0f, 1.0f);
        lfo_.set_frequency(state().get_value(kRate));
        lfo_.set_waveform(waveform_from_param(state().get_value(kWaveform)));

        // One LFO drives all channels so they modulate in phase. Compute the
        // per-sample gain once, then apply across channels.
        for (std::size_t i = 0; i < frames; ++i) {
            // Clamp the LFO: the band-limited triangle's leaky integrator can
            // briefly overshoot ±1 at startup, which would otherwise push
            // full-depth gain above unity.
            const float bipolar = std::clamp(lfo_.next(), -1.0f, 1.0f);
            const float unipolar = 0.5f * (bipolar + 1.0f); // 0..1
            const float gain = (1.0f - depth) + depth * unipolar;
            for (std::size_t ch = 0; ch < channels; ++ch) {
                output.channel(ch)[i] = input.channel(ch)[i] * gain;
            }
        }
        clear_extra_outputs(output, channels);
    }

private:
    // Output buses may have more channels than the input; zero any the effect
    // didn't write so the host never sees stale buffer contents.
    static void clear_extra_outputs(audio::BufferView<float>& output,
                                    std::size_t written) {
        const std::size_t frames = output.num_samples();
        for (std::size_t ch = written; ch < output.num_channels(); ++ch) {
            auto out = output.channel(ch);
            for (std::size_t i = 0; i < frames; ++i) out[i] = 0.0f;
        }
    }

    static signal::Oscillator::Waveform waveform_from_param(float value) {
        switch (static_cast<int>(value + 0.5f)) {
            case 1:  return signal::Oscillator::Waveform::triangle;
            case 2:  return signal::Oscillator::Waveform::square;
            default: return signal::Oscillator::Waveform::sine;
        }
    }

    float sample_rate_ = 48000.0f;
    signal::Oscillator lfo_;
};

inline std::unique_ptr<format::Processor> create_tremolo() {
    return std::make_unique<TremoloProcessor>();
}

} // namespace pulp::examples::classic
