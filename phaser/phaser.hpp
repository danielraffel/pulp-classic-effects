#pragma once

// Phaser — a cascade of first-order all-pass filters whose corner frequency is
// swept by an LFO, summed with the dry signal.
//
// Clean-room textbook phaser. Each first-order all-pass imposes a
// frequency-dependent phase shift but a flat magnitude response; cascading N of
// them and adding the result to the dry signal produces a comb of moving
// notches wherever the cascade reaches odd multiples of pi. An LFO sweeps the
// shared corner frequency between Min Freq and Min Freq + Sweep Width, so the
// notches glide. A feedback path around the cascade resonates the notches,
// sharpening them. The all-pass is realised with the bilinear-transform
// first-order form
//
//     c = (tan(wc/2) - 1) / (tan(wc/2) + 1),   wc = 2*pi*fc/fs
//     y[n] = c*x[n] + x[n-1] - c*y[n-1]
//
// which is the standard one-pole/one-zero all-pass; |H| = 1 at every frequency
// and the phase passes through -pi/2 at fc. The DSP here is written from that
// textbook definition; no third-party effect source was transcribed. See the
// README for the algorithmic reference.

#include <pulp/format/processor.hpp>
// Headless WASM DSP builds curate out core/view (canvas/Skia/text-shaping),
// so every editor reference below is gated on PULP_HEADLESS.
#if !PULP_HEADLESS
#include <pulp/view/view.hpp>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pulp::examples::classic {

enum PhaserParams : state::ParamID {
    kPhaserDepth    = 1,  // 0..1 wet blend
    kPhaserFeedback = 2,  // 0..0.9 resonance
    kPhaserStages   = 3,  // combo index 0..4 -> 2/4/6/8/10 all-pass stages
    kPhaserMinFreq  = 4,  // 50..1000 Hz sweep floor
    kPhaserSweep    = 5,  // 50..3000 Hz sweep span above the floor
    kPhaserRate     = 6,  // 0..2 Hz LFO rate
    kPhaserWaveform = 7,  // combo index 0..3 -> sine/triangle/square/sawtooth
    kPhaserStereo   = 8,  // toggle: quarter-cycle LFO offset on odd channels
};

// Defined out-of-line in phaser_editor.hpp (included at the bottom).
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
std::unique_ptr<view::View> build_phaser_editor(state::StateStore& store);
#endif

class PhaserProcessor : public format::Processor {
public:
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
    std::unique_ptr<view::View> create_view() override { return build_phaser_editor(state()); }
#endif

    // Maximum cascade depth (the largest Stages option). Per-channel state is
    // sized to this; only the first `stage_count()` entries are processed.
    static constexpr int kMaxStages = 10;
    static constexpr int kMaxChannels = 8;

