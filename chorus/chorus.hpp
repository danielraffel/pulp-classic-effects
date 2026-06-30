#pragma once

// Chorus — several LFO-swept fractional-delay voices summed with the dry signal.
//
// Clean-room textbook chorus. A chorus thickens a mono source into an ensemble
// by mixing in several copies that are each delayed by a short, slowly
// time-varying amount. Because every voice's delay wanders on its own
// low-frequency oscillator (with a different phase offset), the copies drift in
// and out of tune relative to the dry signal and to each other — the classic
// "many instruments" shimmer.
//
// Signal flow per channel:
//   * A single delay line stores the recent input history.
//   * For each wet voice v, the read position is
//         read = write - (Delay + Width * lfo_v) * sample_rate
//     where lfo_v is a 0..1 LFO sampled at the shared phase plus a per-voice
//     phase offset, so the voices spread evenly around the LFO cycle.
//   * The wet taps (scaled by Depth) are added on top of the dry input.
//   * With Stereo on, the per-voice weights are mirrored between the two
//     channels (and the 2-voice case routes pure dry to the left / pure wet to
//     the right), decorrelating L and R for a wide image.
//
// Discrete controls (Voices, Waveform, Interp) are exposed as zero-based index
// parameters, matching the combo/stepper convention used by the other example
// editors: the stored value is the option index, decoded back to its meaning
// here. Built only on the C++ standard library; no third-party effect source
// was read.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace pulp::examples::classic {

// Longest delay the line must hold: max Delay (0.05 s) + max Width (0.05 s).
inline constexpr float kChorusMaxDelaySecs = 0.1f;
inline constexpr int   kChorusMinVoices    = 2;
inline constexpr int   kChorusMaxVoices    = 5;

enum ChorusParams : state::ParamID {
    kChorusDelay    = 1,  // 0.01..0.05 s  base delay of every voice
    kChorusWidth    = 2,  // 0.01..0.05 s  peak LFO excursion added to the delay
    kChorusDepth    = 3,  // 0..1          wet level of the summed voices
    kChorusVoices   = 4,  // index 0..3 -> 2..5 total voices (1 dry + N-1 wet)
    kChorusRate     = 5,  // 0.05..2 Hz    LFO frequency
    kChorusWaveform = 6,  // index 0..3    Sine / Triangle / Saw / Inv. Saw
    kChorusInterp   = 7,  // index 0..2    Nearest / Linear / Cubic
    kChorusStereo   = 8,  // 0/1           stereo voice spreading
};

// Waveform option indices (kChorusWaveform).
enum class ChorusWaveform { sine = 0, triangle = 1, sawtooth = 2, inverse_sawtooth = 3 };
// Interpolation option indices (kChorusInterp).
enum class ChorusInterp { nearest = 0, linear = 1, cubic = 2 };

// Defined out-of-line in chorus_editor.hpp (included at the bottom of this file).
// Forward-declared so create_view() hands the host the same dark Ink & Signal
// editor the screenshot tests render.
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
        store.add_parameter({.id = kChorusDelay, .name = "Delay", .unit = "s",
                             .range = {0.01f, 0.05f, 0.03f, 0.0f}});
        store.add_parameter({.id = kChorusWidth, .name = "Width", .unit = "s",
                             .range = {0.01f, 0.05f, 0.02f, 0.0f}});
        store.add_parameter({.id = kChorusDepth, .name = "Depth", .unit = "",
                             .range = {0.0f, 1.0f, 1.0f, 0.0f}});
        // Discrete voice count, exposed as a 0-based stepper index (0 -> 2 voices
        // … 3 -> 5 voices) so the shared index-based stepper widget binds cleanly.
        store.add_parameter({.id = kChorusVoices, .name = "Voices", .unit = "",
                             .range = {0.0f, 3.0f, 0.0f, 1.0f}});
        store.add_parameter({.id = kChorusRate, .name = "Rate", .unit = "Hz",
                             .range = {0.05f, 2.0f, 0.2f, 0.0f}});
        store.add_parameter({.id = kChorusWaveform, .name = "Waveform", .unit = "",
                             .range = {0.0f, 3.0f, 0.0f, 1.0f}});  // default Sine
        store.add_parameter({.id = kChorusInterp, .name = "Interp", .unit = "",
                             .range = {0.0f, 2.0f, 1.0f, 1.0f}});  // default Linear
        store.add_parameter({.id = kChorusStereo, .name = "Stereo", .unit = "",
                             .range = {0.0f, 1.0f, 1.0f, 1.0f}});  // default on
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        buf_len_ = std::max(4, static_cast<int>(kChorusMaxDelaySecs * sample_rate_) + 1);
        for (auto& line : lines_) {
            line.assign(static_cast<std::size_t>(buf_len_), 0.0f);
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

        if (ctx.should_reset_dsp_state()) {
            for (auto& line : lines_) std::fill(line.begin(), line.end(), 0.0f);
            write_pos_ = 0;
            lfo_phase_ = 0.0f;
        }

        // Snapshot the controls for this block.
        const float delay_s = std::clamp(state().get_value(kChorusDelay), 0.01f, 0.05f);
        const float width_s = std::clamp(state().get_value(kChorusWidth), 0.0f, 0.05f);
        const float depth   = std::clamp(state().get_value(kChorusDepth), 0.0f, 1.0f);
        const int   voices  = decode_voices(state().get_value(kChorusVoices));
        const float rate    = std::clamp(state().get_value(kChorusRate), 0.05f, 2.0f);
        const auto  wave    = decode_waveform(state().get_value(kChorusWaveform));
        const auto  interp  = decode_interp(state().get_value(kChorusInterp));
        const bool  stereo  = state().get_value(kChorusStereo) >= 0.5f;

        const int   num_wet  = voices - 1;                 // voice 1 is the dry signal
        const float buf_len_f = static_cast<float>(buf_len_);
        const float phase_inc = rate / sample_rate_;

        std::array<float, kChorusMaxVoices - 1> read_pos{};

        for (std::size_t i = 0; i < frames; ++i) {
            // Read positions are shared across channels (they depend only on the
            // LFO, not the channel), so compute them once per sample.
            const int ref = write_pos_;
            float phase_offset = 0.0f;
            for (int v = 0; v < num_wet; ++v) {
                const float lfo_val = lfo(lfo_phase_ + phase_offset, wave);          // 0..1
                float local_delay = (delay_s + width_s * lfo_val) * sample_rate_;     // samples
                local_delay = std::clamp(local_delay, 0.0f, buf_len_f - 2.0f);
                float pos = static_cast<float>(ref) - local_delay;
                pos = std::fmod(pos, buf_len_f);
                if (pos < 0.0f) pos += buf_len_f;
                read_pos[static_cast<std::size_t>(v)] = pos;

                // Spread the wet voices evenly around the LFO cycle.
                if (voices == 3) {
                    phase_offset += 0.25f;
                } else if (voices > 3) {
                    phase_offset += 1.0f / static_cast<float>(voices - 1);
                }
            }

            for (std::size_t ch = 0; ch < channels; ++ch) {
                const float dry = input.channel(ch)[i];
                float acc = dry;
                auto& line = lines_[ch];

                for (int v = 0; v < num_wet; ++v) {
                    const float voiced =
                        sample_at(line, read_pos[static_cast<std::size_t>(v)], interp);

                    if (stereo && voices == 2) {
                        // Two-voice stereo: left is pure dry, right is pure wet —
                        // the strongest possible decorrelation for a single tap.
                        acc = (ch == 0) ? dry : voiced * depth;
                    } else {
                        float weight = 1.0f;
                        if (stereo && voices > 2) {
                            // Mirror voice weights between channels so each side
                            // emphasises a different end of the voice spread.
                            weight = static_cast<float>(v) / static_cast<float>(voices - 2);
                            if (ch != 0) weight = 1.0f - weight;
                        }
                        acc += voiced * depth * weight;
                    }
                }

                output.channel(ch)[i] = acc;
                line[static_cast<std::size_t>(ref)] = dry;  // overwrite the oldest slot
            }

            // Advance the shared write head and LFO once per sample.
            if (++write_pos_ >= buf_len_) write_pos_ = 0;
            lfo_phase_ += phase_inc;
            if (lfo_phase_ >= 1.0f) lfo_phase_ -= 1.0f;
        }

        clear_extra(output, channels);
    }

