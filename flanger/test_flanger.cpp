#include <catch2/catch_test_macros.hpp>
#include "flanger.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_flanger;
using pulp::examples::classic::kFlangerRate;
using pulp::examples::classic::kFlangerDepth;
using pulp::examples::classic::kFlangerFeedback;
using pulp::examples::classic::kFlangerMix;
using pulp::examples::classic::kFlangerBypass;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
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
std::vector<float> sine(float hz, int n, float amp = 0.4f, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
double sum_abs_diff(const std::vector<float>& a, const std::vector<float>& b, int from) {
    double d = 0.0; for (int n = from; n < (int)a.size(); ++n) d += std::fabs(a[n] - b[n]); return d;
}
}

TEST_CASE("Flanger parameters round-trip", "[flanger]") {
    format::HeadlessHost h(create_flanger);
    h.prepare(48000.0, 4096);
    for (state::ParamID id : {kFlangerRate, kFlangerDepth, kFlangerFeedback, kFlangerMix}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Flanger LFO modulates the output over time", "[flanger]") {
    format::HeadlessHost h(create_flanger);
    h.prepare(48000.0, 48000);
    h.state().set_value(kFlangerRate, 2.0f);    // ~0.5 s period
    h.state().set_value(kFlangerDepth, 3.0f);
    h.state().set_value(kFlangerFeedback, 0.5f);
    h.state().set_value(kFlangerMix, 100.0f);   // fully wet to isolate the comb

    auto input = sine(220.0f, 48000);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    REQUIRE(v::check_peak_below(out, 1.5f));

    // As the comb notch sweeps across the steady tone the output amplitude rises
    // and falls. Sample local RMS across the buffer (past warm-up) and require a
    // real peak-to-trough spread. A static or dead LFO holds the delay constant
    // and the RMS would stay flat — this is what actually proves the modulation
    // is live, which a fixed-delay reference comparison cannot.
    auto window_rms = [&](int from, int len) {
        double e = 0.0; for (int n = from; n < from + len; ++n) e += (double)out[n] * out[n];
        return std::sqrt(e / len);
    };
    double lo = 1e9, hi = 0.0;
    for (int from = 6000; from + 2000 <= 48000; from += 2000) {
        const double r = window_rms(from, 2000);
        lo = std::min(lo, r); hi = std::max(hi, r);
    }
    REQUIRE(hi > lo * 1.15);   // >=15% peak-to-trough modulation
}

TEST_CASE("Flanger feedback changes the output (resonant comb)", "[flanger]") {
    auto input = sine(330.0f, 4096);

    format::HeadlessHost dry_fb(create_flanger);
    dry_fb.prepare(48000.0, 4096);
    dry_fb.state().set_value(kFlangerRate, 0.7f);
    dry_fb.state().set_value(kFlangerDepth, 2.0f);
    dry_fb.state().set_value(kFlangerMix, 100.0f);
    dry_fb.state().set_value(kFlangerFeedback, 0.0f);
    auto no_fb = render(dry_fb, input);

    format::HeadlessHost with_fb(create_flanger);
    with_fb.prepare(48000.0, 4096);
    with_fb.state().set_value(kFlangerRate, 0.7f);
    with_fb.state().set_value(kFlangerDepth, 2.0f);
    with_fb.state().set_value(kFlangerMix, 100.0f);
    with_fb.state().set_value(kFlangerFeedback, 0.8f);
    auto fb = render(with_fb, input);

    REQUIRE(v::check_finite(fb));
    REQUIRE(v::check_peak_below(fb, 1.5f));
    // Feedback recirculates the delayed copy, so the resonant output must differ
    // materially from the feedback-free sweep.
    REQUIRE(sum_abs_diff(fb, no_fb, 1200) > 50.0);
}

TEST_CASE("Flanger bypass is a clean passthrough", "[flanger]") {
    format::HeadlessHost h(create_flanger);
    h.prepare(48000.0, 4096);
    h.state().set_value(kFlangerBypass, 1.0f);
    auto input = sine(440.0f, 2048);
    auto out = render(h, input);
    for (int n = 0; n < (int)out.size(); ++n) REQUIRE(out[n] == input[n]);
}
