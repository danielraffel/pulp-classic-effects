#pragma once

// Ring Modulation — multiply the input by a sine carrier.
//
// Clean-room textbook ring mod: wet = input * carrier(t), output = dry/wet mix.
// One carrier oscillator drives all channels in phase. Built on Pulp's own
// pulp::signal::Oscillator; no third-party effect source was read. See the
// repo README for the algorithmic reference.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>
#include <pulp/signal/oscillator.hpp>

#include <algorithm>
#include <memory>

namespace pulp::examples::classic {

enum RingModParams : state::ParamID {
    kCarrierHz = 1,
    kMix       = 2,  // 0..100 % dry->wet
    kRmBypass  = 3,
};

// Defined out-of-line in ring_mod_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
std::unique_ptr<view::View> build_ring_mod_editor(state::StateStore& store);

class RingModProcessor : public format::Processor {
public:
    // Hand the host our dark Ink & Signal editor; the framework owns the
    // returned tree and may call this once per attached editor window.
    std::unique_ptr<view::View> create_view() override { return build_ring_mod_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "RingMod", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.ring-mod", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kCarrierHz, .name = "Carrier", .unit = "Hz",
                             .range = state::ParamRange::with_centre(1.0f, 4000.0f, 200.0f, 200.0f)});
        store.add_parameter({.id = kMix, .name = "Mix", .unit = "%",
                             .range = {0.0f, 100.0f, 100.0f, 0.0f}});
        store.add_parameter({.id = kRmBypass, .name = "Bypass", .unit = "",
                             .range = {0.0f, 1.0f, 0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        carrier_.set_sample_rate(sample_rate_);
        carrier_.set_waveform(signal::Oscillator::Waveform::sine);
        carrier_.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext&) override {
        const std::size_t channels = std::min(output.num_channels(), input.num_channels());
        const std::size_t frames = output.num_samples();

        if (state().get_value(kRmBypass) >= 0.5f) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto in = input.channel(ch); auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, channels);
            return;
        }

        const float mix = std::clamp(state().get_value(kMix) / 100.0f, 0.0f, 1.0f);
        carrier_.set_frequency(state().get_value(kCarrierHz));

        for (std::size_t i = 0; i < frames; ++i) {
            const float c = carrier_.next();  // -1..1
            for (std::size_t ch = 0; ch < channels; ++ch) {
                const float dry = input.channel(ch)[i];
                output.channel(ch)[i] = dry * (1.0f - mix) + (dry * c) * mix;
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
    signal::Oscillator carrier_;
};

inline std::unique_ptr<format::Processor> create_ring_mod() {
    return std::make_unique<RingModProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_ring_mod_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
#include "ring_mod_editor.hpp"
