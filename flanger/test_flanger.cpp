#include <catch2/catch_test_macros.hpp>
#include "flanger.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_flanger;
using pulp::examples::classic::kFlangerDelay;
using pulp::examples::classic::kFlangerWidth;
using pulp::examples::classic::kFlangerDepth;
using pulp::examples::classic::kFlangerFeedback;
using pulp::examples::classic::kFlangerInverted;
using pulp::examples::classic::kFlangerRate;
using pulp::examples::classic::kFlangerWaveform;
using pulp::examples::classic::kFlangerInterp;
using pulp::examples::classic::kFlangerStereo;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Render mono → stereo (same signal fed to both inputs); return channel 0.
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

// Same as render() but returns both output channels for stereo checks.
std::pair<std::vector<float>, std::vector<float>>
render_stereo(format::HeadlessHost& h, const std::vector<float>& mono) {
    const int frames = (int)mono.size();
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) { in.channel(0)[n] = mono[n]; in.channel(1)[n] = mono[n]; }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});
    std::vector<float> l(frames), r(frames);
    for (int n = 0; n < frames; ++n) { l[n] = out.channel(0)[n]; r[n] = out.channel(1)[n]; }
    return {std::move(l), std::move(r)};
}

std::vector<float> sine(float hz, int n, float amp = 0.3f, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
double sum_abs_diff(const std::vector<float>& a, const std::vector<float>& b, int from) {
    double d = 0.0; for (int n = from; n < (int)a.size(); ++n) d += std::fabs(a[n] - b[n]); return d;
}
// Largest peak-to-trough spread of windowed RMS across the buffer past warm-up.
double rms_spread(const std::vector<float>& out, int from, int win, int step) {
    auto window_rms = [&](int s) {
        double e = 0.0; for (int n = s; n < s + win; ++n) e += (double)out[n] * out[n];
        return std::sqrt(e / win);
    };
    double lo = 1e9, hi = 0.0;
    for (int s = from; s + win <= (int)out.size(); s += step) {
        const double r = window_rms(s);
        lo = std::min(lo, r); hi = std::max(hi, r);
    }
    return hi - lo;
}
}

