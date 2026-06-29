#include <catch2/catch_test_macros.hpp>
#include "wah.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
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
double gain_at(format::HeadlessHost& h, float hz, float amp) {
    auto in = sine(amp, hz, kN);
    auto out = render(h, in);
    REQUIRE(v::check_finite(out));
    return rms_tail(out) / rms_tail(in);
}
}

TEST_CASE("Wah manual mode centres the resonance on the Freq param", "[wah]") {
    // Filter centred at 400 Hz: a 400 Hz tone passes; centred at 2000 Hz it is
    // off-band and attenuated. Proves the bandpass centre tracks the param.
    double on, off;
    {
        format::HeadlessHost h(create_wah);
        h.prepare(48000.0, kN);
        h.state().set_value(kWahMode, 0.0f);   // manual
        h.state().set_value(kWahResonance, 6.0f);
        h.state().set_value(kWahFreq, 400.0f);
        on = gain_at(h, 400.0f, 0.2f);
    }
    {
        format::HeadlessHost h(create_wah);
        h.prepare(48000.0, kN);
        h.state().set_value(kWahMode, 0.0f);
        h.state().set_value(kWahResonance, 6.0f);
        h.state().set_value(kWahFreq, 2000.0f);
        off = gain_at(h, 400.0f, 0.2f);
    }
    REQUIRE(off < 0.5);        // 400 Hz rejected when centred at 2 kHz
    REQUIRE(on > 2.0 * off);   // and clearly passed when centred at 400 Hz
}

TEST_CASE("Wah resonance raises the peak at the centre frequency", "[wah]") {
    double low_q, high_q;
    {
        format::HeadlessHost h(create_wah);
        h.prepare(48000.0, kN);
        h.state().set_value(kWahMode, 0.0f);
        h.state().set_value(kWahFreq, 1000.0f);
        h.state().set_value(kWahResonance, 2.0f);
        low_q = gain_at(h, 1000.0f, 0.2f);
    }
    {
        format::HeadlessHost h(create_wah);
        h.prepare(48000.0, kN);
        h.state().set_value(kWahMode, 0.0f);
        h.state().set_value(kWahFreq, 1000.0f);
        h.state().set_value(kWahResonance, 12.0f);
        high_q = gain_at(h, 1000.0f, 0.2f);
    }
    REQUIRE(high_q > 2.0 * low_q);   // higher resonance => clearly taller peak
}

// Negative control for the Mode switch: manual mode must ignore input level
// (and Sensitivity). If a regression made the wah always-envelope, this fails.
TEST_CASE("Wah manual mode is level-independent (mode switch matters)", "[wah]") {
    auto man_gain = [](float amp) {
        format::HeadlessHost h(create_wah);
        h.prepare(48000.0, kN);
        h.state().set_value(kWahMode, 0.0f);            // manual
        h.state().set_value(kWahFreq, 400.0f);
        h.state().set_value(kWahSensitivity, 3000.0f);  // must be IGNORED in manual
        h.state().set_value(kWahResonance, 6.0f);
        return gain_at(h, 2000.0f, amp);
    };
    const double quiet = man_gain(0.02f);
    const double loud = man_gain(0.5f);
    REQUIRE(std::fabs(loud - quiet) < 0.05 * quiet + 1e-6);  // level has no effect
}

// Envelope mode at rest parks the centre at the base frequency: a tiny input
// should behave like manual-at-base (the 2 kHz probe stays off-band/low).
TEST_CASE("Wah envelope mode rests at the base frequency", "[wah]") {
    auto probe = [](bool envelope) {
        format::HeadlessHost h(create_wah);
        h.prepare(48000.0, kN);
        h.state().set_value(kWahMode, envelope ? 1.0f : 0.0f);
        h.state().set_value(kWahFreq, 400.0f);
        h.state().set_value(kWahSensitivity, 3000.0f);
        h.state().set_value(kWahResonance, 6.0f);
        return gain_at(h, 2000.0f, 0.02f);   // tiny input: env ~ 0
    };
    const double env_rest = probe(true);
    const double manual = probe(false);
    REQUIRE(env_rest < 2.0 * manual + 0.05);  // at rest ~ manual at base
}

TEST_CASE("Wah stays stable at maximum resonance", "[wah]") {
    format::HeadlessHost h(create_wah);
    h.prepare(48000.0, kN);
    h.state().set_value(kWahMode, 1.0f);
    h.state().set_value(kWahResonance, 20.0f);     // param max
    h.state().set_value(kWahSensitivity, 4000.0f);
    auto out = render(h, sine(0.5f, 1000.0f, kN));
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 12.0f));       // resonant but bounded
}

TEST_CASE("Wah envelope mode sweeps the centre with input level (auto-wah)", "[wah]") {
    // Probe at 2 kHz with base=400, sensitivity=3000. A quiet input keeps the
    // centre near 400 (2 kHz off-band, low gain); a loud input sweeps the centre
    // up toward ~1900 (2 kHz near the peak, higher gain). A level-independent
    // filter would give the same gain for both — this is the auto-wah behavior.
    auto env_gain = [](float amp) {
        format::HeadlessHost h(create_wah);
        h.prepare(48000.0, kN);
        h.state().set_value(kWahMode, 1.0f);   // envelope
        h.state().set_value(kWahFreq, 400.0f);
        h.state().set_value(kWahSensitivity, 3000.0f);
        h.state().set_value(kWahResonance, 6.0f);
        return gain_at(h, 2000.0f, amp);
    };
    const double quiet = env_gain(0.02f);
    const double loud  = env_gain(0.5f);
    REQUIRE(loud > 1.5 * quiet);   // level drives the centre frequency up
}

TEST_CASE("Wah mix=0 is dry; bypass passes through", "[wah]") {
    format::HeadlessHost h(create_wah);
    h.prepare(48000.0, 512);
    h.state().set_value(kWahMode, 0.0f);
    h.state().set_value(kWahMix, 0.0f);
    auto in = sine(0.4f, 700.0f, 512);
    {
        auto dry = render(h, in);
        for (int n = 0; n < 512; ++n) REQUIRE(std::fabs(dry[n] - in[n]) < 1e-6f);
    }
    h.state().set_value(kWahMix, 100.0f);
    h.state().set_value(kWahBypass, 1.0f);
    auto out = render(h, in);
    for (int n = 0; n < 512; ++n) REQUIRE(std::fabs(out[n] - in[n]) < 1e-6f);
}
