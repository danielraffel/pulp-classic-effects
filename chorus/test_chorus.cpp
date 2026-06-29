#include <catch2/catch_test_macros.hpp>
#include "chorus.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_chorus;
using pulp::examples::classic::kChorusRate;
using pulp::examples::classic::kChorusDepth;
using pulp::examples::classic::kChorusMix;
using pulp::examples::classic::kChorusBypass;
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

TEST_CASE("Chorus sweep diverges from a constant-delay reference", "[chorus]") {
    format::HeadlessHost h(create_chorus);
    h.prepare(48000.0, 4096);
    h.state().set_value(kChorusRate, 1.5f);
    h.state().set_value(kChorusDepth, 5.0f);
    h.state().set_value(kChorusMix, 100.0f);   // fully wet to isolate the sweep

    auto input = sine(330.0f, 4096);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    REQUIRE(v::check_peak_below(out, 0.75f));

    // Wet, no modulation would just be a fixed ~15 ms (720-sample) delay of the
    // input. A working LFO makes the output diverge from that reference.
    const int base = (int)std::lround(0.015f * 48000.0f);  // 720
    double diff = 0.0;
    for (int n = 1200; n < 4096; ++n) {
        const float ref = (n - base >= 0) ? input[n - base] : 0.0f;
        diff += std::fabs(out[n] - ref);
    }
    REQUIRE(diff > 50.0);
}

// Guards against an "always-dry" regression: at depth=0 the sweep collapses to
// a clean fixed ~15 ms delay, so a fully-wet output must MATCH the delayed copy
// and clearly NOT match the dry input. Without this, a chorus that dropped the
// wet term entirely would still pass the divergence + mix=0 + bypass tests.
TEST_CASE("Chorus mix=100, depth=0 delivers the wet delayed signal (not dry)", "[chorus]") {
    format::HeadlessHost h(create_chorus);
    h.prepare(48000.0, 4096);
    h.state().set_value(kChorusRate, 1.5f);
    h.state().set_value(kChorusDepth, 0.0f);   // no sweep -> fixed ~720-sample delay
    h.state().set_value(kChorusMix, 100.0f);

    auto input = sine(330.0f, 4096);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    const int base = (int)std::lround(0.015f * 48000.0f);  // 720
    double err = 0.0, dry_err = 0.0;
    for (int n = 1200; n < 4096; ++n) {
        err += std::fabs(out[n] - input[n - base]);  // must equal the delayed copy
        dry_err += std::fabs(out[n] - input[n]);      // must not equal the dry input
    }
    REQUIRE(err < 1.0);          // wet path actually delivers the delayed signal
    REQUIRE(dry_err > 100.0);    // and it is clearly not just the dry signal
}

TEST_CASE("Chorus at mix=0 is exact dry; bypass passes through", "[chorus]") {
    format::HeadlessHost h(create_chorus);
    h.prepare(48000.0, 512);
    h.state().set_value(kChorusMix, 0.0f);
    auto ramp = sine(800.0f, 512);
    {
        auto dry = render(h, ramp);
        for (std::size_t i = 0; i < ramp.size(); ++i)
            REQUIRE(std::fabs(dry[i] - ramp[i]) < 1e-6f);
    }
    h.state().set_value(kChorusMix, 100.0f);
    h.state().set_value(kChorusBypass, 1.0f);
    auto out = render(h, ramp);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        REQUIRE(std::fabs(out[i] - ramp[i]) < 1e-6f);
}
