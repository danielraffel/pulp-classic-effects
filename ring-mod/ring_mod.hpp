#pragma once

// Ring Modulation — multiply the input by a periodic carrier oscillator.
//
// Clean-room textbook ring mod (Reiss & McPherson, "Audio Effects", ch. on
// modulation): a carrier oscillator c(t) in [-1, 1] multiplies the input, and
// a Depth control crossfades from the dry signal to the fully modulated one:
//
//     gain = (1 - depth) + depth * c(t)
//     out  = in * gain
//
// At Depth = 0 the output is the dry input; at Depth = 1 it is the pure ring
// product in * c(t), whose spectrum is the input shifted into sum/difference
// sidebands around each carrier harmonic. One carrier drives all channels in
// phase. The carrier waveform is selectable (sine through a sloped square).
//
// The oscillator is hand-written here (a single normalized phase accumulator
// plus per-shape closed forms) rather than pulled from any third-party effect
// source. See the repo README for the algorithmic reference.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace pulp::examples::classic {

enum RingModParams : state::ParamID {
    kRmDepth    = 1,  // 0..1 dry -> fully modulated blend
    kRmFreq     = 2,  // carrier frequency, Hz (log-shaped 10..1000)
    kRmWaveform = 3,  // carrier shape index, see kWaveformLabels
};

// Carrier waveform options, in selection order. The discrete parameter stores
// the index; the editor combo shows these labels.
enum class Waveform : int {
    Sine = 0,
    Triangle,
    Sawtooth,
    InverseSawtooth,
    Square,
    SquareSloped,
    Count,
};

inline const std::vector<std::string>& waveform_labels() {
    static const std::vector<std::string> labels = {
        "Sine", "Triangle", "Sawtooth", "Inv. Saw", "Square", "Sq. Sloped"};
    return labels;
}

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
        return {.name = "Ring Mod", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.ring-mod", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kRmDepth, .name = "Depth", .unit = "",
                             .range = state::ParamRange::linear(0.0f, 1.0f, 0.5f)});
        // Carrier Freq, log-shaped so the dial spans 10..1000 Hz musically.
        // with_centre derives the skew that lands the geometric midpoint
        // (sqrt(10*1000) = 100 Hz) at the dial centre — a log-like sweep.
        store.add_parameter({.id = kRmFreq, .name = "Carrier Freq", .unit = "Hz",
                             .range = state::ParamRange::with_centre(10.0f, 1000.0f, 100.0f, 200.0f)});
        // Discrete carrier waveform selector (Sine .. Sq. Sloped).
        store.add_parameter({.id = kRmWaveform, .name = "Waveform", .unit = "",
                             .range = {0.0f, static_cast<float>(static_cast<int>(Waveform::Count) - 1),
                                       0.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        inv_sr_ = sample_rate_ > 0.0f ? 1.0f / sample_rate_ : 0.0f;
        phase_ = 0.0f;
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext&) override {
        const std::size_t channels = std::min(output.num_channels(), input.num_channels());
        const std::size_t frames = output.num_samples();

        const float depth = std::clamp(state().get_value(kRmDepth), 0.0f, 1.0f);
        const float freq  = std::max(0.0f, state().get_value(kRmFreq));
        const auto wave   = to_waveform(state().get_value(kRmWaveform));
        const float inc   = freq * inv_sr_;

        for (std::size_t i = 0; i < frames; ++i) {
            const float c = carrier(phase_, wave);          // bipolar [-1, 1]
            const float gain = (1.0f - depth) + depth * c;  // dry -> ring blend
            for (std::size_t ch = 0; ch < channels; ++ch)
                output.channel(ch)[i] = input.channel(ch)[i] * gain;

            phase_ += inc;
            phase_ -= std::floor(phase_);  // wrap to [0, 1)
        }
        clear_extra(output, channels);
    }

private:
    static Waveform to_waveform(float raw) {
        int idx = static_cast<int>(std::lround(raw));
        idx = std::clamp(idx, 0, static_cast<int>(Waveform::Count) - 1);
        return static_cast<Waveform>(idx);
    }

    // Closed-form bipolar carrier shapes over a normalized phase in [0, 1).
    // Each returns a value in [-1, 1]; the shapes are deliberately distinct so
    // the timbre of the modulation changes with the selection.
    static float carrier(float phase, Waveform wave) {
        constexpr float kTwoPi = 6.283185307179586f;
        switch (wave) {
            case Waveform::Sine:
                return std::sin(kTwoPi * phase);
            case Waveform::Triangle:
                // Peaks +1 at phase 0, -1 at phase 0.5.
                return 1.0f - 4.0f * std::fabs(phase - 0.5f);
            case Waveform::Sawtooth:
                // Rising ramp -1 -> +1 across the period.
                return 2.0f * phase - 1.0f;
            case Waveform::InverseSawtooth:
                // Falling ramp +1 -> -1 across the period.
                return 1.0f - 2.0f * phase;
            case Waveform::Square:
                return phase < 0.5f ? 1.0f : -1.0f;
            case Waveform::SquareSloped: {
                // Trapezoid: a square whose edges ramp over a short window to
                // soften the discontinuity (gentler high-harmonic content).
                constexpr float kEdge = 0.05f;  // ramp width as fraction of period
                if (phase < kEdge)               return 1.0f - (phase / kEdge) * 2.0f;        // +1 -> -1
                if (phase < 0.5f - kEdge)        return -1.0f;
                if (phase < 0.5f + kEdge)        return -1.0f + ((phase - (0.5f - kEdge)) / (2.0f * kEdge)) * 2.0f; // -1 -> +1
                if (phase < 1.0f - kEdge)        return 1.0f;
                return 1.0f - ((phase - (1.0f - kEdge)) / kEdge) * 2.0f;                       // +1 -> -1 (period end)
            }
            case Waveform::Count:
            default:
                return 0.0f;
        }
    }

    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }

    float sample_rate_ = 48000.0f;
    float inv_sr_      = 1.0f / 48000.0f;
    float phase_       = 0.0f;  // normalized carrier phase in [0, 1)
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