    format::PluginDescriptor descriptor() const override {
        return {.name = "Phaser", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.phaser", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        // Ranges, defaults and units mirror the textbook/JUCE phaser controls.
        store.add_parameter({.id = kPhaserDepth, .name = "Depth", .unit = "",
                             .range = {0.0f, 1.0f, 1.0f, 0.0f}});
        store.add_parameter({.id = kPhaserFeedback, .name = "Feedback", .unit = "",
                             .range = {0.0f, 0.9f, 0.7f, 0.0f}});
        // Combo index 0..4; stage_count() maps it to 2/4/6/8/10. Default 1 -> 4.
        store.add_parameter({.id = kPhaserStages, .name = "Stages", .unit = "",
                             .range = {0.0f, 4.0f, 1.0f, 1.0f}});
        store.add_parameter({.id = kPhaserMinFreq, .name = "Min Freq", .unit = "Hz",
                             .range = {50.0f, 1000.0f, 80.0f, 0.0f}});
        store.add_parameter({.id = kPhaserSweep, .name = "Sweep Width", .unit = "Hz",
                             .range = {50.0f, 3000.0f, 1000.0f, 0.0f}});
        store.add_parameter({.id = kPhaserRate, .name = "LFO Rate", .unit = "Hz",
                             .range = {0.0f, 2.0f, 0.05f, 0.0f}});
        store.add_parameter({.id = kPhaserWaveform, .name = "Waveform", .unit = "",
                             .range = {0.0f, 3.0f, 0.0f, 1.0f}});
        store.add_parameter({.id = kPhaserStereo, .name = "Stereo", .unit = "",
                             .range = {0.0f, 1.0f, 1.0f, 1.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        inv_sr_ = 1.0f / sample_rate_;
        reset_state();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t channels =
            std::min({output.num_channels(), input.num_channels(),
                      static_cast<std::size_t>(kMaxChannels)});
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) reset_state();

        const float depth = std::clamp(state().get_value(kPhaserDepth), 0.0f, 1.0f);
        const float fb = std::clamp(state().get_value(kPhaserFeedback), 0.0f, 0.9f);
        const int stages = stage_count(state().get_value(kPhaserStages));
        const float min_f = state().get_value(kPhaserMinFreq);
        const float sweep = state().get_value(kPhaserSweep);
        const float rate = state().get_value(kPhaserRate);
        const int waveform = waveform_index(state().get_value(kPhaserWaveform));
        const bool stereo = state().get_value(kPhaserStereo) >= 0.5f;

        const float phase_inc = rate * inv_sr_;

        for (std::size_t i = 0; i < frames; ++i) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                // Odd channels run the LFO a quarter cycle ahead when stereo is
                // on, so the two outputs' notches sit at different frequencies.
                float ph = phase_;
                if (stereo && (ch & 1u)) { ph += 0.25f; if (ph >= 1.0f) ph -= 1.0f; }

                const float m = lfo_unipolar(ph, waveform);     // 0..1
                const float centre = min_f + sweep * m;          // Hz
                float wc = 2.0f * kPi * centre * inv_sr_;         // rad/sample
                wc = std::min(wc, kPi * 0.99f);
                const float t = std::tan(0.5f * wc);
                const float c = (t - 1.0f) / (t + 1.0f);          // all-pass coeff

                auto& z = ap_[ch];
                const float dry = input.channel(ch)[i];
                float x = dry + fb * fb_[ch];
                for (int s = 0; s < stages; ++s) {
                    const float xn = x;
                    const float yn = c * xn + z[s].x1 - c * z[s].y1;
                    z[s].x1 = xn;
                    z[s].y1 = yn;
                    x = yn;
                }
                if (std::fabs(x) < 1e-30f) x = 0.0f;  // flush feedback denormals
                fb_[ch] = x;
                // Sum dry with the phase-shifted copy; *0.5 keeps unity-ish level.
                output.channel(ch)[i] = dry + depth * (x - dry) * 0.5f;
            }
            phase_ += phase_inc;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
        }
        clear_extra(output, channels);
    }

private:
    struct AllpassState { float x1 = 0.0f, y1 = 0.0f; };

    static constexpr float kPi = 3.14159265358979323846f;

    // Combo index 0..4 -> 2/4/6/8/10 cascaded all-pass stages.
    static int stage_count(float index_value) {
        const int idx = std::clamp(static_cast<int>(std::lround(index_value)), 0, 4);
        return 2 + 2 * idx;
    }
    static int waveform_index(float value) {
        return std::clamp(static_cast<int>(std::lround(value)), 0, 3);
    }
    // Unipolar 0..1 LFO. 0 sine, 1 triangle, 2 square, 3 sawtooth.
    static float lfo_unipolar(float phase, int waveform) {
        switch (waveform) {
            case 1:  // triangle
                if (phase < 0.25f) return 0.5f + 2.0f * phase;
                if (phase < 0.75f) return 1.0f - 2.0f * (phase - 0.25f);
                return 2.0f * (phase - 0.75f);
            case 2:  // square
                return phase < 0.5f ? 1.0f : 0.0f;
            case 3:  // sawtooth (continuous 0..1 ramp, wrapped)
                return phase < 0.5f ? 0.5f + phase : phase - 0.5f;
            case 0:  // sine
            default:
                return 0.5f + 0.5f * std::sin(2.0f * kPi * phase);
        }
    }

    void reset_state() {
        for (auto& chan : ap_)
            for (auto& s : chan) s = AllpassState{};
        fb_.fill(0.0f);
        phase_ = 0.0f;
    }
    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }

    float sample_rate_ = 48000.0f;
    float inv_sr_ = 1.0f / 48000.0f;
    float phase_ = 0.0f;
    std::array<std::array<AllpassState, kMaxStages>, kMaxChannels> ap_{};
    std::array<float, kMaxChannels> fb_{};
};

inline std::unique_ptr<format::Processor> create_phaser() {
    return std::make_unique<PhaserProcessor>();
}

} // namespace pulp::examples::classic

// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
#include "phaser_editor.hpp"
#endif
