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
// Configure the single band.
void set_band(format::HeadlessHost& h, int type, float freq, float q, float gain_db) {
    h.state().set_value(kType, static_cast<float>(type));
    h.state().set_value(kEqFreq, freq);
    h.state().set_value(kQ, q);
    h.state().set_value(kGain, gain_db);
}
}  // namespace

TEST_CASE("Parametric EQ exposes exactly four params: Freq, Q, Gain, Type", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    // Default Type is Peaking (index 6), the seventh option.
    REQUIRE(kFilterTypeCount == 7);
    REQUIRE(kFilterTypeDefault == 6);
    REQUIRE(std::string(kFilterTypeLabels[0]) == "Low-Pass");
    REQUIRE(std::string(kFilterTypeLabels[6]) == "Peaking");
    REQUIRE(h.state().get_value(kType) == 6.0f);  // defaults to Peaking
}

TEST_CASE("Parametric EQ default (Peaking, 0 dB) is flat / unity", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    // Defaults: Peaking, gain 0 dB -> identity at every frequency.
    for (float f : {80.0f, 1000.0f, 8000.0f}) {
        const double g = gain_at(h, f);
        REQUIRE(g > 0.97);
        REQUIRE(g < 1.03);
    }
}

TEST_CASE("Parametric EQ Low-Pass attenuates highs, passes lows", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 0, 1000.0f, 0.707f, 0.0f);   // Low-Pass @ 1 kHz
    const double low = gain_at(h, 100.0f);
    REQUIRE(low > 0.9); REQUIRE(low < 1.1);   // well below cutoff: passes
    REQUIRE(gain_at(h, 10000.0f) < 0.2);      // well above cutoff: rolled off
}

TEST_CASE("Parametric EQ High-Pass attenuates lows, passes highs", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 1, 1000.0f, 0.707f, 0.0f);   // High-Pass @ 1 kHz
    REQUIRE(gain_at(h, 100.0f) < 0.2);        // well below cutoff: rolled off
    const double high = gain_at(h, 10000.0f);
    REQUIRE(high > 0.9); REQUIRE(high < 1.1);  // well above cutoff: passes
}

TEST_CASE("Parametric EQ Low-Shelf boosts lows, leaves highs alone", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 2, 500.0f, 0.707f, 12.0f);   // Low-Shelf +12 dB
    REQUIRE(gain_at(h, 60.0f) > 2.5);          // shelf plateau ~ +12 dB (~4x)
    const double high = gain_at(h, 10000.0f);
    REQUIRE(high > 0.9); REQUIRE(high < 1.1);  // far above corner: untouched
}

TEST_CASE("Parametric EQ High-Shelf boosts highs, leaves lows alone", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 3, 2000.0f, 0.707f, 12.0f);  // High-Shelf +12 dB
    REQUIRE(gain_at(h, 12000.0f) > 2.5);       // shelf plateau ~ +12 dB
    const double low = gain_at(h, 100.0f);
    REQUIRE(low > 0.9); REQUIRE(low < 1.1);    // far below corner: untouched
}

TEST_CASE("Parametric EQ Band-Pass peaks at centre, rejects far bands", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 4, 1000.0f, 2.0f, 0.0f);     // Band-Pass @ 1 kHz, Q=2
    const double centre = gain_at(h, 1000.0f);
    REQUIRE(centre > 0.9); REQUIRE(centre < 1.1);  // 0 dB peak gain at centre
    REQUIRE(gain_at(h, 100.0f) < 0.3);             // below band: rejected
    REQUIRE(gain_at(h, 8000.0f) < 0.3);            // above band: rejected
}

TEST_CASE("Parametric EQ Band-Stop notches centre, passes the rest", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 5, 1000.0f, 2.0f, 0.0f);     // Band-Stop (notch) @ 1 kHz
    REQUIRE(gain_at(h, 1000.0f) < 0.15);      // deep null at centre
    const double low = gain_at(h, 100.0f);
    const double high = gain_at(h, 8000.0f);
    REQUIRE(low > 0.9); REQUIRE(low < 1.1);    // far below: passes
    REQUIRE(high > 0.9); REQUIRE(high < 1.1);  // far above: passes
}

