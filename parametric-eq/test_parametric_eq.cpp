#include <catch2/catch_test_macros.hpp>
#include "parametric_eq.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <utility>
#include <vector>

using namespace pulp;
using namespace pulp::examples::classic;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kN = 8000;
constexpr int kTail0 = 4000;

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
double gain_at(format::HeadlessHost& h, float hz, float amp = 0.2f) {
    auto in = sine(amp, hz, kN);
    auto out = render(h, in);
    REQUIRE(v::check_finite(out));
    return rms_tail(out) / rms_tail(in);
}
}

TEST_CASE("Parametric EQ is flat (~unity) with all gains at 0 dB", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    for (float f : {80.0f, 1000.0f, 8000.0f}) {
        const double g = gain_at(h, f);
        REQUIRE(g > 0.97);
        REQUIRE(g < 1.03);
    }
}

TEST_CASE("Parametric EQ low shelf boosts lows, leaves highs alone", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    h.state().set_value(kLowFreq, 200.0f);
    h.state().set_value(kLowGain, 12.0f);
    REQUIRE(gain_at(h, 60.0f) > 1.5);            // low band boosted
    const double high = gain_at(h, 8000.0f);
    REQUIRE(high > 0.9); REQUIRE(high < 1.1);    // highs untouched
}

TEST_CASE("Parametric EQ mid bell cut attenuates its band only", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    h.state().set_value(kMidFreq, 1000.0f);
    h.state().set_value(kMidQ, 2.0f);
    h.state().set_value(kMidGain, -12.0f);
    REQUIRE(gain_at(h, 1000.0f) < 0.6);          // mid band cut
    const double low = gain_at(h, 100.0f);
    REQUIRE(low > 0.9); REQUIRE(low < 1.1);      // lows untouched
}

TEST_CASE("Parametric EQ high shelf boosts highs, leaves lows alone", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    h.state().set_value(kHighFreq, 5000.0f);
    h.state().set_value(kHighGain, 12.0f);
    REQUIRE(gain_at(h, 10000.0f) > 1.5);         // high band boosted
    const double low = gain_at(h, 200.0f);
    REQUIRE(low > 0.9); REQUIRE(low < 1.1);      // lows untouched
}

TEST_CASE("Parametric EQ stays stable at extreme settings", "[eq]") {
    // Prepare at 8 kHz so the mid band's 5 kHz max sits ABOVE Nyquist (4 kHz),
    // forcing the process()-level 0.49*sr freq clamp to engage — the real
    // blowup guard. Excite with a broadband impulse so the narrow +18 dB/Q=10
    // resonance actually rings, then assert it stays finite and bounded.
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(8000.0, kN);
    h.state().set_value(kMidFreq, 5000.0f);      // > Nyquist@8k -> clamped in process
    h.state().set_value(kMidQ, 10.0f);
    h.state().set_value(kMidGain, 18.0f);
    h.state().set_value(kLowGain, 18.0f);
    h.state().set_value(kHighGain, 18.0f);
    std::vector<float> impulse(kN, 0.0f); impulse[0] = 1.0f;
    auto out = render(h, impulse);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 12.0f));     // rings but stays bounded
    // The ring must decay — a stable filter settles back toward silence.
    float tail = 0.0f;
    for (int n = kN - 500; n < kN; ++n) tail = std::max(tail, std::fabs(out[n]));
    REQUIRE(tail < 0.05f);
}

TEST_CASE("Parametric EQ processes channels independently (per-channel state)", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    h.state().set_value(kLowFreq, 200.0f);
    h.state().set_value(kLowGain, 12.0f);        // boosts lows only
    // Left = low tone (in the boosted shelf), Right = high tone (outside it).
    auto left = sine(0.2f, 60.0f, kN);
    auto right = sine(0.2f, 8000.0f, kN);
    auto [out_l, out_r] = render_stereo(h, left, right);
    REQUIRE(v::check_finite(out_l));
    REQUIRE(v::check_finite(out_r));
    const double gain_l = rms_tail(out_l) / rms_tail(left);
    const double gain_r = rms_tail(out_r) / rms_tail(right);
    REQUIRE(gain_l > 1.5);                         // left's low tone boosted
    REQUIRE(gain_r > 0.9); REQUIRE(gain_r < 1.1);  // right's high tone untouched
    // A channel-0-only or cross-contaminating impl could not produce both.
}

TEST_CASE("Parametric EQ mid Q controls cut bandwidth", "[eq]") {
    auto offcenter_gain = [](float q) {
        format::HeadlessHost h(create_parametric_eq);
        h.prepare(48000.0, kN);
        h.state().set_value(kMidFreq, 1000.0f);
        h.state().set_value(kMidGain, -12.0f);
        h.state().set_value(kMidQ, q);
        return gain_at(h, 1600.0f);   // ~2/3 octave above centre
    };
    const double wide = offcenter_gain(0.5f);   // broad cut reaches 1600 Hz
    const double narrow = offcenter_gain(8.0f);  // narrow cut barely touches it
    REQUIRE(wide < narrow);                       // wider Q attenuates more off-centre
    REQUIRE(narrow > 0.9);                         // narrow cut leaves 1600 Hz ~alone
}

TEST_CASE("Parametric EQ applies all three bands at once", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    h.state().set_value(kLowFreq, 200.0f);  h.state().set_value(kLowGain, 12.0f);
    h.state().set_value(kMidFreq, 1000.0f); h.state().set_value(kMidQ, 2.0f);
    h.state().set_value(kMidGain, -12.0f);
    h.state().set_value(kHighFreq, 5000.0f); h.state().set_value(kHighGain, 12.0f);
    REQUIRE(gain_at(h, 60.0f) > 1.5);     // low boosted
    REQUIRE(gain_at(h, 1000.0f) < 0.6);   // mid cut
    REQUIRE(gain_at(h, 10000.0f) > 1.5);  // high boosted
}

TEST_CASE("Parametric EQ bypass passes through unchanged", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, 512);
    h.state().set_value(kLowGain, 12.0f);        // would change sound if not bypassed
    h.state().set_value(kEqBypass, 1.0f);
    auto in = sine(0.5f, 120.0f, 512);
    auto out = render(h, in);
    for (int n = 0; n < 512; ++n) REQUIRE(std::fabs(out[n] - in[n]) < 1e-6f);
}
