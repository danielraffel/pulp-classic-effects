#include <catch2/catch_test_macros.hpp>
#include "panning.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_panning;
using pulp::examples::classic::kMethod;
using pulp::examples::classic::kPan;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
struct Stereo { std::vector<float> l, r; };

// Render a mono signal (fed to both input channels) through a fresh host so the
// position smoother snaps to the configured pan on the first block.
Stereo render(float method, float pan, const std::vector<float>& mono) {
    format::HeadlessHost h(create_panning);
    h.prepare(48000.0, (int)mono.size());
    h.state().set_value(kMethod, method);
    h.state().set_value(kPan, pan);

    const int frames = (int)mono.size();
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) { in.channel(0)[n] = mono[n]; in.channel(1)[n] = mono[n]; }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});

    Stereo s; s.l.resize(frames); s.r.resize(frames);
    for (int n = 0; n < frames; ++n) { s.l[n] = out.channel(0)[n]; s.r[n] = out.channel(1)[n]; }
    return s;
}

std::vector<float> sine(float hz, int n, float amp = 0.4f, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}

// Sum of squares (energy) of the latter half of a buffer — past the smoother
// settle and the delay-line warm-up.
double energy_tail(const std::vector<float>& x) {
    double e = 0.0;
    for (std::size_t i = x.size() / 2; i < x.size(); ++i) e += (double)x[i] * x[i];
    return e;
}
} // namespace

TEST_CASE("Panning parameters round-trip", "[panning]") {
    format::HeadlessHost h(create_panning);
    h.prepare(48000.0, 4096);
    for (state::ParamID id : {kMethod, kPan}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Pan+Pre positions the source left / centre / right", "[panning]") {
    auto input = sine(440.0f, 8192);

    auto left   = render(0.0f, 0.0f, input);
    auto centre = render(0.0f, 0.5f, input);
    auto right  = render(0.0f, 1.0f, input);
    REQUIRE(v::check_finite(left.l));
    REQUIRE(v::check_finite(right.r));

    const double el_l = energy_tail(left.l),   el_r = energy_tail(left.r);
    const double ec_l = energy_tail(centre.l), ec_r = energy_tail(centre.r);
    const double er_l = energy_tail(right.l),  er_r = energy_tail(right.r);

    // Pan=0 → energy in the LEFT channel; Pan=1 → energy in the RIGHT.
    REQUIRE(el_l > 50.0 * el_r);
    REQUIRE(er_r > 50.0 * er_l);
    // Pan=0.5 → balanced.
    REQUIRE(std::fabs(ec_l - ec_r) / (ec_l + ec_r) < 0.02);
}

TEST_CASE("Pan+Pre preserves constant power across positions", "[panning]") {
    auto input = sine(440.0f, 8192, 0.4f);
    const double in_e = energy_tail(input);  // mono == both input channels

    // gL² + gR² = 1 for every pan, so L+R energy tracks the mono energy
    // independent of position (the precedence delay does not change tone power).
    for (float pan : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        auto out = render(0.0f, pan, input);
        const double total = energy_tail(out.l) + energy_tail(out.r);
        REQUIRE(std::fabs(total / in_e - 1.0) < 0.03);
    }
}

TEST_CASE("ITD+ILD moves the image left / centre / right", "[panning]") {
    // The head-shadow ILD is frequency dependent (unity at DC), so probe with a
    // high tone where the shelf produces a clear level difference.
    auto input = sine(6000.0f, 8192);

    auto left   = render(1.0f, 0.0f, input);
    auto centre = render(1.0f, 0.5f, input);
    auto right  = render(1.0f, 1.0f, input);
    REQUIRE(v::check_finite(left.l));
    REQUIRE(v::check_finite(right.r));

    const double el_l = energy_tail(left.l),   el_r = energy_tail(left.r);
    const double ec_l = energy_tail(centre.l), ec_r = energy_tail(centre.r);
    const double er_l = energy_tail(right.l),  er_r = energy_tail(right.r);

    REQUIRE(el_l > 4.0 * el_r);                                   // hard left
    REQUIRE(er_r > 4.0 * er_l);                                   // hard right
    REQUIRE(std::fabs(ec_l - ec_r) / (ec_l + ec_r) < 0.05);      // centred
}

TEST_CASE("ITD+ILD applies an interaural time difference", "[panning]") {
    // A click panned hard-left should reach the LEFT output before the RIGHT.
    std::vector<float> impulse(2048, 0.0f);
    impulse[16] = 1.0f;
    auto out = render(1.0f, 0.0f, impulse);

    auto first_nonzero = [](const std::vector<float>& x) {
        for (int n = 0; n < (int)x.size(); ++n)
            if (std::fabs(x[n]) > 1e-4f) return n;
        return -1;
    };
    const int tl = first_nonzero(out.l);
    const int tr = first_nonzero(out.r);
    REQUIRE(tl >= 0);
    REQUIRE(tr >= 0);
    REQUIRE(tl < tr);  // near (left) ear leads the far (right) ear
}

TEST_CASE("The two panning methods differ", "[panning]") {
    auto input = sine(6000.0f, 4096);
    auto pan_pre = render(0.0f, 0.25f, input);
    auto itd_ild = render(1.0f, 0.25f, input);

    double diff = 0.0;
    for (std::size_t n = 0; n < input.size(); ++n)
        diff += std::fabs(pan_pre.l[n] - itd_ild.l[n]) + std::fabs(pan_pre.r[n] - itd_ild.r[n]);
    REQUIRE(diff > 1.0);
}

TEST_CASE("Panning image actually moves as Pan sweeps", "[panning]") {
    auto input = sine(6000.0f, 8192);
    double prev_ratio = -1.0;
    bool first = true;
    bool moved = false;
    // L/(L+R) energy ratio should fall monotonically as the image moves right.
    for (float pan : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        auto out = render(1.0f, pan, input);
        const double el = energy_tail(out.l), er = energy_tail(out.r);
        const double ratio = el / (el + er + 1e-12);
        if (!first) { REQUIRE(ratio < prev_ratio - 0.01); moved = true; }
        prev_ratio = ratio;
        first = false;
    }
    REQUIRE(moved);
}
