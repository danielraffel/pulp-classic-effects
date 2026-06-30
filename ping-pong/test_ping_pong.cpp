#include <catch2/catch_test_macros.hpp>
#include "ping_pong.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_ping_pong;
using pulp::examples::classic::kPingBalance;
using pulp::examples::classic::kPingTime;
using pulp::examples::classic::kPingFeedback;
using pulp::examples::classic::kPingMix;
namespace v = pulp::format::validation;

namespace {
struct Stereo { std::vector<float> l, r; };
// Render with independent left/right input channels.
Stereo render(format::HeadlessHost& h, const std::vector<float>& L, const std::vector<float>& R) {
    const int frames = (int)L.size();
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) { in.channel(0)[n] = L[n]; in.channel(1)[n] = R[n]; }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});
    Stereo s; s.l.resize(frames); s.r.resize(frames);
    for (int n = 0; n < frames; ++n) { s.l[n] = out.channel(0)[n]; s.r[n] = out.channel(1)[n]; }
    return s;
}
// Sum of |x| over [from, to).
double energy(const std::vector<float>& x, int from, int to) {
    double e = 0.0;
    for (int n = from; n < to && n < (int)x.size(); ++n) e += std::fabs(x[n]);
    return e;
}
double energy(const std::vector<float>& x, int from = 0) { return energy(x, from, (int)x.size()); }
// Burst of `len` samples at `amp` on a `frames`-long channel.
std::vector<float> burst(int frames, int len, float amp) {
    std::vector<float> x(frames, 0.0f);
    for (int n = 0; n < len && n < frames; ++n) x[n] = amp;
    return x;
}
}

TEST_CASE("Ping-pong parameters round-trip", "[pingpong]") {
    format::HeadlessHost h(create_ping_pong);
    h.prepare(48000.0, 4096);
    for (state::ParamID id : {kPingBalance, kPingTime, kPingFeedback, kPingMix}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Ping-pong echoes alternate between left and right", "[pingpong]") {
    format::HeadlessHost h(create_ping_pong);
    h.prepare(48000.0, 4096);
    h.state().set_value(kPingBalance, 0.0f);   // full left-input feed
    h.state().set_value(kPingTime, 0.01f);     // 10 ms = 480-sample bounce
    h.state().set_value(kPingFeedback, 0.6f);
    h.state().set_value(kPingMix, 1.0f);       // fully wet to isolate echoes

    const int d = 480;
    auto L = burst(4096, 64, 0.5f);
    std::vector<float> R(4096, 0.0f);          // right input silent

    auto out = render(h, L, R);
    REQUIRE(v::check_finite(out.l));
    REQUIRE(v::check_finite(out.r));
    REQUIRE(v::check_peak_below(out.l, 1.0f));
    REQUIRE(v::check_peak_below(out.r, 1.0f));

    // Echo 1 lands at ~1*d on the LEFT (the tap the signal entered).
    const double e1_l = energy(out.l, d - 32, d + 96);
    const double e1_r = energy(out.r, d - 32, d + 96);
    REQUIRE(e1_l > 1.0);
    REQUIRE(e1_l > 4.0 * e1_r);

    // Echo 2 lands at ~2*d on the RIGHT — the bounce crossed channels.
    const double e2_l = energy(out.l, 2 * d - 32, 2 * d + 96);
    const double e2_r = energy(out.r, 2 * d - 32, 2 * d + 96);
    REQUIRE(e2_r > 1.0);
    REQUIRE(e2_r > 4.0 * e2_l);

    // A plain (non-cross-coupled) stereo delay would leave RIGHT silent.
    REQUIRE(energy(out.r) > 1.0);
    REQUIRE(v::check_any_nonzero(out.r));
}

TEST_CASE("Ping-pong balance biases which input feeds the taps", "[pingpong]") {
    auto run = [](float balance, bool left_input) {
        format::HeadlessHost h(create_ping_pong);
        h.prepare(48000.0, 4096);
        h.state().set_value(kPingBalance, balance);
        h.state().set_value(kPingTime, 0.01f);
        h.state().set_value(kPingFeedback, 0.6f);
        h.state().set_value(kPingMix, 1.0f);
        auto sig = burst(4096, 64, 0.5f);
        std::vector<float> zero(4096, 0.0f);
        auto out = left_input ? render(h, sig, zero) : render(h, zero, sig);
        return energy(out.l) + energy(out.r);
    };

    // Left-only input: balance=0 feeds the taps fully; balance=0.8 nearly mutes
    // the feed, so the wet output collapses.
    const double left_low_bal = run(0.0f, /*left_input=*/true);
    const double left_high_bal = run(0.8f, /*left_input=*/true);
    REQUIRE(left_low_bal > 3.0 * left_high_bal);

    // Right-only input: the bias is symmetric — balance=1 feeds, balance=0.2
    // nearly mutes.
    const double right_high_bal = run(1.0f, /*left_input=*/false);
    const double right_low_bal = run(0.2f, /*left_input=*/false);
    REQUIRE(right_high_bal > 3.0 * right_low_bal);
}

TEST_CASE("Ping-pong feedback regenerates and decays", "[pingpong]") {
    auto tail = [](float fb) {
        format::HeadlessHost h(create_ping_pong);
        h.prepare(48000.0, 4096);
        h.state().set_value(kPingBalance, 0.0f);
        h.state().set_value(kPingTime, 0.01f);
        h.state().set_value(kPingFeedback, fb);
        h.state().set_value(kPingMix, 1.0f);
        auto L = burst(4096, 64, 0.5f);
        std::vector<float> R(4096, 0.0f);
        return render(h, L, R);
    };

    const int d = 480;
    auto out = tail(0.6f);
    // Successive same-channel echoes (left at 1*d and 3*d) decay with feedback.
    const double echo1 = energy(out.l, d - 32, d + 96);
    const double echo3 = energy(out.l, 3 * d - 32, 3 * d + 96);
    REQUIRE(echo1 > echo3);
    REQUIRE(echo3 > 0.0);   // but it is still regenerated, not gone

    // More feedback ⇒ a longer/louder tail past the first bounce.
    auto lo = tail(0.2f);
    auto hi = tail(0.8f);
    const double tail_lo = energy(lo.l, 2 * d) + energy(lo.r, 2 * d);
    const double tail_hi = energy(hi.l, 2 * d) + energy(hi.r, 2 * d);
    REQUIRE(tail_hi > tail_lo);
}

TEST_CASE("Ping-pong mix=0 is dry with no echoes", "[pingpong]") {
    format::HeadlessHost h(create_ping_pong);
    h.prepare(48000.0, 4096);
    h.state().set_value(kPingBalance, 0.0f);   // balance=0 ⇒ left passes 1:1
    h.state().set_value(kPingTime, 0.01f);
    h.state().set_value(kPingFeedback, 0.8f);
    h.state().set_value(kPingMix, 0.0f);       // fully dry

    auto L = burst(4096, 64, 0.5f);
    std::vector<float> R(4096, 0.0f);
    auto out = render(h, L, R);

    // The dry signal passes through unchanged during the burst...
    for (int n = 0; n < 64; ++n) REQUIRE(out.l[n] == L[n]);
    // ...and there are NO echoes anywhere after it (mix=0 mutes the wet path).
    REQUIRE(energy(out.l, 200) < 1e-4);
    REQUIRE(energy(out.r, 0) < 1e-4);
}
