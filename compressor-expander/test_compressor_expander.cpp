#include <catch2/catch_test_macros.hpp>
#include "compressor_expander.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <utility>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_compressor_expander;
using pulp::examples::classic::kMode;
using pulp::examples::classic::kThreshold;
using pulp::examples::classic::kRatio;
using pulp::examples::classic::kAttack;
using pulp::examples::classic::kRelease;
using pulp::examples::classic::kMakeup;
using pulp::examples::classic::kCxBypass;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kN = 24000;          // 0.5 s @ 48k
constexpr int kTail0 = 16000;      // measure after the envelope settles

// Mode indices, matching the Mode dropdown labels in the editor.
constexpr float kCompressor = 0.0f;
constexpr float kExpander   = 1.0f;

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
// Configure the shared dynamics knobs (seconds for attack/release, per truce).
void configure(format::HeadlessHost& h, float mode, float thr, float ratio,
               float attack_s = 0.005f, float release_s = 0.2f, float makeup = 0.0f) {
    h.state().set_value(kMode, mode);
    h.state().set_value(kThreshold, thr);
    h.state().set_value(kRatio, ratio);
    h.state().set_value(kAttack, attack_s);
    h.state().set_value(kRelease, release_s);
    h.state().set_value(kMakeup, makeup);
    h.state().set_value(kCxBypass, 0.0f);
}
}

TEST_CASE("Compressor mode attenuates loud signals but leaves quiet ones alone", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, kN);
    configure(h, kCompressor, /*thr=*/-18.0f, /*ratio=*/4.0f);

    const double quiet = steady_gain(h, 0.05f);   // ~-26 dB, below threshold
    const double loud  = steady_gain(h, 0.5f);    // ~-6 dB, above threshold

    REQUIRE(quiet > 0.9);     // below threshold: ~unity
    REQUIRE(loud < 0.85);     // above threshold: compressed down
    REQUIRE(loud < quiet);    // level-dependent — a fixed gain can't do this
}

TEST_CASE("Expander mode attenuates below the threshold", "[compexp]") {
    // The expander detector integrates the squared level slowly (~0.2 s memory,
    // the book's anti-pumping average), so each amplitude needs its own freshly
    // reset host to settle to the right level within the measurement window.
    auto gain_at = [](float amp) {
        format::HeadlessHost h(create_compressor_expander);
        h.prepare(48000.0, kN);
        configure(h, kExpander, /*thr=*/-30.0f, /*ratio=*/3.0f);
        return steady_gain(h, amp);
    };
    const double moderate = gain_at(0.1f);   // ~-23 dB, above exp threshold
    const double tiny     = gain_at(0.01f);  // ~-43 dB, below exp threshold

    REQUIRE(moderate > 0.85);  // above exp threshold: ~unity
    REQUIRE(tiny < 0.5);       // below exp threshold: expanded down
    REQUIRE(tiny < moderate);
}

TEST_CASE("Compressor mode does not attenuate below the threshold", "[compexp]") {
    // The complement of the compressor test: a level under the threshold must
    // pass through near unity, proving the curve compresses ABOVE, not below.
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, kN);
    configure(h, kCompressor, /*thr=*/-6.0f, /*ratio=*/8.0f);
    const double quiet = steady_gain(h, 0.05f);    // ~-26 dB, well below -6 dB
    REQUIRE(quiet > 0.95);
}

TEST_CASE("Compressor/Expander applies makeup gain", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, kN);
    // Threshold at 0 dB so a -20 dB tone never crosses it; only makeup applies.
    configure(h, kCompressor, /*thr=*/0.0f, /*ratio=*/1.0f,
              /*attack=*/0.005f, /*release=*/0.2f, /*makeup=*/6.0f);  // +6 dB ~= x1.995

    const double g = steady_gain(h, 0.1f);
    REQUIRE(g > 1.9);
    REQUIRE(g < 2.1);
}

// Ballistics: a fast attack must reduce gain sooner than a slow attack. Without
// this, an instantaneous (no-smoothing) detector — the whole point of a
// compressor vs a waveshaper — would pass the steady-state tests.
TEST_CASE("Compressor attack time governs how fast gain reduction engages", "[compexp]") {
    auto onset_to_half = [](float attack_s) {
        format::HeadlessHost h(create_compressor_expander);
        h.prepare(48000.0, 12000);
        configure(h, kCompressor, /*thr=*/-18.0f, /*ratio=*/4.0f,
                  /*attack=*/attack_s, /*release=*/0.2f);
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
    const int t_fast = onset_to_half(0.001f);   // 1 ms attack
    const int t_slow = onset_to_half(0.050f);   // 50 ms attack
    REQUIRE(t_fast > 0);
    REQUIRE(t_slow > 2 * t_fast);               // slow attack engages much later
}

// Stereo link: the detector keys off a mono mixdown and applies ONE gain to
// both channels, so a quiet channel is attenuated alongside a loud one. A
// per-channel detector would leave the quiet channel near unity.
TEST_CASE("Compressor is stereo-linked (quiet channel follows the loud one)", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, kN);
    configure(h, kCompressor, /*thr=*/-18.0f, /*ratio=*/4.0f);

    auto loud_l = sine(0.5f, 300.0f, kN);    // ~-6 dB, above threshold
    auto quiet_r = sine(0.02f, 300.0f, kN);  // ~-34 dB, far below threshold alone
    auto [out_l, out_r] = render_stereo(h, loud_l, quiet_r);
    REQUIRE(v::check_finite(out_l));
    REQUIRE(v::check_finite(out_r));

    const double gain_l = rms_tail(out_l) / rms_tail(loud_l);
    const double gain_r = rms_tail(out_r) / rms_tail(quiet_r);
    REQUIRE(gain_l < 0.9);                         // loud channel compressed
    REQUIRE(gain_r < 0.9);                         // quiet channel pulled down too
    REQUIRE(std::fabs(gain_l - gain_r) < 0.05);    // same gain on both channels
}

TEST_CASE("Compressor/Expander bypass passes through unchanged", "[compexp]") {
    format::HeadlessHost h(create_compressor_expander);
    h.prepare(48000.0, 512);
    h.state().set_value(kMode, kCompressor);
    h.state().set_value(kThreshold, -40.0f);  // would compress hard if engaged
    h.state().set_value(kRatio, 20.0f);
    h.state().set_value(kCxBypass, 1.0f);
    auto in = sine(0.7f, 500.0f, 512);
    auto out = render(h, in);
    for (int n = 0; n < 512; ++n) REQUIRE(std::fabs(out[n] - in[n]) < 1e-6f);
}
