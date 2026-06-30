#include <catch2/catch_test_macros.hpp>
#include "delay.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <algorithm>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_delay;
using pulp::examples::classic::kTimeMs;
using pulp::examples::classic::kFeedback;
using pulp::examples::classic::kDelayMix;
namespace v = pulp::format::validation;

namespace {
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
}

TEST_CASE("Delay reproduces the input delayed by the set time", "[delay]") {
    format::HeadlessHost h(create_delay);
    h.prepare(48000.0, 2048);
    h.state().set_value(kTimeMs, 0.01f);     // 10 ms → 480 samples @ 48k
    h.state().set_value(kFeedback, 0.0f);
    h.state().set_value(kDelayMix, 1.0f);    // wet only

    std::vector<float> impulse(2048, 0.0f);
    impulse[0] = 1.0f;
    auto out = render(h, impulse);
    REQUIRE(v::check_finite(out));
    REQUIRE(out[0] < 0.01f);                  // nothing arrived yet (wet-only)
    float peak_around = 0.0f;
    for (int n = 470; n <= 490; ++n) peak_around = std::max(peak_around, std::fabs(out[n]));
    REQUIRE(peak_around > 0.5f);              // delayed impulse landed near 480
}

namespace {
// Peak magnitude in a small window centred on an expected echo position.
float peak_near(const std::vector<float>& x, int centre, int radius = 8) {
    float p = 0.0f;
    for (int n = std::max(0, centre - radius);
         n <= std::min((int)x.size() - 1, centre + radius); ++n)
        p = std::max(p, std::fabs(x[n]));
    return p;
}
}

TEST_CASE("Delay feedback produces repeating, decaying echoes", "[delay]") {
    format::HeadlessHost h(create_delay);
    h.prepare(48000.0, 4096);
    const float time_ms = 5.0f;                 // 240 samples @ 48k
    h.state().set_value(kTimeMs, time_ms / 1000.0f);  // Time is in seconds
    h.state().set_value(kFeedback, 0.9f);
    h.state().set_value(kDelayMix, 1.0f);
    std::vector<float> impulse(4096, 0.0f); impulse[0] = 1.0f;
    auto out = render(h, impulse);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 1.001f));   // fb<1: no echo exceeds the impulse

    // Three successive echoes, each spaced by the delay time, each quieter than
    // the last. A disconnected feedback path would yield only the first echo.
    const int d = (int)std::lround(time_ms / 1000.0f * 48000.0f);
    const float e1 = peak_near(out, d + 1);
    const float e2 = peak_near(out, 2 * d + 1);
    const float e3 = peak_near(out, 3 * d + 1);
    REQUIRE(e1 > 0.5f);
    REQUIRE(e2 > 0.05f);
    REQUIRE(e3 > 0.005f);
    REQUIRE(e2 < e1);                            // genuine decay, not a fixed echo
    REQUIRE(e3 < e2);
}

TEST_CASE("Delay with mix=0 passes the dry signal through unchanged", "[delay]") {
    format::HeadlessHost h(create_delay);
    h.prepare(48000.0, 512);
    h.state().set_value(kTimeMs, 0.01f);   // 10 ms — would be audible if mixed in
    h.state().set_value(kFeedback, 0.5f);  // feedback fills the line but stays wet
    h.state().set_value(kDelayMix, 0.0f);  // fully dry: output == input
    std::vector<float> ramp(512);
    for (int n = 0; n < 512; ++n) ramp[n] = -0.5f + (float)n / 511.0f;
    auto out = render(h, ramp);
    REQUIRE(v::check_finite(out));
    for (int n = 0; n < 512; ++n) REQUIRE(std::fabs(out[n] - ramp[n]) < 1e-6f);
}
