#include <catch2/catch_test_macros.hpp>
#include "vibrato.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_vibrato;
using pulp::examples::classic::kVibRateHz;
using pulp::examples::classic::kVibDepthMs;
using pulp::examples::classic::kVibBypass;
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
std::vector<float> sine(float hz, int n, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = 0.5f * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
}

// The vibrato applies a *static* base delay (= depth+1 samples) on top of the
// LFO sweep. So comparing the output to the dry input would pass even if the
// LFO were dead (a constant delay still differs from dry). The honest test is
// against the constant-delay reference: a working vibrato must diverge from a
// fixed read at the mean position; a dead LFO would match it.
TEST_CASE("Vibrato sweep diverges from a constant-delay reference", "[vibrato]") {
    format::HeadlessHost h(create_vibrato);
    h.prepare(48000.0, 4096);
    const float rate = 5.0f, depth_ms = 3.0f;
    h.state().set_value(kVibRateHz, rate);
    h.state().set_value(kVibDepthMs, depth_ms);

    auto input = sine(220.0f, 4096);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    REQUIRE(v::check_peak_below(out, 0.75f));   // a delay can't amplify a 0.5 sine

    // Mean read position with push-before-read: out_unmod[i] == input[i - base].
    const int base = (int)std::lround(depth_ms / 1000.0f * 48000.0f) + 1;
    double diff_vs_const = 0.0;   // vibrato output vs the dead-LFO reference
    double diff_const_id = 0.0;   // the reference vs itself (sanity, ~0)
    for (int n = 1000; n < 4096; ++n) {
        const float ref = (n - base >= 0) ? input[n - base] : 0.0f;
        diff_vs_const += std::fabs(out[n] - ref);
        diff_const_id += std::fabs(ref - ref);
    }
    REQUIRE(diff_const_id == 0.0);
    REQUIRE(diff_vs_const > 50.0);   // a dead LFO would leave this near zero
}

TEST_CASE("Vibrato bypass is an exact passthrough", "[vibrato]") {
    format::HeadlessHost h(create_vibrato);
    h.prepare(48000.0, 1024);
    h.state().set_value(kVibBypass, 1.0f);
    // Use a time-varying signal so a 1-sample shift or sign flip would be caught.
    auto ramp_in = sine(1000.0f, 512);
    auto out = render(h, ramp_in);
    for (int n = 0; n < (int)ramp_in.size(); ++n)
        REQUIRE(std::fabs(out[n] - ramp_in[n]) < 1e-6f);
}
