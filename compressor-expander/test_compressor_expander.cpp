#include <catch2/catch_test_macros.hpp>
#include "compressor_expander.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <utility>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_compressor_expander;
using pulp::examples::classic::kCompThreshold;
using pulp::examples::classic::kCompRatio;
using pulp::examples::classic::kExpThreshold;
using pulp::examples::classic::kExpRatio;
using pulp::examples::classic::kAttackMs;
using pulp::examples::classic::kReleaseMs;
using pulp::examples::classic::kMakeupDb;
using pulp::examples::classic::kCxBypass;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kN = 24000;          // 0.5 s @ 48k
constexpr int kTail0 = 16000;      // measure after the envelope settles

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
// Render distinct left/right inputs; return both output channels.
std::pair<std::vector<float>, std::vector<float>> render_stereo(
    format::HeadlessHost& h, const std::vector<float>& left, const std::vector<float>& right) {
    const int frames = (int)left.size();
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) { in.channel(0)[n] = left[n]; in.channel(1)[n] = right[n]; }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});
    std::vector<float> l(frames), r(frames);
    for (int n = 0; n < frames; ++n) { l[n] = out.channel(0)[n]; r[n] = out.channel(1)[n]; }
    return {l, r};
}
std::vector<float> sine(float amp, float hz, int n, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
double rms_tail(const std::vector<float>& x) {
    double acc = 0.0; int c = 0;
    for (int n = kTail0; n < (int)x.size(); ++n) { acc += (double)x[n] * x[n]; ++c; }
    return std::sqrt(acc / std::max(1, c));
}
// settled output/input gain for a steady sine at the given amplitude
double steady_gain(format::HeadlessHost& h, float amp) {
    auto in = sine(amp, 300.0f, kN);
    auto out = render(h, in);
    REQUIRE(v::check_finite(out));
    return rms_tail(out) / rms_tail(in);
}
}

TEST_CASE("Comp/Expander attenuates loud signals but leaves quiet ones alone", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, kN);
    h.state().set_value(kCompThreshold, -18.0f);
    h.state().set_value(kCompRatio, 4.0f);
    h.state().set_value(kExpThreshold, -80.0f);   // expander out of the way
    h.state().set_value(kExpRatio, 1.0f);
    h.state().set_value(kAttackMs, 5.0f);
    h.state().set_value(kReleaseMs, 200.0f);
    h.state().set_value(kMakeupDb, 0.0f);

    const double quiet = steady_gain(h, 0.05f);   // ~-26 dB, below threshold
    const double loud  = steady_gain(h, 0.5f);    // ~-6 dB, above threshold

    REQUIRE(quiet > 0.95);    // below threshold: ~unity
    REQUIRE(loud < 0.85);     // above threshold: compressed down
    REQUIRE(loud < quiet);    // level-dependent — a fixed gain can't do this
}

TEST_CASE("Comp/Expander expands (further attenuates) below the expander threshold", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, kN);
    h.state().set_value(kCompThreshold, 0.0f);    // compressor out of the way
    h.state().set_value(kCompRatio, 1.0f);
    h.state().set_value(kExpThreshold, -30.0f);
    h.state().set_value(kExpRatio, 3.0f);
    h.state().set_value(kAttackMs, 5.0f);
    h.state().set_value(kReleaseMs, 200.0f);
    h.state().set_value(kMakeupDb, 0.0f);

    const double moderate = steady_gain(h, 0.1f);  // ~-20 dB, above exp threshold
    const double tiny     = steady_gain(h, 0.01f); // ~-40 dB, below exp threshold

    REQUIRE(moderate > 0.9);   // above exp threshold: ~unity
    REQUIRE(tiny < 0.6);       // below exp threshold: expanded down
    REQUIRE(tiny < moderate);
}

