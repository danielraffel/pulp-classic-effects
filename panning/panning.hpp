#pragma once

// Panning — a STATIC stereo panner with a selectable panning law, after the
// "Panning" chapter of Reiss & McPherson, *Audio Effects: Theory,
// Implementation and Application*. A mono source is placed at one position in
// the stereo field; the position does NOT move on its own (this is not an
// LFO auto-panner). Two laws are offered:
//
//   • Pan+Pre  — constant-power amplitude panning with a short precedence
//     (Haas) delay on the attenuated side. Suited to loudspeaker playback.
//     Per-side gains follow gL = cos θ, gR = sin θ with θ = pan·π/2, so
//     gL² + gR² = 1 at every position (constant perceived power). The quieter
//     side is delayed up to ~1 ms so the louder side arrives first — the
//     precedence effect that reinforces the amplitude cue.
//
//   • ITD+ILD — a spherical-head binaural model producing an Interaural Time
//     Difference (a fractional per-ear delay, Woodworth's formula) and an
//     Interaural Level Difference (a per-ear first-order head-shadow shelf,
//     Brown–Duda form). Suited to headphone playback.
//
// Clean-room: the DSP below is implemented from the standard textbook
// formulations (constant-power law, Woodworth ITD, Brown–Duda head-shadow
// shelf derived here via the bilinear transform). No third-party effect source
// code was copied. References in the repo README.

#include <pulp/format/processor.hpp>
#include <pulp/view/view.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace pulp::examples::classic {

enum PanningParams : state::ParamID {
    kMethod = 1,  // 0 = Pan+Pre (constant-power + precedence), 1 = ITD+ILD
    kPan    = 2,  // 0 = hard left … 0.5 = centre … 1 = hard right
};

// Defined out-of-line in panning_editor.hpp (included at the bottom).
std::unique_ptr<view::View> build_panning_editor(state::StateStore& store);

// A small fractional-delay ring buffer (linear interpolation). Read taps may
// differ per channel, so a single shared line is fed once per sample and read
// twice. Own implementation.
class FractionalDelay {
public:
    void resize(std::size_t max_samples) {
        buf_.assign(std::max<std::size_t>(max_samples + 2, 2), 0.0f);
        write_ = 0;
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); write_ = 0; }
    void write(float x) {
        buf_[write_] = x;
        if (++write_ >= buf_.size()) write_ = 0;
    }
    // Read `delay` samples in the past (delay >= 0), linearly interpolated.
    float read(float delay) const {
        const float len = static_cast<float>(buf_.size());
        if (delay < 0.0f) delay = 0.0f;
        if (delay > len - 2.0f) delay = len - 2.0f;
        float pos = static_cast<float>(write_) - 1.0f - delay;
        while (pos < 0.0f) pos += len;
        const std::size_t i0 = static_cast<std::size_t>(pos) % buf_.size();
        const std::size_t i1 = (i0 + 1) % buf_.size();
        const float frac = pos - std::floor(pos);
        return buf_[i0] + frac * (buf_[i1] - buf_[i0]);
    }

private:
    std::vector<float> buf_{2, 0.0f};
    std::size_t write_ = 0;
};

// First-order head-shadow shelf, Brown–Duda form. The analogue prototype is
//     H(s) = (1 + α·τ·s) / (1 + τ·s),   τ = head_radius / speed_of_sound,
// a high-shelf whose high-frequency gain equals α and whose DC gain is 1. The
// incidence angle θ (between the source direction and the ear) sets
//     α = 1 + cos θ  ∈ [0, 2]
// — the near ear (θ→0) is bright (α→2), the far/shadowed ear (θ→π) is dull
// (α→0). Discretised here with the bilinear transform (own derivation), which
// preserves the unity DC gain so a centred source stays level-balanced.
class HeadShadowShelf {
public:
    void reset() { x1_ = y1_ = 0.0f; }

    void set(float incidence_rad, float sample_rate, float head_radius_over_c) {
        const float alpha = 1.0f + std::cos(incidence_rad);
        const float K = 2.0f * sample_rate * head_radius_over_c;  // 2·sr·τ
        const float inv = 1.0f / (1.0f + K);
        b0_ = (1.0f + alpha * K) * inv;
        b1_ = (1.0f - alpha * K) * inv;
        a1_ = (1.0f - K) * inv;
    }

    float process(float x) {
        const float y = b0_ * x + b1_ * x1_ - a1_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    float b0_ = 1.0f, b1_ = 0.0f, a1_ = 0.0f;
    float x1_ = 0.0f, y1_ = 0.0f;
};

class PanningProcessor : public format::Processor {
public:
    std::unique_ptr<view::View> create_view() override { return build_panning_editor(state()); }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Panning", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.panning", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        // Two-option method selector (stepped 0..1). Default 1 = ITD+ILD.
        store.add_parameter({.id = kMethod, .name = "Method", .unit = "",
                             .range = {0.0f, 1.0f, 1.0f, 1.0f}});
        // Position: 0 = hard left, 0.5 = centre, 1 = hard right.
        store.add_parameter({.id = kPan, .name = "Pan", .unit = "",
                             .range = {0.0f, 1.0f, 0.5f, 0.0f}});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        // Max delay covers both the Haas precedence tap (~1 ms) and the
        // Woodworth ITD (~0.65 ms at full pan). 2 ms is comfortably enough.
        max_delay_samples_ = static_cast<std::size_t>(0.002f * sample_rate_) + 4;
        delay_.resize(max_delay_samples_);
        shelf_l_.reset();
        shelf_r_.reset();
        pan_initialized_ = false;
        last_method_ = -1;
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t in_ch = input.num_channels();
        const std::size_t out_ch = output.num_channels();
        const std::size_t frames = output.num_samples();

