#include <catch2/catch_test_macros.hpp>
#include "robotization.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using Robot = pulp::examples::classic::RobotizationProcessor;
using pulp::examples::classic::create_robotization;
using pulp::examples::classic::kEffect;
using pulp::examples::classic::kRobotFftSize;
using pulp::examples::classic::kRobotHop;
using pulp::examples::classic::kRobotWindow;
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

// L1 difference between two equal-length signals over [from, to).
double l1_diff(const std::vector<float>& a, const std::vector<float>& b, int from, int to) {
    double d = 0.0; for (int n = from; n < to; ++n) d += std::fabs(a[n] - b[n]); return d;
}

// Normalized autocorrelation at a given lag over a window — ~1 when the signal
// repeats every `lag` samples, near 0 / negative otherwise.
double autocorr(const std::vector<float>& x, int lag, int from, int to) {
    double num = 0.0, den = 0.0;
    for (int n = from; n < to; ++n) { num += x[n] * x[n + lag]; den += x[n] * x[n]; }
    return den > 1e-12 ? num / den : 0.0;
}

Robot* proc(format::HeadlessHost& h) {
    return h.processor_as<Robot>();
}

int proc_latency(format::HeadlessHost& h) {
    return h.processor_as<Robot>()->latency_samples();
}

// L1 distance between `out` and `input` delayed by `latency`, over the region
// where the delayed input is defined.
double l1_diff_delayed(const std::vector<float>& out, const std::vector<float>& input,
                       int latency) {
    double d = 0.0;
    for (int n = latency; n < (int)out.size(); ++n) d += std::fabs(out[n] - input[n - latency]);
    return d;
}
} // namespace

TEST_CASE("Robotization parameters round-trip", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 8192);
    for (state::ParamID id : {kEffect, kRobotFftSize, kRobotHop, kRobotWindow}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Robotization reports FFT-size latency and tracks the dropdown", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 8192);

    // Default FFT Size index 4 -> 512.
    (void)render(h, std::vector<float>(64, 0.0f));   // let the first block configure
    REQUIRE(proc(h)->latency_samples() == 512);

    // Switch to the 256-pt FFT; latency must follow on the next block.
    h.state().set_value(kRobotFftSize, 3.0f);
    (void)render(h, std::vector<float>(64, 0.0f));
    REQUIRE(proc(h)->latency_samples() == 256);
}

TEST_CASE("Robotization Pass-Through is a latency-delayed identity", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 8192);
    h.state().set_value(kEffect, 0.0f);     // Pass-Through
    // Default Hann + 1/8 hop satisfies COLA, so reconstruction is near-exact.

    const int latency = proc_latency(h);
    auto input = sine(300.0f, 8192);
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 2.0f));

    // Pre-fill window is silent; afterwards the output reconstructs the input
    // delayed by exactly `latency` samples.
    for (int n = 0; n < latency - 4; ++n) REQUIRE(std::fabs(out[n]) < 1e-3f);
    double err = l1_diff_delayed(out, input, latency);
    REQUIRE(err / (8192 - latency) < 0.01);   // mean |error| well under the 0.4 amplitude
}

TEST_CASE("Robotization imposes a fixed pitch (periodic at the hop rate)", "[robotization]") {
    format::HeadlessHost h(create_robotization);
    h.prepare(48000.0, 8192);
    h.state().set_value(kEffect, 1.0f);     // Robotization, default 512 / 1/8 (hop 64)

    auto input = sine(300.0f, 8192);        // 160-sample period — NOT a hop multiple
    auto out = render(h, input);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 4.0f));

    const int hop = 64;
    const int from = 2048, to = 7000;
    // Zero-phase resynthesis makes every frame restart in phase, so the output
    // is strongly periodic at the analysis hop (its new monotone fundamental),
    // unlike the 300 Hz input whose autocorrelation at lag 64 is negative.
    REQUIRE(autocorr(out, hop, from, to) > 0.5);
    REQUIRE(autocorr(input, hop, from, to) < 0.0);

    // Carries real energy (far above the silence floor) and destroys the
    // input's phase — differs from a plain delayed copy of the input. The
    // output is an impulsive pulse train at the hop rate, so its L1 energy is
    // modest even though its peak is large; the point is that it is non-trivial.
    REQUIRE(energy(out, from, to) > 4.0);
    REQUIRE(l1_diff_delayed(out, input, 512) > 20.0);
}

TEST_CASE("Robotization effect modes produce distinct output", "[robotization]") {
    auto input = sine(300.0f, 8192);
    auto run = [&](float effect) {
        format::HeadlessHost h(create_robotization);
        h.prepare(48000.0, 8192);
        h.state().set_value(kEffect, effect);
        return render(h, input);
    };
    const auto pass  = run(0.0f);
    const auto robot = run(1.0f);
    const auto whisp = run(2.0f);
    for (const auto& s : {pass, robot, whisp}) REQUIRE(v::check_finite(s));

    const int from = 2048, to = 7000;
    // Each mode is a materially different signal from the others.
    REQUIRE(l1_diff(pass,  robot, from, to) > 20.0);
    REQUIRE(l1_diff(pass,  whisp, from, to) > 20.0);
    REQUIRE(l1_diff(robot, whisp, from, to) > 20.0);

    // Whisperization randomizes phase -> NOT periodic at the hop, unlike robot.
    REQUIRE(autocorr(whisp, 64, from, to) < autocorr(robot, 64, from, to));
}

TEST_CASE("Robotization is silent for silent input", "[robotization]") {
    for (float effect : {0.0f, 1.0f, 2.0f}) {
        format::HeadlessHost h(create_robotization);
        h.prepare(48000.0, 4096);
        h.state().set_value(kEffect, effect);
        std::vector<float> silence(4096, 0.0f);
        auto out = render(h, silence);
        REQUIRE(v::check_finite(out));
        REQUIRE(v::check_silent(out, 1.0e-6f));
    }
}

TEST_CASE("Robotization stays bounded across FFT size / hop / window settings", "[robotization]") {
    auto input = sine(220.0f, 8192);
    for (float size = 3.0f; size <= 6.0f; size += 1.0f)        // 256..2048
        for (float hop = 0.0f; hop <= 2.0f; hop += 1.0f)        // 1/2..1/8
            for (float win = 0.0f; win <= 3.0f; win += 1.0f) {  // all windows
                format::HeadlessHost h(create_robotization);
                h.prepare(48000.0, 8192);
                h.state().set_value(kEffect, 1.0f);
                h.state().set_value(kRobotFftSize, size);
                h.state().set_value(kRobotHop, hop);
                h.state().set_value(kRobotWindow, win);
                auto out = render(h, input);
                REQUIRE(v::check_finite(out));
                REQUIRE(v::check_peak_below(out, 8.0f));
            }
}