TEST_CASE("Comp/Expander applies makeup gain", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, kN);
    h.state().set_value(kCompThreshold, 0.0f);    // no compression for our level
    h.state().set_value(kCompRatio, 1.0f);
    h.state().set_value(kExpThreshold, -80.0f);   // no expansion
    h.state().set_value(kExpRatio, 1.0f);
    h.state().set_value(kMakeupDb, 6.0f);         // +6 dB ~= x1.995

    const double g = steady_gain(h, 0.1f);
    REQUIRE(g > 1.9);
    REQUIRE(g < 2.1);
}

// Ballistics: a fast attack must reduce gain sooner than a slow attack. Without
// this, an instantaneous (no-smoothing) detector — the whole point of a
// compressor vs a waveshaper — would pass the steady-state tests.
TEST_CASE("Comp/Expander attack time governs how fast gain reduction engages", "[compexp]") {
    auto onset_to_half = [](float attack_ms) {
        format::HeadlessHost h(create_compressor_expander);
        h.prepare(48000.0, 12000);
        h.state().set_value(kCompThreshold, -18.0f);
        h.state().set_value(kCompRatio, 4.0f);
        h.state().set_value(kExpThreshold, -80.0f);
        h.state().set_value(kExpRatio, 1.0f);
        h.state().set_value(kAttackMs, attack_ms);
        h.state().set_value(kReleaseMs, 200.0f);
        h.state().set_value(kMakeupDb, 0.0f);
        // Step: silence, then a constant 0.5 (well above the -18 dB threshold).
        std::vector<float> step(12000, 0.0f);
        const int onset = 2000;
        for (int n = onset; n < 12000; ++n) step[n] = 0.5f;
        auto out = render(h, step);
        // Steady-state gain ~0.355 -> output settles to ~0.1775; halfway = ~0.339.
        const float half = 0.339f;
        for (int n = onset; n < 12000; ++n)
            if (out[n] <= half) return n - onset;
        return 12000 - onset;
    };
    const int t_fast = onset_to_half(1.0f);    // 1 ms attack
    const int t_slow = onset_to_half(50.0f);   // 50 ms attack
    REQUIRE(t_fast > 0);
    REQUIRE(t_slow > 2 * t_fast);               // slow attack engages much later
}

// Stereo link: the detector keys off the louder channel and applies ONE gain to
// both, so a quiet channel is attenuated alongside a loud one. A per-channel or
// channel-0-only detector would leave the quiet channel near unity.
TEST_CASE("Comp/Expander is stereo-linked (quiet channel follows the loud one)", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, kN);
    h.state().set_value(kCompThreshold, -18.0f);
    h.state().set_value(kCompRatio, 4.0f);
    h.state().set_value(kExpThreshold, -80.0f);
    h.state().set_value(kExpRatio, 1.0f);
    h.state().set_value(kAttackMs, 5.0f);
    h.state().set_value(kReleaseMs, 200.0f);
    h.state().set_value(kMakeupDb, 0.0f);

    auto loud_l = sine(0.5f, 300.0f, kN);    // ~-6 dB, above threshold
    auto quiet_r = sine(0.02f, 300.0f, kN);  // ~-34 dB, far below threshold alone
    auto [out_l, out_r] = render_stereo(h, loud_l, quiet_r);
    REQUIRE(v::check_finite(out_l));
    REQUIRE(v::check_finite(out_r));

    const double gain_l = rms_tail(out_l) / rms_tail(loud_l);
    const double gain_r = rms_tail(out_r) / rms_tail(quiet_r);
    REQUIRE(gain_l < 0.85);                       // loud channel compressed
    REQUIRE(gain_r < 0.85);                        // quiet channel pulled down too
    REQUIRE(std::fabs(gain_l - gain_r) < 0.05);    // same gain on both channels
}

TEST_CASE("Comp/Expander bypass passes through unchanged", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, 512);
    h.state().set_value(kCxBypass, 1.0f);
    auto in = sine(0.7f, 500.0f, 512);
    auto out = render(h, in);
    for (int n = 0; n < 512; ++n) REQUIRE(std::fabs(out[n] - in[n]) < 1e-6f);
}