        if (ctx.should_reset_dsp_state()) {
            delay_.reset();
            shelf_l_.reset();
            shelf_r_.reset();
            pan_initialized_ = false;
        }

        // Panning needs a stereo output bus; pass through otherwise.
        if (out_ch < 2) {
            for (std::size_t ch = 0; ch < std::min(in_ch, out_ch); ++ch) {
                auto in = input.channel(ch);
                auto out = output.channel(ch);
                for (std::size_t i = 0; i < frames; ++i) out[i] = in[i];
            }
            clear_extra(output, std::min(in_ch, out_ch));
            return;
        }

        const int method = static_cast<int>(state().get_value(kMethod) + 0.5f);
        if (method != last_method_) {  // avoid stale IIR state across laws
            shelf_l_.reset();
            shelf_r_.reset();
            last_method_ = method;
        }

        const float target = std::clamp(state().get_value(kPan), 0.0f, 1.0f);
        if (!pan_initialized_) { pan_ = target; pan_initialized_ = true; }

        // One-pole position smoother (~5 ms) to avoid zipper noise on automation.
        const float smooth = std::exp(-1.0f / (0.005f * sample_rate_));

        constexpr float kHalfPi   = 1.57079632679489662f;  // π/2
        constexpr float kPi       = 3.14159265358979324f;
        constexpr float kHeadR    = 0.085f;                 // head radius (m)
        constexpr float kSoundC   = 340.0f;                 // speed of sound (m/s)
        const float head_tau      = kHeadR / kSoundC;       // τ for the shelf
        const float head_samples  = sample_rate_ * head_tau;// ITD scale (samples)
        const float haas_max      = 0.001f * sample_rate_;  // 1 ms precedence

        const float* il = input.channel(0).data();
        const float* ir = (in_ch >= 2) ? input.channel(1).data() : il;
        float* ol = output.channel(0).data();
        float* orr = output.channel(1).data();

        for (std::size_t i = 0; i < frames; ++i) {
            pan_ = target + (pan_ - target) * smooth;  // smoothed position [0,1]
            const float mono = 0.5f * (il[i] + ir[i]);
            delay_.write(mono);

            if (method == 0) {
                // Constant-power pan: θ ∈ [0, π/2]. gL²+gR² = 1 for all pan.
                const float theta = pan_ * kHalfPi;
                const float gL = std::cos(theta);
                const float gR = std::sin(theta);
                // Precedence: delay the attenuated side so the louder side
                // leads. pan=0 (left) → delay the right; pan=1 → delay the left.
                const float dL = haas_max * pan_;
                const float dR = haas_max * (1.0f - pan_);
                ol[i]  = delay_.read(dL) * gL;
                orr[i] = delay_.read(dR) * gR;
            } else {
                // ITD+ILD spherical head. Azimuth φ ∈ [-π/2, +π/2]:
                // pan=0 → -π/2 (hard left), 0.5 → 0 (front), 1 → +π/2 (right).
                const float phi = (pan_ - 0.5f) * kPi;
                // Per-ear incidence vs. the ear's outward normal (±π/2).
                const float inc_l = phi + kHalfPi;  // 0 when source hard-left
                const float inc_r = phi - kHalfPi;  // 0 when source hard-right
                const float dL = woodworth_delay(inc_l, head_samples);
                const float dR = woodworth_delay(inc_r, head_samples);
                shelf_l_.set(inc_l, sample_rate_, head_tau);
                shelf_r_.set(inc_r, sample_rate_, head_tau);
                ol[i]  = shelf_l_.process(delay_.read(dL));
                orr[i] = shelf_r_.process(delay_.read(dR));
            }
        }
        clear_extra(output, 2);
    }

private:
    // Woodworth spherical-head time-of-arrival for one ear, in samples, as a
    // function of the incidence angle between source and the ear's normal.
    // Near the ear (|angle| < π/2) the surface path shortens with cos; on the
    // shadowed side it grows linearly with the wrap-around arc.
    static float woodworth_delay(float angle, float head_samples) {
        constexpr float kHalfPi = 1.57079632679489662f;
        const float a = std::fabs(angle);
        if (a < kHalfPi) return head_samples * (1.0f - std::cos(angle));
        return head_samples * (a + 1.0f - kHalfPi);
    }

    static void clear_extra(audio::BufferView<float>& out, std::size_t written) {
        for (std::size_t ch = written; ch < out.num_channels(); ++ch) {
            auto o = out.channel(ch);
            for (std::size_t i = 0; i < out.num_samples(); ++i) o[i] = 0.0f;
        }
    }

    float sample_rate_ = 48000.0f;
    std::size_t max_delay_samples_ = 1;
    FractionalDelay delay_;
    HeadShadowShelf shelf_l_, shelf_r_;
    float pan_ = 0.5f;
    bool pan_initialized_ = false;
    int last_method_ = -1;
};

inline std::unique_ptr<format::Processor> create_panning() {
    return std::make_unique<PanningProcessor>();
}

} // namespace pulp::examples::classic

#include "panning_editor.hpp"
