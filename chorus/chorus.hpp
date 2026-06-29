#pragma once

// Chorus — a short LFO-swept delay blended with the dry signal.
//
// Clean-room textbook chorus: a low-frequency oscillator sweeps the read
// position of a short fractional delay (centred on a ~15 ms base delay), and the
// delayed, pitch-wavering copy is mixed back with the dry input. The small
// continuously-varying delay detunes the copy, producing the shimmering
// "many voices" thickening. Built on Pulp's own pulp::signal::DelayLine +
// Oscillator; no third-party effect source was read.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/delay_line.hpp>
#include <pulp/signal/oscillator.hpp>

#include <algorithm>
#include <array>
#include <memory>

namespace pulp::examples::classic {

enum ChorusParams : state::ParamID {
    kChorusRate   = 1,  // 0.05..8 Hz
    kChorusDepth  = 2,  // 0..10 ms
    kChorusMix    = 3,  // 0..100 %
    kChorusBypass = 4,
};

// Defined out-of-line in chorus_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
std::unique_ptr<view::View> build_chorus_editor(state::StateStore& store);

class ChorusProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
    std::unique_ptr<view::View> create_view() override { return build_chorus_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Chorus", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.chorus", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kChorusRate, .name = "Rate", .unit = "Hz",
                             .range = state::ParamRange::with_centre(0.05f, 8.0f, 0.8f, 0.8f)});
        store.add_parameter({.id = kChorusDepth, .name = "Depth", .unit = "ms",
                             .range = {0.0f, 10.0f, 4.0f, 0.0f}});
        store.add_parameter({.id = kChorusMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 50.0f, 0.0f}});
        store.add_parameter({.id = kChorusBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        base_samples_ = 0.015f * sample_rate_;                  // ~15 ms centre
        max_delay_ = static_cast<int>(sample_rate_ * 0.035f) + 4; // 35 ms headroom
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

        if (ctx.should_reset_dsp_state()) {
            for (auto& line : lines_) line.reset();
            lfo_.reset();  // deterministic phase after a transport jump
        }

        if (state().get_value(kChorusBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        lfo_.set_frequency(state().get_value(kChorusRate));
        const float mix = std::clamp(state().get_value(kChorusMix) / 100.0f, 0.0f, 1.0f);
        float depth = state().get_value(kChorusDepth) / 1000.0f * sample_rate_;
        // Keep the swept read position strictly inside (1, base + depth] so it
        // never underruns the line or reads the just-written sample. The
        // std::max guards the (unreachable) case base < 2 so clamp's lo<=hi.
        depth = std::clamp(depth, 0.0f, std::max(0.0f, base_samples_ - 2.0f));

        // One LFO drives all channels in phase (mono-coherent chorus); stereo
        // widening would phase-offset the LFO per channel.
        for (std::size_t i = 0; i < frames; ++i) {
            const float mod = lfo_.next();                  // -1..1
            const float read_pos = base_samples_ + depth * mod;
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto& line = lines_[ch];
                const float dry = input.channel(ch)[i];
                line.push(dry);
                const float wet = line.read(read_pos);
                output.channel(ch)[i] = dry * (1.0f - mix) + wet * mix;
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
    float base_samples_ = 720.0f;
    int max_delay_ = 1684;
    std::array<signal::DelayLine, 8> lines_{};
    signal::Oscillator lfo_;
};

inline std::unique_ptr<format::Processor> create_chorus() {
    return std::make_unique<ChorusProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_chorus_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
#include "chorus_editor.hpp"
