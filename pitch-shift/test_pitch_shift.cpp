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
constexpr int kN = 24000;       // long enough for the vocoder to settle
constexpr int kTail0 = 10000;   // measure only the settled tail

// Render a mono signal as stereo through the processor; return channel 0.
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

std::vector<float> sine(float amp, float hz, int n, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}

// Dominant period (samples) via normalized autocorrelation over the settled
// tail. The correlation is normalized by the term count so the argmax isn't
// biased toward smaller lags. An upward scan with strict > returns the smallest
// lag on ties, so the fundamental wins over its 2x harmonic.
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

float peak_abs(const std::vector<float>& x, int from) {
    float m = 0.0f;
    for (int n = from; n < (int)x.size(); ++n) m = std::max(m, std::fabs(x[n]));
    return m;
}

// Prepare a host with an explicit STFT config (combo indices).
void setup(format::HeadlessHost& h, float shift, int fft_idx, int hop_idx, int win_idx) {
    h.prepare(48000.0, kN);
    h.state().set_value(kShift, shift);
    h.state().set_value(kPsFftSize, (float)fft_idx);
    h.state().set_value(kPsHop, (float)hop_idx);
    h.state().set_value(kPsWindow, (float)win_idx);
}
}  // namespace

TEST_CASE("Pitch shift up an octave halves the period (~2x frequency)", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    setup(h, 12.0f, /*1024*/2, /*1/8*/2, /*Hann*/1);
    auto out = render(h, sine(0.3f, 250.0f, kN));   // input period = 192 samples
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    const int p = detect_period(out, 70, 140);      // expect ~96
    REQUIRE(p > 88); REQUIRE(p < 104);
}

TEST_CASE("Pitch shift down an octave doubles the period (~0.5x frequency)", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    setup(h, -12.0f, /*1024*/2, /*1/8*/2, /*Hann*/1);
    auto out = render(h, sine(0.3f, 250.0f, kN));
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    // Search includes the ORIGINAL period (192) so a broken/passthrough output
    // (which peaks at 192) cannot masquerade as the doubled period. A correct
    // 384-period output anti-correlates at 192, so 384 wins.
    const int p = detect_period(out, 150, 480);     // expect ~384, not 192
    REQUIRE(p > 360); REQUIRE(p < 410);
}

TEST_CASE("Pitch shift by a non-octave interval (+7 st) lands on the right period", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    setup(h, 7.0f, /*1024*/2, /*1/8*/2, /*Hann*/1);   // ratio snaps to ~1.5 -> period ~128
    auto out = render(h, sine(0.3f, 250.0f, kN));      // input period 192
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    // 128 is neither 192 nor a simple multiple, so this can't be fooled by a
    // harmonic of the original — the strongest discriminator of the set.
    const int p = detect_period(out, 100, 170);
    REQUIRE(p > 118); REQUIRE(p < 140);
}

TEST_CASE("Pitch shift at 0 semitones preserves pitch and is bounded (~identity)", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    setup(h, 0.0f, /*1024*/2, /*1/8*/2, /*Hann*/1);
    const float amp = 0.3f;
    auto out = render(h, sine(amp, 250.0f, kN));        // input period 192
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    // Pitch unchanged: dominant period stays ~192.
    const int p = detect_period(out, 150, 260);
    REQUIRE(p > 178); REQUIRE(p < 206);
    // Roughly unity gain (COLA reconstruction), and never blows up.
    const float pk = peak_abs(out, kTail0);
    REQUIRE(pk > 0.5f * amp);
    REQUIRE(pk < 2.0f);
}

TEST_CASE("Pitch shift processes both channels identically", "[pitchshift]") {
    format::HeadlessHost h(create_pitch_shift);
    setup(h, 5.0f, /*512*/1, /*1/8*/2, /*Hann*/1);
    auto [l, r] = render_both(h, sine(0.3f, 300.0f, kN));
    REQUIRE(v::check_finite(l));
    REQUIRE(v::check_any_nonzero(r));                   // channel 1 is actually processed
    for (int n = 0; n < kN; ++n) REQUIRE(std::fabs(l[n] - r[n]) < 1e-5f);
}

TEST_CASE("Pitch shift is finite and bounded across every FFT size and window", "[pitchshift]") {
    for (int fft_idx = 0; fft_idx < (int)kPitchFftSizes.size(); ++fft_idx) {
        for (int win_idx = 0; win_idx < 3; ++win_idx) {
            format::HeadlessHost h(create_pitch_shift);
            setup(h, 12.0f, fft_idx, /*1/4*/1, win_idx);
            auto out = render(h, sine(0.3f, 250.0f, kN));
            REQUIRE(v::check_finite(out));
            REQUIRE(v::check_any_nonzero(out));
            REQUIRE(peak_abs(out, 0) < 4.0f);
        }
    }
}

TEST_CASE("Pitch shift honors the Hop (overlap) combo", "[pitchshift]") {
    for (int hop_idx = 0; hop_idx < (int)kPitchOverlaps.size(); ++hop_idx) {
        format::HeadlessHost h(create_pitch_shift);
        setup(h, 12.0f, /*1024*/2, hop_idx, /*Hann*/1);
        auto out = render(h, sine(0.3f, 250.0f, kN));
        REQUIRE(v::check_finite(out));
        REQUIRE(v::check_any_nonzero(out));
        const int p = detect_period(out, 70, 140);     // octave up -> ~96 at every overlap
        REQUIRE(p > 84); REQUIRE(p < 108);
    }
}
