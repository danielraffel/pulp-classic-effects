#pragma once

// Vibrato — pitch modulation via an LFO-swept fractional delay.
//
// Clean-room textbook vibrato: a low-frequency oscillator sweeps the read
// position of a short fractional delay line, so the output pitch wavers. 100%
// wet (the effect *is* the modulated signal). The LFO is unipolar (0..1) so the
// instantaneous delay sweeps between 0 and `Width` seconds; "Waveform" picks the
// LFO shape and "Interp" picks how the fractional read is reconstructed. The
// circular buffer, LFO shaping, and the Nearest/Linear/Cubic interpolators are
// all written from scratch against Pulp primitives; no third-party effect source
// was transcribed.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace pulp::examples::classic {

enum VibratoParams : state::ParamID {
    kVibWidthSecs = 1,  // 0.001..0.05 s — peak LFO-swept delay
    kVibRateHz    = 2,  // 0..10 Hz — LFO rate
    kVibWaveform  = 3,  // 0 sine, 1 triangle, 2 sawtooth, 3 inverse sawtooth
    kVibInterp    = 4,  // 0 nearest, 1 linear, 2 cubic
};

// LFO shapes, matching the editor's Waveform combo order.
enum class VibratoWave : int { Sine = 0, Triangle = 1, Sawtooth = 2, InverseSawtooth = 3 };
// Fractional-delay reconstruction modes, matching the Interp combo order.
enum class VibratoInterp : int { Nearest = 0, Linear = 1, Cubic = 2 };

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
        store.add_parameter({.id = kVibWidthSecs, .name = "Width", .unit = "s",
                             .range = {0.001f, 0.05f, 0.01f, 0.0f}});
        store.add_parameter({.id = kVibRateHz, .name = "Rate", .unit = "Hz",
                             .range = {0.0f, 10.0f, 2.0f, 0.0f}});
        store.add_parameter({.id = kVibWaveform, .name = "Waveform", .unit = "",
                             .range = {0.0f, 3.0f, 0.0f, 1.0f}});   // stepped enum
        store.add_parameter({.id = kVibInterp, .name = "Interp", .unit = "",
                             .range = {0.0f, 2.0f, 1.0f, 1.0f}});   // stepped enum, default Linear
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        // Room for the widest sweep (0.05 s) plus the cubic interpolator's
        // four-tap neighbourhood (one sample behind the oldest read, two ahead).
        buffer_len_ = static_cast<int>(std::ceil(0.05f * sample_rate_)) + 4;
        if (buffer_len_ < 4) buffer_len_ = 4;
        for (auto& line : lines_) {
            line.assign(static_cast<std::size_t>(buffer_len_), 0.0f);
        }
        write_pos_ = 0;
        lfo_phase_ = 0.0f;
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(), lines_.size()});
        const std::size_t frames = output.num_samples();

        // Clear stale delay history on a transport seek/loop.
        if (ctx.should_reset_dsp_state()) {
            for (auto& line : lines_)
                std::fill(line.begin(), line.end(), 0.0f);
            write_pos_ = 0;
            lfo_phase_ = 0.0f;
        }

        const float width_secs = std::clamp(state().get_value(kVibWidthSecs), 0.0f, 0.05f);
        const float rate_hz    = std::max(0.0f, state().get_value(kVibRateHz));
        const auto  wave       = static_cast<VibratoWave>(
            std::clamp(static_cast<int>(state().get_value(kVibWaveform) + 0.5f), 0, 3));
        const auto  interp     = static_cast<VibratoInterp>(
            std::clamp(static_cast<int>(state().get_value(kVibInterp) + 0.5f), 0, 2));

        // Peak delay in samples, capped so the read stays inside the buffer.
        float max_delay = width_secs * sample_rate_;
        max_delay = std::clamp(max_delay, 0.0f, static_cast<float>(buffer_len_ - 3));
        const float phase_inc = rate_hz / sample_rate_;

        // One LFO and one write cursor drive every channel in phase. Write the
        // freshest input first, then read back at the swept (fractional) delay.
        for (std::size_t i = 0; i < frames; ++i) {
            const float m = lfo_unipolar(lfo_phase_, wave);   // 0..1
            const float delay = max_delay * m;                 // 0..max_delay samples
            // Read position measured backwards from the write cursor.
            float read_pos = static_cast<float>(write_pos_) - delay;
            while (read_pos < 0.0f) read_pos += static_cast<float>(buffer_len_);

            for (std::size_t ch = 0; ch < channels; ++ch) {
                auto& line = lines_[ch];
                line[static_cast<std::size_t>(write_pos_)] = input.channel(ch)[i];
                output.channel(ch)[i] = read_interp(line, read_pos, interp);
            }

            if (++write_pos_ >= buffer_len_) write_pos_ = 0;
            lfo_phase_ += phase_inc;
            while (lfo_phase_ >= 1.0f) lfo_phase_ -= 1.0f;
        }
        clear_extra(output, channels);
    }

private:
    // Unipolar (0..1) LFO. Phase is in [0,1). Each shape is written directly
    // rather than derived from a shared oscillator so the four options stay
    // independent and exact at the cycle endpoints.
    static float lfo_unipolar(float phase, VibratoWave wave) {
        switch (wave) {
            case VibratoWave::Triangle:
                // 0 -> 1 over the first half, 1 -> 0 over the second.
                return phase < 0.5f ? 2.0f * phase : 2.0f * (1.0f - phase);
            case VibratoWave::Sawtooth:
                // Rising ramp 0 -> 1.
                return phase;
            case VibratoWave::InverseSawtooth:
                // Falling ramp 1 -> 0.
                return 1.0f - phase;
            case VibratoWave::Sine:
            default:
                return 0.5f + 0.5f * std::sin(2.0f * 3.14159265358979323846f * phase);
        }
    }

    // Read `line` at fractional position `pos` (already wrapped to [0,len)) using
    // the selected reconstruction. Cubic is a 4-point Catmull-Rom spline.
    static float read_interp(const std::vector<float>& line, float pos, VibratoInterp interp) {
        const int len = static_cast<int>(line.size());
        const int i0 = static_cast<int>(std::floor(pos));
        const float frac = pos - static_cast<float>(i0);
        auto at = [&](int idx) -> float {
            idx %= len;
            if (idx < 0) idx += len;
            return line[static_cast<std::size_t>(idx)];
        };
        switch (interp) {
            case VibratoInterp::Nearest:
                return at(i0);
            case VibratoInterp::Cubic: {
                const float s0 = at(i0 - 1), s1 = at(i0), s2 = at(i0 + 1), s3 = at(i0 + 2);
                const float a0 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
                const float a1 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
                const float a2 = -0.5f * s0 + 0.5f * s2;
                const float f2 = frac * frac, f3 = f2 * frac;
                return a0 * f3 + a1 * f2 + a2 * frac + s1;
            }
            case VibratoInterp::Linear:
            default: {
                const float s0 = at(i0), s1 = at(i0 + 1);
                return s0 + frac * (s1 - s0);
            }
        }
    }

    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }

    float sample_rate_ = 48000.0f;
    int buffer_len_ = 2404;
    int write_pos_ = 0;
    float lfo_phase_ = 0.0f;
    std::array<std::vector<float>, 8> lines_{};
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
