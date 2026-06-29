#include <catch2/catch_test_macros.hpp>
#include "robotization.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_robotization;
using pulp::examples::classic::kRobotMix;
using pulp::examples::classic::kRobotBypass;
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
double energy(const std::vector<float>& x, int from, int to) {
    double e = 0.0; for (int n = from; n < to; ++n) e += std::fabs(x[n]); return e;
}
}

TEST_CASE("Robotization parameters round-trip", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 8192);
    const auto& range = h.state().info(kRobotMix)->range;
    for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const float raw = range.min + frac * (range.max - range.min);
        REQUIRE(v::check_param_round_trip(range, raw));
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Robotization processes after its reported latency", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 8192);
    h.state().set_value(kRobotMix, 100.0f);   // fully wet

    const int latency = h.processor_as<pulp::examples::classic::RobotizationProcessor>()
                            ->latency_samples();
    REQUIRE(latency > 0);

    auto input = sine(300.0f, 8192);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 2.0f));

    // The wet output is silent until the analysis pipeline fills (the first
    // latency samples are zeros), then it carries energy.
    REQUIRE(energy(out, 0, latency - 100) < 1.0);
    REQUIRE(energy(out, latency + 200, 8192) > 20.0);

    // Zero-phase resynthesis destroys the input's phase, so past the latency the
    // wet output differs materially from the (latency-delayed) input.
    double diff = 0.0;
    for (int n = latency + 200; n < 8192; ++n) {
        const int src = n - latency;
        diff += std::fabs(out[n] - (src >= 0 ? input[src] : 0.0f));
    }
    REQUIRE(diff > 20.0);
}

TEST_CASE("Robotization is silent for silent input", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 4096);
    h.state().set_value(kRobotMix, 100.0f);
    std::vector<float> silence(4096, 0.0f);
    auto out = render(h, silence);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_silent(out, 1.0e-6f));
}

TEST_CASE("Robotization dry path is latency-compensated (mix=0)", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 8192);
    h.state().set_value(kRobotMix, 0.0f);   // fully dry — exercises the delay-comp path
    const int latency = h.processor_as<pulp::examples::classic::RobotizationProcessor>()
                            ->latency_samples();
    auto input = sine(300.0f, 8192);
    auto out = render(h, input);
    // The dry signal must come out delayed by exactly `latency` samples (so it
    // lines up with the wet path), with the pre-fill window silent. An off-by-one
    // in the dry tap would comb-filter here instead of reconstructing the input.
    for (int n = 0; n < latency; ++n) REQUIRE(out[n] == 0.0f);
    for (int n = latency; n < 8192; ++n) REQUIRE(out[n] == input[n - latency]);
}

TEST_CASE("Robotization mix blends dry and wet linearly", "[robotization]") {
    auto input = sine(300.0f, 8192);
    auto run = [&](float mix) {
        format::HeadlessHost h(create_robotization);
        h.prepare(48000.0, 8192);
        h.state().set_value(kRobotMix, mix);
        return render(h, input);
    };
    const auto dry = run(0.0f);      // latency-aligned dry only
    const auto wet = run(100.0f);    // robotized only
    const auto half = run(50.0f);    // the blend under test

    // out(50%) must equal the mean of the dry and wet paths sample-for-sample —
    // proving both the mix coefficients and that the two paths are time-aligned
    // (a misaligned blend would diverge by O(amplitude) per sample).
    double err = 0.0;
    for (int n = 0; n < 8192; ++n)
        err += std::fabs(half[n] - 0.5f * (dry[n] + wet[n]));
    REQUIRE(err < 1.0);
}

TEST_CASE("Robotization bypass is a clean, latency-free passthrough", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 2048);
    h.state().set_value(kRobotBypass, 1.0f);
    auto input = sine(440.0f, 2048);
    auto out = render(h, input);
    for (int n = 0; n < (int)out.size(); ++n) REQUIRE(out[n] == input[n]);
}