TEST_CASE("Flanger parameters round-trip", "[flanger]") {
    format::HeadlessHost h(create_flanger);
    h.prepare(48000.0, 4096);
    // Continuous params: full normalized-raw round trip across the range.
    for (state::ParamID id :
         {kFlangerDelay, kFlangerWidth, kFlangerDepth, kFlangerFeedback, kFlangerRate}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    // Discrete params: each index reads back as itself.
    for (float idx : {0.0f, 1.0f, 2.0f, 3.0f})
        { h.state().set_value(kFlangerWaveform, idx); REQUIRE(h.state().get_value(kFlangerWaveform) == idx); }
    for (float idx : {0.0f, 1.0f, 2.0f})
        { h.state().set_value(kFlangerInterp, idx); REQUIRE(h.state().get_value(kFlangerInterp) == idx); }
    for (float idx : {0.0f, 1.0f}) {
        h.state().set_value(kFlangerInverted, idx); REQUIRE(h.state().get_value(kFlangerInverted) == idx);
        h.state().set_value(kFlangerStereo, idx);   REQUIRE(h.state().get_value(kFlangerStereo) == idx);
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Flanger LFO sweeps a comb notch over time", "[flanger]") {
    format::HeadlessHost h(create_flanger);
    h.prepare(48000.0, 48000);
    h.state().set_value(kFlangerRate, 2.0f);     // ~0.5 s period
    h.state().set_value(kFlangerDelay, 0.002f);
    h.state().set_value(kFlangerWidth, 0.02f);   // wide sweep
    h.state().set_value(kFlangerDepth, 1.0f);    // full comb
    h.state().set_value(kFlangerFeedback, 0.4f);

    auto input = sine(220.0f, 48000);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    REQUIRE(v::check_peak_below(out, 2.0f));

    // As the comb notch sweeps across the steady tone the summed amplitude rises
    // and falls. A dead/static LFO would hold the delay fixed and the RMS would
    // stay flat — the spread is what proves the modulation is live.
    const double dry_rms = 0.3 / std::sqrt(2.0);  // RMS of the 0.3-amplitude dry sine
    REQUIRE(rms_spread(out, 6000, 2000, 2000) > 0.15 * dry_rms);
}

TEST_CASE("Flanger feedback changes the output (resonant comb)", "[flanger]") {
    auto input = sine(330.0f, 4096);

    auto run = [&](float feedback) {
        format::HeadlessHost h(create_flanger);
        h.prepare(48000.0, 4096);
        h.state().set_value(kFlangerRate, 0.7f);
        h.state().set_value(kFlangerDelay, 0.002f);
        h.state().set_value(kFlangerWidth, 0.01f);
        h.state().set_value(kFlangerDepth, 1.0f);
        h.state().set_value(kFlangerFeedback, feedback);
        return render(h, input);
    };
    auto no_fb = run(0.0f);
    auto fb    = run(0.45f);

    REQUIRE(v::check_finite(fb));
    REQUIRE(v::check_peak_below(fb, 2.0f));
    // Feedback recirculates the delayed copy, so the resonant output must differ
    // materially from the feedback-free sweep.
    REQUIRE(sum_abs_diff(fb, no_fb, 1200) > 20.0);
}

TEST_CASE("Flanger Inverted flips the wet polarity", "[flanger]") {
    auto input = sine(440.0f, 4096);

    auto run = [&](float inverted) {
        format::HeadlessHost h(create_flanger);
        h.prepare(48000.0, 4096);
        h.state().set_value(kFlangerRate, 0.05f);   // effectively static over 4096 frames
        h.state().set_value(kFlangerDelay, 0.003f);
        h.state().set_value(kFlangerWidth, 0.004f);
        h.state().set_value(kFlangerDepth, 1.0f);
        h.state().set_value(kFlangerFeedback, 0.0f);  // identical line content both runs
        h.state().set_value(kFlangerInverted, inverted);
        return render(h, input);
    };
    auto normal = run(0.0f);
    auto invert = run(1.0f);

    // With feedback off the delay line holds the same dry history in both runs,
    // so the only difference is the sign of the wet add:
    //   normal = dry + wet,  inverted = dry - wet  →  normal + inverted = 2·dry.
    for (int n = 1200; n < (int)input.size(); ++n)
        REQUIRE(std::fabs((normal[n] + invert[n]) - 2.0f * input[n]) < 1e-3f);
    // And the wet term is non-trivial, so the two outputs genuinely differ.
    REQUIRE(sum_abs_diff(normal, invert, 1200) > 50.0);
}

TEST_CASE("Flanger Width sets the sweep amount", "[flanger]") {
    auto input = sine(220.0f, 24000);

    auto run = [&](float width) {
        format::HeadlessHost h(create_flanger);
        h.prepare(48000.0, 24000);
        h.state().set_value(kFlangerRate, 1.0f);
        h.state().set_value(kFlangerDelay, 0.002f);
        h.state().set_value(kFlangerWidth, width);
        h.state().set_value(kFlangerDepth, 1.0f);
        h.state().set_value(kFlangerFeedback, 0.0f);
        return render(h, input);
    };
    const double narrow = rms_spread(run(0.001f), 6000, 2000, 1000);
    const double wide   = rms_spread(run(0.02f),  6000, 2000, 1000);
    // A wider sweep moves the comb notch across a larger frequency span, so the
    // amplitude modulation depth is larger.
    REQUIRE(wide > narrow * 1.3);
}

TEST_CASE("Flanger Delay sets the base comb frequency", "[flanger]") {
    auto input = sine(440.0f, 4096);

    auto run = [&](float delay) {
        format::HeadlessHost h(create_flanger);
        h.prepare(48000.0, 4096);
        h.state().set_value(kFlangerRate, 0.05f);   // static over the buffer
        h.state().set_value(kFlangerDelay, delay);
        h.state().set_value(kFlangerWidth, 0.001f);
        h.state().set_value(kFlangerDepth, 1.0f);
        h.state().set_value(kFlangerFeedback, 0.0f);
        return render(h, input);
    };
    auto shorter = run(0.0015f);
    auto longer  = run(0.015f);
    // Different base delays put the comb notches at different frequencies, so the
    // steady-state outputs diverge materially.
    REQUIRE(sum_abs_diff(shorter, longer, 1200) > 30.0);
}

TEST_CASE("Flanger Interp selects the fractional read", "[flanger]") {
    auto input = sine(440.0f, 4096);

    auto run = [&](float interp) {
        format::HeadlessHost h(create_flanger);
        h.prepare(48000.0, 4096);
        h.state().set_value(kFlangerRate, 0.05f);
        h.state().set_value(kFlangerDelay, 0.0026f);  // fractional sample delay
        h.state().set_value(kFlangerWidth, 0.001f);
        h.state().set_value(kFlangerDepth, 1.0f);
        h.state().set_value(kFlangerFeedback, 0.0f);
        h.state().set_value(kFlangerInterp, interp);
        return render(h, input);
    };
    auto nearest = run(0.0f);
    auto linear  = run(1.0f);
    auto cubic   = run(2.0f);
    REQUIRE(v::check_finite(cubic));
    // The three interpolators reconstruct different values at a fractional read.
    REQUIRE(sum_abs_diff(nearest, linear, 1200) > 1.0);
    REQUIRE(sum_abs_diff(linear, cubic, 1200) > 0.1);
}

TEST_CASE("Flanger smooths the base Delay knob so the read position glides", "[flanger]") {
    // Static LFO (tiny width, slow rate), feedback off, depth 1: output = dry +
    // delayed. Isolate the wet term as (out - dry). Change the base Delay knob
    // abruptly between two blocks; per-sample smoothing glides the base read
    // position so the wet term only pitch-bends, whereas the pre-fix per-block
    // step would teleport the read and click.
    constexpr int block = 1024;
    constexpr float sr = 48000.0f, f = 220.0f, amp = 0.3f;

    format::HeadlessHost h(create_flanger);
    h.prepare(sr, block);
    h.state().set_value(kFlangerRate, 0.05f);     // effectively static over 2 blocks
    h.state().set_value(kFlangerWidth, 0.001f);   // negligible, ~constant sweep
    h.state().set_value(kFlangerDepth, 1.0f);
    h.state().set_value(kFlangerFeedback, 0.0f);
    h.state().set_value(kFlangerInterp, 1.0f);    // linear

    auto run_block = [&](int n0, std::vector<float>& wet) {
        audio::Buffer<float> in(2, block), out(2, block);
        std::vector<float> dry(block);
        for (int n = 0; n < block; ++n) {
            const float s = amp * std::sin(2.0f * kPi * f * (n0 + n) / sr);
            in.channel(0)[n] = s; in.channel(1)[n] = s; dry[n] = s;
        }
        const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
        audio::BufferView<const float> iv(ip, 2, block);
        auto ov = out.view();
        midi::MidiBuffer a, b;
        h.process(ov, iv, a, b, format::ProcessContext{});
        wet.resize(block);
        for (int n = 0; n < block; ++n) wet[n] = out.channel(0)[n] - dry[n];
    };

    std::vector<float> w1, w2;
    h.state().set_value(kFlangerDelay, 0.015f);   // 720 samples
    run_block(0, w1);
    h.state().set_value(kFlangerDelay, 0.002f);   // 96 samples — big abrupt change
    run_block(block, w2);
    REQUIRE(v::check_finite(w1));
    REQUIRE(v::check_finite(w2));

    // Largest sample-to-sample jump of the isolated wet term across the boundary
    // and through the glide. The delayed 220 Hz / 0.3-amp copy slews
    // ~0.0086/sample; the glide adds a little. 0.05 is smooth yet far below a jump.
    float max_step = std::fabs(w2[0] - w1[block - 1]);
    for (int n = 1; n < block; ++n)
        max_step = std::max(max_step, std::fabs(w2[n] - w2[n - 1]));
    REQUIRE(max_step < 0.05f);
}

TEST_CASE("Flanger Stereo offsets the channels", "[flanger]") {
    auto input = sine(220.0f, 24000);

    format::HeadlessHost mono(create_flanger);
    mono.prepare(48000.0, 24000);
    mono.state().set_value(kFlangerRate, 1.0f);
    mono.state().set_value(kFlangerDelay, 0.002f);
    mono.state().set_value(kFlangerWidth, 0.02f);
    mono.state().set_value(kFlangerDepth, 1.0f);
    mono.state().set_value(kFlangerStereo, 0.0f);
    auto [ml, mr] = render_stereo(mono, input);
    // Stereo off: both channels take the identical LFO phase → identical output.
    REQUIRE(sum_abs_diff(ml, mr, 0) < 1e-3);

    format::HeadlessHost wide(create_flanger);
    wide.prepare(48000.0, 24000);
    wide.state().set_value(kFlangerRate, 1.0f);
    wide.state().set_value(kFlangerDelay, 0.002f);
    wide.state().set_value(kFlangerWidth, 0.02f);
    wide.state().set_value(kFlangerDepth, 1.0f);
    wide.state().set_value(kFlangerStereo, 1.0f);
    auto [wl, wr] = render_stereo(wide, input);
    // Stereo on: the right channel runs a quarter-cycle ahead, so the two combs
    // sweep out of phase and the channels diverge materially.
    REQUIRE(sum_abs_diff(wl, wr, 1200) > 50.0);
}
