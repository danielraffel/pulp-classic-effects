#include <catch2/catch_test_macros.hpp>
#include "phaser.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_phaser;
using pulp::examples::classic::kPhaserDepth;
using pulp::examples::classic::kPhaserFeedback;
using pulp::examples::classic::kPhaserStages;
using pulp::examples::classic::kPhaserMinFreq;
using pulp::examples::classic::kPhaserSweep;
using pulp::examples::classic::kPhaserRate;
using pulp::examples::classic::kPhaserWaveform;
using pulp::examples::classic::kPhaserStereo;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Render a mono signal through both input channels in a single process pass;
// return both output channels so stereo divergence can be measured coherently.
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
    return {l, r};
}
std::vector<float> render(format::HeadlessHost& h, const std::vector<float>& mono) {
    return render_stereo(h, mono).first;
}
std::vector<float> sine(float hz, int n, float amp = 0.4f, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
// Deterministic broadband test signal: a fixed pseudo-random noise burst. Lets
// the static-LFO tests probe many frequencies at once so notch movement and
// feedback resonance show up regardless of exact notch placement.
std::vector<float> noise(int n, float amp = 0.3f) {
    std::vector<float> s(n);
    uint32_t state = 0x1234567u;
    for (int i = 0; i < n; ++i) {
        state = state * 1664525u + 1013904223u;
        const float u = (float)(state >> 8) / (float)(1u << 24);  // 0..1
        s[i] = amp * (2.0f * u - 1.0f);
    }
    return s;
}
double window_rms(const std::vector<float>& x, int from, int len) {
    double e = 0.0; for (int n = from; n < from + len; ++n) e += (double)x[n] * x[n];
    return std::sqrt(e / len);
}
double sum_abs_diff(const std::vector<float>& a, const std::vector<float>& b, int from) {
    double d = 0.0; for (int n = from; n < (int)a.size(); ++n) d += std::fabs(a[n] - b[n]);
    return d;
}
float peak(const std::vector<float>& x, int from) {
    float p = 0.0f; for (int n = from; n < (int)x.size(); ++n) p = std::max(p, std::fabs(x[n]));
    return p;
}
}

TEST_CASE("Phaser parameters round-trip", "[phaser]") {
    format::HeadlessHost h(create_phaser);
    h.prepare(48000.0, 8192);
    for (state::ParamID id : {kPhaserDepth, kPhaserFeedback, kPhaserStages,
                              kPhaserMinFreq, kPhaserSweep, kPhaserRate,
                              kPhaserWaveform, kPhaserStereo}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Phaser default parameters match the textbook controls", "[phaser]") {
    format::HeadlessHost h(create_phaser);
    h.prepare(48000.0, 64);
    // Defaults: Depth 1, Feedback 0.7, Stages index 1 (=4), Min 80 Hz,
    // Sweep 1000 Hz, Rate 0.05 Hz, Sine waveform, Stereo on.
    REQUIRE(h.state().get_value(kPhaserDepth) == 1.0f);
    REQUIRE(h.state().get_value(kPhaserFeedback) == 0.7f);
    REQUIRE(h.state().get_value(kPhaserStages) == 1.0f);
    REQUIRE(h.state().get_value(kPhaserMinFreq) == 80.0f);
    REQUIRE(h.state().get_value(kPhaserSweep) == 1000.0f);
    REQUIRE(h.state().get_value(kPhaserRate) == 0.05f);
    REQUIRE(h.state().get_value(kPhaserWaveform) == 0.0f);
    REQUIRE(h.state().get_value(kPhaserStereo) == 1.0f);
}

TEST_CASE("Phaser colours the signal and stays bounded", "[phaser]") {
    format::HeadlessHost h(create_phaser);
    h.prepare(48000.0, 8192);
    h.state().set_value(kPhaserDepth, 1.0f);
    h.state().set_value(kPhaserFeedback, 0.4f);
    h.state().set_value(kPhaserRate, 1.0f);

    auto input = sine(800.0f, 8192);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    REQUIRE(v::check_peak_below(out, 1.0f));

    // The all-pass copy phase-cancels parts of the dry signal, so the output
    // must differ from the input.
    REQUIRE(sum_abs_diff(out, input, 2000) > 50.0);
}

TEST_CASE("Phaser LFO modulates the output over time", "[phaser]") {
    format::HeadlessHost h(create_phaser);
    h.prepare(48000.0, 48000);
    h.state().set_value(kPhaserRate, 2.0f);    // ~0.5 s period
    h.state().set_value(kPhaserDepth, 1.0f);
    h.state().set_value(kPhaserFeedback, 0.6f);
    h.state().set_value(kPhaserStereo, 0.0f);

    auto input = sine(800.0f, 48000);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));

    // As the notches sweep across the steady tone, local RMS rises and falls —
    // proof the sweep is live, not a static filter.
    double lo = 1e9, hi = 0.0;
    for (int from = 12000; from + 2000 <= 48000; from += 2000) {
        const double rms = window_rms(out, from, 2000);
        lo = std::min(lo, rms); hi = std::max(hi, rms);
    }
    REQUIRE(hi > lo * 1.15);   // >=15% peak-to-trough modulation
}

