#include <catch2/catch_test_macros.hpp>
#include "phaser.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_phaser;
using pulp::examples::classic::kPhaserRate;
using pulp::examples::classic::kPhaserDepth;
using pulp::examples::classic::kPhaserFeedback;
using pulp::examples::classic::kPhaserMix;
using pulp::examples::classic::kPhaserBypass;
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
std::vector<float> sine(float hz, int n, float amp = 0.4f, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
double window_rms(const std::vector<float>& x, int from, int len) {
    double e = 0.0; for (int n = from; n < from + len; ++n) e += (double)x[n] * x[n];
    return std::sqrt(e / len);
}
}

TEST_CASE("Phaser parameters round-trip", "[phaser]") {
    format::HeadlessHost h(create_phaser);
    h.prepare(48000.0, 8192);
    for (state::ParamID id : {kPhaserRate, kPhaserDepth, kPhaserFeedback, kPhaserMix}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Phaser colours the signal and stays bounded", "[phaser]") {
    format::HeadlessHost h(create_phaser);
    h.prepare(48000.0, 8192);
    h.state().set_value(kPhaserRate, 1.0f);
    h.state().set_value(kPhaserDepth, 80.0f);
    h.state().set_value(kPhaserFeedback, 0.4f);
    h.state().set_value(kPhaserMix, 50.0f);

    auto input = sine(800.0f, 8192);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_any_nonzero(out));
    REQUIRE(v::check_peak_below(out, 1.0f));

    // At 50 % mix the all-pass copy phase-cancels parts of the dry signal, so
    // the output must differ from the input.
    double diff = 0.0;
    for (int n = 2000; n < 8192; ++n) diff += std::fabs(out[n] - input[n]);
    REQUIRE(diff > 50.0);
}

TEST_CASE("Phaser LFO modulates the output over time", "[phaser]") {
    format::HeadlessHost h(create_phaser);
    h.prepare(48000.0, 48000);
    h.state().set_value(kPhaserRate, 2.0f);    // ~0.5 s period
    h.state().set_value(kPhaserDepth, 100.0f);
    h.state().set_value(kPhaserFeedback, 0.6f);
    h.state().set_value(kPhaserMix, 50.0f);

    auto input = sine(800.0f, 48000);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));

    // As the notches sweep across the steady tone, the output amplitude rises
    // and falls. Sample local RMS across the second half (past the warm-up) and
    // require a meaningful spread — proof the sweep is live, not a static filter.
    double lo = 1e9, hi = 0.0;
    for (int from = 12000; from + 2000 <= 48000; from += 2000) {
        const double rms = window_rms(out, from, 2000);
        lo = std::min(lo, rms); hi = std::max(hi, rms);
    }
    REQUIRE(hi > lo * 1.15);   // >=15% peak-to-trough modulation
}

TEST_CASE("Phaser bypass is a clean passthrough", "[phaser]") {
    format::HeadlessHost h(create_phaser);
    h.prepare(48000.0, 2048);
    h.state().set_value(kPhaserBypass, 1.0f);
    auto input = sine(440.0f, 2048);
    auto out = render(h, input);
    for (int n = 0; n < (int)out.size(); ++n) REQUIRE(out[n] == input[n]);
}
