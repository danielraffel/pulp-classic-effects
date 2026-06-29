#include <catch2/catch_test_macros.hpp>
#include "pitch_shift.hpp"
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
constexpr int kN = 12000;
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
// Dominant period (samples) via autocorrelation over the settled tail. The
// correlation is normalized by the term count so the argmax isn't biased toward
// smaller lags. Search [lo, hi]; an upward scan with strict > returns the
// smallest lag on ties (so the original period wins over its 2x harmonic when
// the signal is unshifted — which makes the octave-down test discriminating).
int detect_period(const std::vector<float>& x, int lo, int hi) {
    double best = -1e30; int best_lag = lo;
    for (int lag = lo; lag <= hi; ++lag) {
        double acc = 0.0; int c = 0;
        for (int n = kTail0; n + lag < (int)x.size(); ++n) { acc += (double)x[n] * x[n + lag]; ++c; }
        const double norm = (c > 0) ? acc / c : 0.0;
        if (norm > best) { best = norm; best_lag = lag; }
    }
    return best_lag;
}
std::pair<std::vector<float>, std::vector<float>> render_both(
    format::HeadlessHost& h, const std::vector<float>& mono) {
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
}

TEST_CASE("Pitch shift up an octave halves the period (~2x frequency)", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    h.prepare(48000.0, kN);
    h.state().set_value(kPitchSemitones, 12.0f);   // +1 octave
    h.state().set_value(kPitchMix, 100.0f);
    auto out = render(h, sine(0.3f, 250.0f, kN));   // input period = 192 samples
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    const int p = detect_period(out, 70, 140);      // expect ~96
    REQUIRE(p > 88); REQUIRE(p < 104);
}

TEST_CASE("Pitch shift down an octave doubles the period (~0.5x frequency)", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    h.prepare(48000.0, kN);
    h.state().set_value(kPitchSemitones, -12.0f);  // -1 octave
    h.state().set_value(kPitchMix, 100.0f);
    auto out = render(h, sine(0.3f, 250.0f, kN));
    REQUIRE(v::check_finite(out));
    // Search includes the ORIGINAL period (192) so a broken/passthrough output
    // (which peaks at 192) cannot masquerade as the doubled period. A correct
    // 384-period output anti-correlates at 192, so 384 wins.
    const int p = detect_period(out, 150, 480);     // expect ~384, not 192
    REQUIRE(p > 360); REQUIRE(p < 410);
}

TEST_CASE("Pitch shift by a non-octave interval (+7 st) lands on the right period", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    h.prepare(48000.0, kN);
    h.state().set_value(kPitchSemitones, 7.0f);     // ratio ~1.498 -> period ~128
    h.state().set_value(kPitchMix, 100.0f);
    auto out = render(h, sine(0.3f, 250.0f, kN));    // input period 192
    REQUIRE(v::check_finite(out));
    // 128 is neither 192 nor a simple multiple, so this can't be fooled by a
    // harmonic of the original — the strongest discriminator of the set.
    const int p = detect_period(out, 100, 170);
    REQUIRE(p > 120); REQUIRE(p < 138);
}

TEST_CASE("Pitch shift processes both channels identically (shared phase)", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    h.prepare(48000.0, kN);
    h.state().set_value(kPitchSemitones, 5.0f);
    h.state().set_value(kPitchMix, 100.0f);
    auto [l, r] = render_both(h, sine(0.3f, 300.0f, kN));
    REQUIRE(v::check_finite(l));
    REQUIRE(v::check_any_nonzero(r));               // channel 1 is actually processed
    for (int n = 0; n < kN; ++n) REQUIRE(std::fabs(l[n] - r[n]) < 1e-6f);  // shared phase
}

TEST_CASE("Pitch shift at 0 semitones is exact passthrough", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    h.prepare(48000.0, 1024);
    h.state().set_value(kPitchSemitones, 0.0f);
    h.state().set_value(kPitchMix, 100.0f);
    auto in = sine(0.4f, 440.0f, 1024);
    auto out = render(h, in);
    for (int n = 0; n < 1024; ++n) REQUIRE(std::fabs(out[n] - in[n]) < 1e-6f);
}

TEST_CASE("Pitch shift mix=0 is dry; bypass passes through", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    h.prepare(48000.0, 1024);
    h.state().set_value(kPitchSemitones, 7.0f);    // would shift if not dry/bypassed
    h.state().set_value(kPitchMix, 0.0f);
    auto in = sine(0.4f, 440.0f, 1024);
    {
        auto dry = render(h, in);
        for (int n = 0; n < 1024; ++n) REQUIRE(std::fabs(dry[n] - in[n]) < 1e-6f);
    }
    h.state().set_value(kPitchMix, 100.0f);
    h.state().set_value(kPitchBypass, 1.0f);
    auto out = render(h, in);
    for (int n = 0; n < 1024; ++n) REQUIRE(std::fabs(out[n] - in[n]) < 1e-6f);
}