TEST_CASE("Phaser stage count changes the notch pattern", "[phaser]") {
    // Static LFO (rate 0) freezes the notch positions, so any difference is due
    // to cascade depth alone. More all-pass stages -> more phase wrap -> more
    // notches -> a measurably different output.
    auto run_stages = [](float stages_index) {
        format::HeadlessHost h(create_phaser);
        h.prepare(48000.0, 8192);
        h.state().set_value(kPhaserRate, 0.0f);
        h.state().set_value(kPhaserDepth, 1.0f);
        h.state().set_value(kPhaserFeedback, 0.0f);
        h.state().set_value(kPhaserStereo, 0.0f);
        h.state().set_value(kPhaserStages, stages_index);
        return render(h, noise(8192));
    };
    auto two = run_stages(0.0f);   // 2 stages
    auto ten = run_stages(4.0f);   // 10 stages
    REQUIRE(v::check_finite(two));
    REQUIRE(v::check_finite(ten));
    // Distinct stage counts produce distinct colourations.
    REQUIRE(sum_abs_diff(two, ten, 1000) > 100.0);
}

TEST_CASE("Phaser Min Freq and Sweep Width set the sweep range", "[phaser]") {
    // With a static LFO the notch sits at a fixed centre = Min + Sweep*lfo(0).
    // Moving either control relocates the notch, changing the output.
    auto run = [](float min_f, float sweep) {
        format::HeadlessHost h(create_phaser);
        h.prepare(48000.0, 8192);
        h.state().set_value(kPhaserRate, 0.0f);
        h.state().set_value(kPhaserDepth, 1.0f);
        h.state().set_value(kPhaserFeedback, 0.0f);
        h.state().set_value(kPhaserStereo, 0.0f);
        h.state().set_value(kPhaserStages, 4.0f);
        h.state().set_value(kPhaserMinFreq, min_f);
        h.state().set_value(kPhaserSweep, sweep);
        return render(h, noise(8192));
    };
    auto low_min  = run(60.0f, 1000.0f);
    auto high_min = run(800.0f, 1000.0f);
    REQUIRE(sum_abs_diff(low_min, high_min, 1000) > 100.0);  // Min Freq moves the notch

    auto narrow = run(80.0f, 100.0f);
    auto wide   = run(80.0f, 3000.0f);
    REQUIRE(sum_abs_diff(narrow, wide, 1000) > 100.0);       // Sweep Width moves the notch
}

TEST_CASE("Phaser feedback sharpens the notches", "[phaser]") {
    // Feedback wraps the cascade into a resonant loop. With a static sweep, high
    // feedback boosts the response near resonance, lifting the output peak above
    // the feedback-free case on the same broadband input.
    auto run_fb = [](float fb) {
        format::HeadlessHost h(create_phaser);
        h.prepare(48000.0, 8192);
        h.state().set_value(kPhaserRate, 0.0f);
        h.state().set_value(kPhaserDepth, 1.0f);
        h.state().set_value(kPhaserStereo, 0.0f);
        h.state().set_value(kPhaserStages, 4.0f);
        h.state().set_value(kPhaserFeedback, fb);
        return render(h, noise(8192));
    };
    auto none = run_fb(0.0f);
    auto high = run_fb(0.85f);
    REQUIRE(v::check_finite(high));
    REQUIRE(v::check_peak_below(high, 1.5f));
    // Resonant feedback sharpens/peaks the response: higher peak than fb=0.
    REQUIRE(peak(high, 1000) > peak(none, 1000) * 1.1f);
    // And it audibly changes the signal.
    REQUIRE(sum_abs_diff(none, high, 1000) > 100.0);
}

TEST_CASE("Phaser stereo offsets the two channels", "[phaser]") {
    auto input = sine(800.0f, 8192);

    // Stereo on: the odd channel's LFO is a quarter cycle ahead, so the two
    // output channels differ even though both inputs are identical.
    format::HeadlessHost on(create_phaser);
    on.prepare(48000.0, 8192);
    on.state().set_value(kPhaserStereo, 1.0f);
    on.state().set_value(kPhaserRate, 1.0f);
    on.state().set_value(kPhaserDepth, 1.0f);
    auto [l_on, r_on] = render_stereo(on, input);
    REQUIRE(sum_abs_diff(l_on, r_on, 0) > 50.0);

    // Stereo off: both channels get the identical LFO, so the outputs match.
    format::HeadlessHost off(create_phaser);
    off.prepare(48000.0, 8192);
    off.state().set_value(kPhaserStereo, 0.0f);
    off.state().set_value(kPhaserRate, 1.0f);
    off.state().set_value(kPhaserDepth, 1.0f);
    auto [l_off, r_off] = render_stereo(off, input);
    REQUIRE(sum_abs_diff(l_off, r_off, 0) < 1e-3);
}
