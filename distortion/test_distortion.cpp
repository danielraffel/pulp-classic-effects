#include <catch2/catch_test_macros.hpp>
#include "distortion.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_distortion;
using pulp::examples::classic::kDistType;
using pulp::examples::classic::kDistInGain;
using pulp::examples::classic::kDistOutGain;
using pulp::examples::classic::kDistTone;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSr = 48000.0f;

// Type combo indices (match truce's DistortionType enum order).
constexpr float kHardClip = 0.0f;
constexpr float kSoftClip = 1.0f;
constexpr float kExponential = 2.0f;
constexpr float kFullRect = 3.0f;
constexpr float kHalfRect = 4.0f;

std::vector<float> render(format::HeadlessHost& h, const std::vector<float>& mono) {
    const int frames = (int)mono.size();
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) { in.channel(0)[n] = mono[n]; in.channel(1)[n] = mono[n]; }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});
    std::vector<float> r(frames);
    for (int n = 0; n < frames; ++n) r[n] = out.channel(0)[n];
    return r;
}
std::vector<float> sine(float hz, int n, float amp, float sr = kSr) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
double rms(const std::vector<float>& x, int from = 0) {
    double e = 0.0; for (int n = from; n < (int)x.size(); ++n) e += (double)x[n] * x[n];
    return std::sqrt(e / std::max(1, (int)x.size() - from));
}
double peak(const std::vector<float>& x) {
    double p = 0.0; for (float v : x) p = std::max(p, (double)std::fabs(v)); return p;
}
double sum_abs_diff(const std::vector<float>& a, const std::vector<float>& b, int from) {
    double d = 0.0; for (int n = from; n < (int)a.size(); ++n) d += std::fabs(a[n] - b[n]); return d;
}
// Render `input` through a fresh host with the given Type and explicit gains in
// dB. Tone defaults to 0 dB (flat / unity shelf) so the shaper is isolated.
std::vector<float> run(float type, float in_db, float out_db,
                       const std::vector<float>& input, float tone_db = 0.0f) {
    format::HeadlessHost h(create_distortion);
    h.prepare(kSr, 4096);
    h.state().set_value(kDistType, type);
    h.state().set_value(kDistInGain, in_db);
    h.state().set_value(kDistOutGain, out_db);
    h.state().set_value(kDistTone, tone_db);
    return render(h, input);
}
}