private:
    static int decode_voices(float v) {
        return std::clamp(static_cast<int>(std::lround(v)) + kChorusMinVoices,
                          kChorusMinVoices, kChorusMaxVoices);
    }
    static ChorusWaveform decode_waveform(float v) {
        return static_cast<ChorusWaveform>(std::clamp(static_cast<int>(std::lround(v)), 0, 3));
    }
    static ChorusInterp decode_interp(float v) {
        return static_cast<ChorusInterp>(std::clamp(static_cast<int>(std::lround(v)), 0, 2));
    }

    // Unipolar (0..1) low-frequency oscillator. Textbook shapes, derived here.
    static float lfo(float phase, ChorusWaveform wave) {
        phase -= std::floor(phase);  // wrap to [0,1)
        switch (wave) {
            case ChorusWaveform::triangle: {
                const float t = 2.0f * phase;          // 0..2
                return t < 1.0f ? t : 2.0f - t;        // up to 1 at .5, back to 0
            }
            case ChorusWaveform::sawtooth:
                return phase;                          // rising ramp 0..1
            case ChorusWaveform::inverse_sawtooth:
                return 1.0f - phase;                   // falling ramp 1..0
            case ChorusWaveform::sine:
            default:
                return 0.5f + 0.5f * std::sin(2.0f * 3.14159265358979323846f * phase);
        }
    }

    // Read a fractional position out of the ring buffer with the chosen
    // interpolation. `pos` is an absolute (already-wrapped) buffer index.
    static float sample_at(const std::vector<float>& line, float pos, ChorusInterp interp) {
        const std::size_t n = line.size();
        const std::size_t i0 = static_cast<std::size_t>(pos) % n;
        switch (interp) {
            case ChorusInterp::nearest:
                return line[i0];
            case ChorusInterp::cubic: {
                const float frac = pos - std::floor(pos);
                const float s0 = line[(i0 + n - 1) % n];
                const float s1 = line[i0];
                const float s2 = line[(i0 + 1) % n];
                const float s3 = line[(i0 + 2) % n];
                // Catmull-Rom spline through s1..s2.
                const float a0 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
                const float a1 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
                const float a2 = -0.5f * s0 + 0.5f * s2;
                return ((a0 * frac + a1) * frac + a2) * frac + s1;
            }
            case ChorusInterp::linear:
            default: {
                const float frac = pos - std::floor(pos);
                const float s0 = line[i0];
                const float s1 = line[(i0 + 1) % n];
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
    int   buf_len_ = 4801;
    int   write_pos_ = 0;
    float lfo_phase_ = 0.0f;
    std::array<std::vector<float>, 8> lines_{};
};

inline std::unique_ptr<format::Processor> create_chorus() {
    return std::make_unique<ChorusProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_chorus_editor (declared above) so the
// create_view() override links in every TU that uses the processor — the plugin
// adapter and the headless tests alike. After the class so the editor header
// sees a complete definition; its re-include of this file is a no-op.
#include "chorus_editor.hpp"
