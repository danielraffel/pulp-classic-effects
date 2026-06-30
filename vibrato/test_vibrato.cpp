#include <catch2/catch_test_macros.hpp>
#include "vibrato.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_vibrato;
using pulp::examples::classic::kVibWidthSecs;
using pulp::examples::classic::kVibRateHz;
using pulp::examples::classic::kVibWaveform;
using pulp::examples::classic::kVibInterp;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Render a mono signal through a fresh vibrato instance with the given settings.
// Each call gets its own host so delay-line / LFO state never leaks between
// reference and test renders.
std::vector<float> render(float width_s, float rate_hz, int waveform, int interp,
                          const std::vector<float>& mono) {
    format::HeadlessHost h(create_vibrato);
    h.prepare(48000.0, 4096);
    h.state().set_value(kVibWidthSecs, width_s);
    h.state().set_value(kVibRateHz, rate_hz);
    h.state().set_value(kVibWaveform, static_cast<float>(waveform));
    h.state().set_value(kVibInterp, static_cast<float>(interp));

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

// Sum of |a - b| over the steady-state tail (skips the warm-up where the delay
// line is still filling).
double tail_diff(const std::vector<float>& a, const std::vector<float>& b) {
    double d = 0.0;
    for (int n = 1000; n < (int)a.size(); ++n) d += std::fabs(a[n] - b[n]);
    return d;
}
} // namespace

// A live LFO sweeps the delay each sample; a frozen LFO (rate 0) holds a constant
// delay. The honest proof that the modulation is real is that the swept output
// diverges from the constant-delay reference — a dead LFO would match it.
TEST_CASE("Vibrato sweep diverges from a constant-delay reference", "[vibrato]") {
    auto input = sine(220.0f, 4096);
    // interp = Linear (1), waveform = Sine (0).
    auto swept = render(0.003f, 5.0f, 0, 1, input);
    auto frozen = render(0.003f, 0.0f, 0, 1, input);   // rate 0 -> static delay

    REQUIRE(v::check_finite(swept));
    REQUIRE(v::check_any_nonzero(swept));
    REQUIRE(v::check_peak_below(swept, 0.75f));   // a delay can't amplify a 0.5 sine

    // The frozen reference is itself a non-trivial (constant) delay, so this
    // divergence isolates the LFO motion specifically.
    REQUIRE(tail_diff(swept, frozen) > 50.0);
}

// Width is the peak swept delay: a wider setting swings the delay further, so
// the live output departs further from its own frozen (rate-0) baseline. Each
// width is measured against the constant delay it would hold with a dead LFO,
// which isolates the modulation amplitude from the static delay offset.
TEST_CASE("Vibrato Width sets the modulation depth", "[vibrato]") {
    auto input = sine(220.0f, 4096);
    auto narrow_swept  = render(0.001f, 5.0f, 0, 1, input);
    auto narrow_frozen = render(0.001f, 0.0f, 0, 1, input);
    auto wide_swept    = render(0.02f, 5.0f, 0, 1, input);
    auto wide_frozen   = render(0.02f, 0.0f, 0, 1, input);

    REQUIRE(v::check_finite(narrow_swept));
    REQUIRE(v::check_finite(wide_swept));

    const double mod_narrow = tail_diff(narrow_swept, narrow_frozen);
    const double mod_wide   = tail_diff(wide_swept, wide_frozen);
    REQUIRE(mod_narrow > 0.0);                 // even a thin width modulates
    REQUIRE(mod_wide > mod_narrow * 1.5);      // wider swings the delay harder
}

// The LFO rate controls how fast the delay sweeps; two different rates produce
// audibly different modulation, and a faster rate has more sweep extrema.
TEST_CASE("Vibrato Rate changes the modulation", "[vibrato]") {
    auto input = sine(220.0f, 4096);
    auto slow = render(0.005f, 1.0f, 0, 1, input);
    auto fast = render(0.005f, 8.0f, 0, 1, input);

    REQUIRE(v::check_finite(slow));
    REQUIRE(v::check_finite(fast));
    REQUIRE(tail_diff(slow, fast) > 50.0);   // different rates, different output
}

// The four LFO shapes drive different delay trajectories, so the rendered output
// differs between waveforms at identical Width/Rate/Interp.
TEST_CASE("Vibrato Waveform options differ", "[vibrato]") {
    auto input = sine(220.0f, 4096);
    auto wsine = render(0.005f, 4.0f, 0, 1, input);   // Sine
    auto wtri  = render(0.005f, 4.0f, 1, 1, input);   // Triangle
    auto wsaw  = render(0.005f, 4.0f, 2, 1, input);   // Sawtooth
    auto winv  = render(0.005f, 4.0f, 3, 1, input);   // Inverse Sawtooth

    REQUIRE(v::check_finite(wsine));
    REQUIRE(v::check_finite(wtri));
    REQUIRE(v::check_finite(wsaw));
    REQUIRE(v::check_finite(winv));
    REQUIRE(tail_diff(wsine, wtri) > 20.0);
    REQUIRE(tail_diff(wtri, wsaw) > 20.0);
    REQUIRE(tail_diff(wsaw, winv) > 20.0);
}

// The three reconstruction modes read the same fractional delay trajectory but
// reconstruct it differently, so their outputs are distinct.
TEST_CASE("Vibrato Interp modes differ", "[vibrato]") {
    // A higher-frequency tone carries more energy between samples, so the choice
    // of fractional reconstruction shows up clearly.
    auto input = sine(3000.0f, 4096);
    auto nearest = render(0.005f, 4.0f, 0, 0, input);  // Nearest
    auto linear  = render(0.005f, 4.0f, 0, 1, input);  // Linear
    auto cubic   = render(0.005f, 4.0f, 0, 2, input);  // Cubic

    REQUIRE(v::check_finite(nearest));
    REQUIRE(v::check_finite(linear));
    REQUIRE(v::check_finite(cubic));
    // Nearest is the crudest; it diverges most from the smoother interpolators.
    REQUIRE(tail_diff(nearest, linear) > 20.0);
    REQUIRE(tail_diff(nearest, cubic) > 20.0);
    // Linear and cubic agree more closely than nearest does with either, but
    // still differ where curvature matters.
    REQUIRE(tail_diff(linear, cubic) > 1.0);
    REQUIRE(tail_diff(linear, cubic) < tail_diff(nearest, linear));
}