TEST_CASE("Distortion parameters round-trip", "[distortion]") {
    format::HeadlessHost h(create_distortion);
    h.prepare(kSr, 4096);

    // Continuous gain params sample the full range.
    for (state::ParamID id : {kDistInGain, kDistOutGain, kDistTone}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    // Discrete Type combo round-trips at every index.
    for (float idx : {kHardClip, kSoftClip, kExponential, kFullRect, kHalfRect}) {
        h.state().set_value(kDistType, idx);
        REQUIRE(std::lround(h.state().get_value(kDistType)) == std::lround(idx));
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Distortion default Type is Full Rect (index 3)", "[distortion]") {
    // The Type default must select the full-wave rectifier (|x|, per truce's
    // default = 3), whose output is non-negative everywhere. Type is left at its
    // default; tone is flattened so the shelf's ringing doesn't dip it negative.
    format::HeadlessHost h(create_distortion);
    h.prepare(kSr, 2048);
    REQUIRE(std::lround(h.state().get_value(kDistType)) == 3);
    h.state().set_value(kDistTone, 0.0f);   // flat shelf isolates the shaper
    auto out = render(h, sine(220.0f, 2048, 0.6f));
    REQUIRE(v::check_finite(out));
    for (float v : out) REQUIRE(v >= -1e-6f);   // full-wave: no negative output
    REQUIRE(peak(out) > 0.0);                    // ...but not silent
}

TEST_CASE("Distortion Exponential is a bounded sign-preserving saturator", "[distortion]") {
    // sgn(x)·(1 - e^-|x|): output keeps the input's sign, stays within ±1, and a
    // hotter drive saturates closer to the ±1 asymptote (more energy) than a
    // quiet one. Distinct from the clip/rect shapes.
    auto input = sine(330.0f, 4096, 0.5f);
    auto quiet = run(kExponential, -6.0f, 0.0f, input);
    auto loud  = run(kExponential, 24.0f, 0.0f, input);
    REQUIRE(v::check_finite(loud));
    REQUIRE(peak(loud) <= 1.0 + 1e-3);              // bounded to the ±1 asymptote
    REQUIRE(rms(loud, 256) > rms(quiet, 256) * 2.0); // drive saturates harder
    // Sign-preserving: at unity gain, output sign tracks input sign.
    auto unity = run(kExponential, 0.0f, 0.0f, input);
    for (int n = 256; n < (int)unity.size(); ++n)
        if (std::fabs(input[n]) > 0.05f)
            REQUIRE((unity[n] >= 0.0f) == (input[n] >= 0.0f));
    // Different transfer function from hard clip.
    auto hard = run(kHardClip, 0.0f, 0.0f, input);
    REQUIRE(sum_abs_diff(unity, hard, 256) > 1.0);
}

TEST_CASE("Distortion Full Rect outputs the absolute value", "[distortion]") {
    // Unity in/out/tone: output must equal |input| (full-wave rectifier), so it
    // is non-negative everywhere and matches fabs(input) sample-for-sample.
    auto input = sine(330.0f, 2048, 0.5f);
    auto out = run(kFullRect, 0.0f, 0.0f, input);
    REQUIRE(v::check_finite(out));
    for (int n = 0; n < (int)out.size(); ++n) {
        REQUIRE(out[n] >= -1e-6f);
        REQUIRE(std::fabs(out[n] - std::fabs(input[n])) < 1e-4f);
    }
}

TEST_CASE("Distortion Half Rect zeroes negative samples", "[distortion]") {
    // Unity in/out/tone: output must equal max(input, 0) (half-wave rectifier).
    auto input = sine(330.0f, 2048, 0.5f);
    auto out = run(kHalfRect, 0.0f, 0.0f, input);
    REQUIRE(v::check_finite(out));
    for (int n = 0; n < (int)out.size(); ++n) {
        REQUIRE(out[n] >= -1e-6f);
        REQUIRE(std::fabs(out[n] - std::max(input[n], 0.0f)) < 1e-4f);
    }
    // Roughly half the energy of the full-wave version (negatives removed).
    auto full = run(kFullRect, 0.0f, 0.0f, input);
    REQUIRE(rms(out) < rms(full));
}

TEST_CASE("Distortion clip types bound the output at the ±0.5 ceiling", "[distortion]") {
    // A signal that crosses the ±0.5 ceiling but spends time inside the knee, so
    // the soft shaper's rounded shoulder stays distinct from the hard corner.
    // Both clippers saturate at ±0.5; the rectifiers impose no such symmetric cap.
    auto loud = sine(440.0f, 4096, 0.9f);

    auto hard = run(kHardClip, 0.0f, 0.0f, loud);
    auto soft = run(kSoftClip, 0.0f, 0.0f, loud);

    REQUIRE(v::check_finite(hard));
    REQUIRE(v::check_finite(soft));
    REQUIRE(peak(hard) <= 0.5 + 1e-3);   // hard ceiling
    REQUIRE(peak(soft) <= 0.5 + 1e-3);   // soft knee, same ceiling
    REQUIRE(peak(hard) > 0.45);          // actually reaching the ceiling
    // Hard clip is a flat-topped square; soft clip rounds the corners, so the
    // two transfer functions must produce audibly different output.
    REQUIRE(sum_abs_diff(hard, soft, 256) > 5.0);
}

TEST_CASE("Distortion Input Gain drives the shaper harder", "[distortion]") {
    // Hard clip a quiet sine. With low In the signal stays below the ±0.5 knee
    // (near-linear, quiet); with high In it slams into the clip ceiling (loud).
    auto quiet = sine(220.0f, 4096, 0.05f);

    auto lo = run(kHardClip, -6.0f, 0.0f, quiet);   // stays linear
    auto hi = run(kHardClip, 24.0f, 0.0f, quiet);   // clipped square

    REQUIRE(v::check_finite(hi));
    REQUIRE(v::check_peak_below(hi, 1.0f));
    REQUIRE(rms(hi, 256) > rms(lo, 256) * 2.0);
}

TEST_CASE("Distortion Output Gain scales the result", "[distortion]") {
    // Full-wave rectify (a fixed, gain-independent shape at unity In) then trim
    // the output: +6 dB Out should roughly double the level of the result.
    auto input = sine(330.0f, 4096, 0.4f);

    auto base = run(kFullRect, 0.0f, 0.0f, input);
    auto up   = run(kFullRect, 0.0f, 6.0f, input);   // +6 dB ≈ ×2

    REQUIRE(v::check_finite(up));
    const double ratio = rms(up) / std::max(1e-9, rms(base));
    REQUIRE(ratio > 1.8);
    REQUIRE(ratio < 2.2);
}

TEST_CASE("Distortion Tone tilt changes the spectrum", "[distortion]") {
    // Hard clip generates harmonics; the tone high-shelf tilts their balance.
    // A bright (+24 dB) setting must differ audibly from a dark (-24 dB) one.
    auto input = sine(1500.0f, 4096, 0.5f);

    auto dark   = run(kHardClip, 12.0f, 0.0f, input, -24.0f);
    auto bright = run(kHardClip, 12.0f, 0.0f, input, 24.0f);

    REQUIRE(v::check_finite(dark));
    REQUIRE(v::check_finite(bright));
    REQUIRE(sum_abs_diff(bright, dark, 256) > 50.0);
}
