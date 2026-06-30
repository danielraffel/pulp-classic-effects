#include <catch2/catch_test_macros.hpp>
#include "ring_mod.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_ring_mod;
using pulp::examples::classic::kRmDepth;
using pulp::examples::classic::kRmFreq;
using pulp::examples::classic::kRmWaveform;
using pulp::examples::classic::Waveform;
namespace v = pulp::format::validation;

namespace {

constexpr double kSr = 48000.0;
constexpr float kTwoPi = 6.283185307179586f;

// Render `frames` of a unit DC input through the processor (channel 0 result).
std::vector<float> render_dc(format::HeadlessHost& h, int frames) {
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) { in.channel(0)[n] = 1.0f; in.channel(1)[n] = 1.0f; }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});
    std::vector<float> r(frames);
    for (int n = 0; n < frames; ++n) r[n] = out.channel(0)[n];
    return r;
}

// Render `frames` of a sine input at `f_in` Hz (channel 0 result).
std::vector<float> render_sine(format::HeadlessHost& h, int frames, float f_in) {
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) {
        const float s = std::sin(kTwoPi * f_in * static_cast<float>(n) / static_cast<float>(kSr));
        in.channel(0)[n] = s; in.channel(1)[n] = s;
    }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});
    std::vector<float> r(frames);
    for (int n = 0; n < frames; ++n) r[n] = out.channel(0)[n];
    return r;
}

// Single-bin DFT magnitude (Goertzel-style) of `x` at frequency `f` Hz.
double bin_magnitude(const std::vector<float>& x, double f) {
    double re = 0.0, im = 0.0;
    const double w = kTwoPi * f / kSr;
    for (std::size_t n = 0; n < x.size(); ++n) {
        re += x[n] * std::cos(w * static_cast<double>(n));
        im += x[n] * std::sin(w * static_cast<double>(n));
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(x.size());
}

} // namespace

TEST_CASE("RingMod multiplies a DC input by the carrier", "[ring-mod]") {
    format::HeadlessHost h(create_ring_mod);
    h.prepare(kSr, 1024);
    h.state().set_value(kRmFreq, 200.0f);
    h.state().set_value(kRmDepth, 1.0f);
    h.state().set_value(kRmWaveform, static_cast<float>(Waveform::Sine));
    auto out = render_dc(h, 1024);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 1.001f));
    // DC * bipolar carrier traces the carrier itself: it must swing both ways.
    const float lo = *std::min_element(out.begin(), out.end());
    const float hi = *std::max_element(out.begin(), out.end());
    REQUIRE(lo < -0.5f);
    REQUIRE(hi > 0.5f);
}

TEST_CASE("RingMod produces sum/difference sidebands", "[ring-mod]") {
    constexpr int kN = 8192;
    constexpr float kFin = 1000.0f, kFc = 200.0f;  // sidebands at 800 / 1200 Hz

    // Dry reference: depth=0 leaves the input fundamental intact.
    format::HeadlessHost dry(create_ring_mod);
    dry.prepare(kSr, kN);
    dry.state().set_value(kRmFreq, kFc);
    dry.state().set_value(kRmDepth, 0.0f);
    dry.state().set_value(kRmWaveform, static_cast<float>(Waveform::Sine));
    auto dry_out = render_sine(dry, kN, kFin);
    const double dry_fund = bin_magnitude(dry_out, kFin);
    REQUIRE(dry_fund > 0.4);  // ~0.5 for a unit sine

    // Full ring mod with a sine carrier: in*c moves all energy into the two
    // sidebands and nulls the input fundamental.
    format::HeadlessHost wet(create_ring_mod);
    wet.prepare(kSr, kN);
    wet.state().set_value(kRmFreq, kFc);
    wet.state().set_value(kRmDepth, 1.0f);
    wet.state().set_value(kRmWaveform, static_cast<float>(Waveform::Sine));
    auto wet_out = render_sine(wet, kN, kFin);

    const double fund = bin_magnitude(wet_out, kFin);
    const double lower = bin_magnitude(wet_out, kFin - kFc);
    const double upper = bin_magnitude(wet_out, kFin + kFc);

    REQUIRE(v::check_finite(wet_out));
    REQUIRE(lower > 0.2);            // difference sideband present
    REQUIRE(upper > 0.2);            // sum sideband present
    REQUIRE(fund < 0.05 * dry_fund); // carrier nulls the fundamental
}

TEST_CASE("RingMod depth blends dry and modulated", "[ring-mod]") {
    constexpr int kN = 8192;
    constexpr float kFin = 1000.0f, kFc = 200.0f;

    auto fundamental_at = [&](float depth) {
        format::HeadlessHost h(create_ring_mod);
        h.prepare(kSr, kN);
        h.state().set_value(kRmFreq, kFc);
        h.state().set_value(kRmDepth, depth);
        h.state().set_value(kRmWaveform, static_cast<float>(Waveform::Sine));
        return bin_magnitude(render_sine(h, kN, kFin), kFin);
    };

    const double f0 = fundamental_at(0.0f);   // pure dry
    const double f_half = fundamental_at(0.5f);
    const double f1 = fundamental_at(1.0f);   // fully modulated

    // Depth crossfades the dry fundamental: full at 0, ~half at 0.5, gone at 1.
    REQUIRE(f_half < f0 * 0.8);
    REQUIRE(f_half > f1 + 0.1 * f0);
    REQUIRE(std::fabs(f_half - 0.5 * f0) < 0.1 * f0);
}

TEST_CASE("RingMod dry passthrough at depth=0", "[ring-mod]") {
    format::HeadlessHost h(create_ring_mod);
    h.prepare(kSr, 256);
    h.state().set_value(kRmDepth, 0.0f);
    h.state().set_value(kRmFreq, 200.0f);
    for (float s : render_dc(h, 256)) REQUIRE(std::fabs(s - 1.0f) < 1e-5f);
}

TEST_CASE("RingMod carrier waveforms differ", "[ring-mod]") {
    const int kN = 1024;
    const int count = static_cast<int>(Waveform::Count);
    std::vector<std::vector<float>> renders;
    for (int w = 0; w < count; ++w) {
        format::HeadlessHost h(create_ring_mod);   // fresh host resets carrier phase
        h.prepare(kSr, kN);
        h.state().set_value(kRmFreq, 200.0f);
        h.state().set_value(kRmDepth, 1.0f);
        h.state().set_value(kRmWaveform, static_cast<float>(w));
        auto out = render_dc(h, kN);
        REQUIRE(v::check_finite(out));
        REQUIRE(v::check_peak_below(out, 1.001f));
        renders.push_back(std::move(out));
    }

    // Every pair of waveforms must yield an audibly different carrier trace.
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            double max_diff = 0.0;
            for (int n = 0; n < kN; ++n)
                max_diff = std::max(max_diff,
                                    static_cast<double>(std::fabs(renders[i][n] - renders[j][n])));
            REQUIRE(max_diff > 1e-3);
        }
    }
}
