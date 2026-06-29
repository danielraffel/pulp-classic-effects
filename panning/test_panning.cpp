#include <catch2/catch_test_macros.hpp>
#include "panning.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_panning;
using pulp::examples::classic::kPanRate;
using pulp::examples::classic::kPanDepth;
using pulp::examples::classic::kPanWaveform;
using pulp::examples::classic::kPanBypass;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
struct Stereo { std::vector<float> l, r; };
Stereo render(format::HeadlessHost& h, const std::vector<float>& mono) {
    const int frames = (int)mono.size();
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) { in.channel(0)[n] = mono[n]; in.channel(1)[n] = mono[n]; }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});
    Stereo s; s.l.resize(frames); s.r.resize(frames);
    for (int n = 0; n < frames; ++n) { s.l[n] = out.channel(0)[n]; s.r[n] = out.channel(1)[n]; }
    return s;
}
std::vector<float> sine(float hz, int n, float amp = 0.4f, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
}

TEST_CASE("Auto-Pan parameters round-trip", "[panning]") {
    format::HeadlessHost h(create_panning);
    h.prepare(48000.0, 4096);
    for (state::ParamID id : {kPanRate, kPanDepth, kPanWaveform}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Auto-Pan preserves constant power across the sweep", "[panning]") {
    format::HeadlessHost h(create_panning);
    h.prepare(48000.0, 4096);
    h.state().set_value(kPanRate, 2.0f);
    h.state().set_value(kPanDepth, 100.0f);   // full sweep

    const float amp = 0.4f;
    auto input = sine(440.0f, 4096, amp);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out.l));
    REQUIRE(v::check_finite(out.r));
    REQUIRE(v::check_peak_below(out.l, 0.75f));
    REQUIRE(v::check_peak_below(out.r, 0.75f));

    // Equal-power law: outL² + outR² == 2·in² at every sample, independent of
    // pan position. Check the ratio where the input is well away from a zero.
    double max_dev = 0.0;
    for (int n = 0; n < 4096; ++n) {
        const double in2 = (double)input[n] * input[n];
        if (in2 < 0.5 * amp * amp) continue;  // skip near zero-crossings
        const double p = (double)out.l[n] * out.l[n] + (double)out.r[n] * out.r[n];
        max_dev = std::max(max_dev, std::fabs(p / (2.0 * in2) - 1.0));
    }
    REQUIRE(max_dev < 1.0e-3);
}

TEST_CASE("Auto-Pan moves the image (channels anti-correlate)", "[panning]") {
    format::HeadlessHost h(create_panning);
    h.prepare(48000.0, 48000);
    h.state().set_value(kPanRate, 2.0f);
    h.state().set_value(kPanDepth, 90.0f);

    auto input = sine(2000.0f, 48000);
    auto out = render(h, input);

    // Track per-channel envelopes (block RMS) and correlate them. A working pan
    // makes them anti-correlated — when the left gets louder the right gets
    // quieter. A no-op (or a bug pinning both gains) would correlate positively.
    std::vector<double> el, er;
    for (int from = 0; from + 1000 <= 48000; from += 1000) {
        double sl = 0.0, sr = 0.0;
        for (int n = from; n < from + 1000; ++n) { sl += (double)out.l[n]*out.l[n]; sr += (double)out.r[n]*out.r[n]; }
        el.push_back(std::sqrt(sl / 1000)); er.push_back(std::sqrt(sr / 1000));
    }
    const int m = (int)el.size();
    double ml = 0, mr = 0; for (int i = 0; i < m; ++i) { ml += el[i]; mr += er[i]; }
    ml /= m; mr /= m;
    double cov = 0, vl = 0, vr = 0;
    for (int i = 0; i < m; ++i) { cov += (el[i]-ml)*(er[i]-mr); vl += (el[i]-ml)*(el[i]-ml); vr += (er[i]-mr)*(er[i]-mr); }
    const double corr = cov / (std::sqrt(vl * vr) + 1e-12);
    REQUIRE(corr < -0.2);
}

TEST_CASE("Auto-Pan bypass is a clean passthrough", "[panning]") {
    format::HeadlessHost h(create_panning);
    h.prepare(48000.0, 2048);
    h.state().set_value(kPanBypass, 1.0f);
    auto input = sine(440.0f, 2048);
    auto out = render(h, input);
    for (int n = 0; n < 2048; ++n) { REQUIRE(out.l[n] == input[n]); REQUIRE(out.r[n] == input[n]); }
}