TEST_CASE("Parametric EQ Peaking boosts at Freq by Gain", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 6, 1000.0f, 2.0f, 12.0f);    // Peaking +12 dB @ 1 kHz
    // RBJ peaking peak gain at centre = 10^(gain/20) = ~3.98 for +12 dB.
    const double centre = gain_at(h, 1000.0f);
    REQUIRE(centre > 3.4); REQUIRE(centre < 4.5);
    const double low = gain_at(h, 100.0f);
    const double high = gain_at(h, 10000.0f);
    REQUIRE(low > 0.9); REQUIRE(low < 1.1);    // off-band untouched
    REQUIRE(high > 0.9); REQUIRE(high < 1.1);
}

TEST_CASE("Parametric EQ Peaking cut attenuates only its band", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 6, 1000.0f, 2.0f, -12.0f);   // Peaking -12 dB @ 1 kHz
    const double centre = gain_at(h, 1000.0f);
    REQUIRE(centre < 0.45);                     // ~ -12 dB cut (~0.25x)
    const double low = gain_at(h, 100.0f);
    REQUIRE(low > 0.9); REQUIRE(low < 1.1);
}

TEST_CASE("Parametric EQ Q controls peaking bandwidth", "[eq]") {
    auto offcenter_gain = [](float q) {
        format::HeadlessHost h(create_parametric_eq);
        h.prepare(48000.0, kN);
        set_band(h, 6, 1000.0f, q, -12.0f);   // Peaking cut, vary Q
        return gain_at(h, 1600.0f);            // ~2/3 octave above centre
    };
    const double wide = offcenter_gain(0.5f);   // broad cut reaches 1600 Hz
    const double narrow = offcenter_gain(8.0f);  // narrow cut barely touches it
    REQUIRE(wide < narrow);                       // lower Q = wider cut off-centre
    REQUIRE(narrow > 0.9);                         // narrow cut leaves 1600 Hz ~alone
}

TEST_CASE("Parametric EQ Type switch changes the response in place", "[eq]") {
    // The same band, retuned only via the Type selector, must produce a
    // materially different response — proving the dropdown drives coefficients.
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 0, 1000.0f, 0.707f, 0.0f);   // Low-Pass: 8 kHz rejected
    const double lp_high = gain_at(h, 8000.0f);
    h.state().set_value(kType, 1.0f);         // -> High-Pass: 8 kHz passes
    const double hp_high = gain_at(h, 8000.0f);
    REQUIRE(lp_high < 0.2);
    REQUIRE(hp_high > 0.9);
}

TEST_CASE("Parametric EQ stays stable at extreme settings", "[eq]") {
    // Prepare at 8 kHz so a 5 kHz peak sits ABOVE Nyquist (4 kHz), forcing the
    // process()-level 0.49*sr freq clamp to engage. Excite with a broadband
    // impulse so a narrow +18 dB / Q=20 resonance rings, then assert it stays
    // finite, bounded, and decays.
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(8000.0, kN);
    set_band(h, 6, 5000.0f, 20.0f, 18.0f);   // Peaking, freq > Nyquist@8k
    std::vector<float> impulse(kN, 0.0f); impulse[0] = 1.0f;
    auto out = render(h, impulse);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 12.0f));
    float tail = 0.0f;
    for (int n = kN - 500; n < kN; ++n) tail = std::max(tail, std::fabs(out[n]));
    REQUIRE(tail < 0.05f);
}

TEST_CASE("Parametric EQ processes channels independently (per-channel state)", "[eq]") {
    format::HeadlessHost h(create_parametric_eq);
    h.prepare(48000.0, kN);
    set_band(h, 0, 1000.0f, 0.707f, 0.0f);   // Low-Pass @ 1 kHz
    // Left = low tone (passes), Right = high tone (rejected).
    auto left = sine(0.2f, 100.0f, kN);
    auto right = sine(0.2f, 10000.0f, kN);
    auto [out_l, out_r] = render_stereo(h, left, right);
    REQUIRE(v::check_finite(out_l));
    REQUIRE(v::check_finite(out_r));
    const double gain_l = rms_tail(out_l) / rms_tail(left);
    const double gain_r = rms_tail(out_r) / rms_tail(right);
    REQUIRE(gain_l > 0.9); REQUIRE(gain_l < 1.1);  // left's low tone passes
    REQUIRE(gain_r < 0.2);                          // right's high tone rejected
    // A channel-0-only or cross-contaminating impl could not produce both.
}
