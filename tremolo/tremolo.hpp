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
// algorithmic reference cited in this folder's README. The LFO is a self-
// contained phase accumulator with hand-written unipolar waveform shapers (no
// third-party DSP source was read or transcribed).

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

// Parameter ids. Declaration order is the host/editor order: Depth, Rate,
// Waveform. No bypass parameter — the host provides transport-level bypass.
enum TremoloParams : state::ParamID {
    kTremDepth    = 1,  // modulation depth, 0..1 (0 = unity gain)
    kRate     = 2,  // LFO rate in Hz
    kTremWaveform = 3,  // LFO shape, indexes the editor's Combo labels
};

// LFO shapes. The integer value of each enumerator is the stored parameter
// index, which must line up with the Combo option labels in the editor:
// Sine, Triangle, Sawtooth, Inv. Saw, Square, Sq. Sloped.
enum class TremoloWaveform : int {
    Sine = 0,
    Triangle,
    Sawtooth,
    InvSaw,
    Square,
    SqSloped,
    Count,
};

// Defined out-of-line in tremolo_editor.hpp (included at the bottom of this
// file). Forward-declared here so create_view() can hand the host the same
// dark Ink & Signal editor the screenshot tests render.
std::unique_ptr<view::View> build_tremolo_editor(state::StateStore& store);

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
            .id = kTremDepth,
            .name = "Depth",
            .unit = "",
            .range = {0.0f, 1.0f, 0.5f, 0.0f},
        });
        store.add_parameter({
            .id = kRate,
            .name = "Rate",
            .unit = "Hz",
            .range = {0.0f, 10.0f, 2.0f, 0.0f},
        });
        store.add_parameter({
            .id = kTremWaveform,
            .name = "Waveform",
            .unit = "",
            // Stepped enum: 0..(Count-1), default Sine, unit step.
            .range = {0.0f,
                      static_cast<float>(static_cast<int>(TremoloWaveform::Count) - 1),
                      0.0f, 1.0f},
        });
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        inv_sr_ = sample_rate_ > 0.0f ? 1.0f / sample_rate_ : 0.0f;
        phase_ = 0.0f;
    }

    void release() override { phase_ = 0.0f; }

    // Hand the host our dark Ink & Signal editor. The framework owns the
    // returned tree and may call this once per attached editor window.
    std::unique_ptr<view::View> create_view() override {
        return build_tremolo_editor(state());
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext&) override {
        const std::size_t channels =
            std::min(output.num_channels(), input.num_channels());
        const std::size_t frames = output.num_samples();

        const float depth =
            std::clamp(state().get_value(kTremDepth), 0.0f, 1.0f);
        const float rate = std::max(0.0f, state().get_value(kRate));
        const TremoloWaveform wf =
            waveform_from_param(state().get_value(kTremWaveform));
        const float increment = rate * inv_sr_;

        // One LFO drives all channels so they modulate in phase. Compute the
        // per-sample gain once, then apply across channels.
        for (std::size_t i = 0; i < frames; ++i) {
            const float m = lfo_unipolar(wf, phase_);   // 0..1
            const float gain = (1.0f - depth) + depth * m;
            for (std::size_t ch = 0; ch < channels; ++ch) {
                output.channel(ch)[i] = input.channel(ch)[i] * gain;
            }
            phase_ += increment;
            if (phase_ >= 1.0f) phase_ -= std::floor(phase_);
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

    static TremoloWaveform waveform_from_param(float value) {
        int i = static_cast<int>(value + 0.5f);
        const int last = static_cast<int>(TremoloWaveform::Count) - 1;
        if (i < 0) i = 0;
        if (i > last) i = last;
        return static_cast<TremoloWaveform>(i);
    }

    // Hand-written unipolar LFO shapers. `phase` is in [0,1); every shape
    // returns a value in [0,1] so the gain law g = (1-d) + d*m stays bounded.
    static float lfo_unipolar(TremoloWaveform wf, float phase) {
        constexpr float kTwoPi = 6.28318530717958647692f;
        switch (wf) {
            case TremoloWaveform::Sine:
                return 0.5f + 0.5f * std::sin(kTwoPi * phase);
            case TremoloWaveform::Triangle:
                // Symmetric: 0 at the edges, 1 at the midpoint.
                return 1.0f - 2.0f * std::fabs(phase - 0.5f);
            case TremoloWaveform::Sawtooth:
                // Rising ramp 0 -> 1 across the cycle.
                return phase;
            case TremoloWaveform::InvSaw:
                // Falling ramp 1 -> 0 across the cycle.
                return 1.0f - phase;
            case TremoloWaveform::Square:
                return phase < 0.5f ? 0.0f : 1.0f;
            case TremoloWaveform::SqSloped: {
                // Trapezoid: a square whose transitions are short linear ramps,
                // which softens the click an ideal square would produce. Ramp
                // width r straddles each edge.
                constexpr float r = 0.02f;
                if (phase < r)              return phase / r;            // 0 -> 1
                if (phase < 0.5f)           return 1.0f;
                if (phase < 0.5f + r)       return 1.0f - (phase - 0.5f) / r; // 1 -> 0
                return 0.0f;
            }
            case TremoloWaveform::Count:
            default:
                return 0.5f;
        }
    }

    float sample_rate_ = 48000.0f;
    float inv_sr_ = 1.0f / 48000.0f;
    float phase_ = 0.0f;
};

inline std::unique_ptr<format::Processor> create_tremolo() {
    return std::make_unique<TremoloProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_tremolo_editor (declared above) so
// the create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. Placed after the class so the
// editor header (which needs TremoloProcessor's param enum) sees a complete
// definition; the header guard makes its own re-include of this file a no-op.
#include "tremolo_editor.hpp"
