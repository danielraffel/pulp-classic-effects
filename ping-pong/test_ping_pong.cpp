#include <catch2/catch_test_macros.hpp>
#include "ping_pong.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_ping_pong;
using pulp::examples::classic::kPingTime;
using pulp::examples::classic::kPingFeedback;
using pulp::examples::classic::kPingMix;
using pulp::examples::classic::kPingBypass;
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
double energy(const std::vector<float>& x, int from = 0) {
    double e = 0.0; for (int n = from; n < (int)x.size(); ++n) e += std::fabs(x[n]); return e;
}
}

TEST_CASE("Ping-pong parameters round-trip", "[pingpong]") {
    format::HeadlessHost h(create_ping_pong);
    h.prepare(48000.0, 4096);
    for (state::ParamID id : {kPingTime, kPingFeedback, kPingMix}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Ping-pong cross-couples left input to the right channel", "[pingpong]") {
    format::HeadlessHost h(create_ping_pong);
    h.prepare(48000.0, 4096);
    h.state().set_value(kPingTime, 10.0f);     // 480-sample bounce
    h.state().set_value(kPingFeedback, 0.6f);
    h.state().set_value(kPingMix, 100.0f);     // fully wet to isolate the echoes

    // A short burst on LEFT only; RIGHT input is silent.
    std::vector<float> L(4096, 0.0f), R(4096, 0.0f);
    for (int n = 0; n < 64; ++n) L[n] = 0.5f;

    auto out = render(h, L, R);
    REQUIRE(v::check_finite(out.l));
    REQUIRE(v::check_finite(out.r));
    REQUIRE(v::check_peak_below(out.l, 1.0f));
    REQUIRE(v::check_peak_below(out.r, 1.0f));

    // A plain stereo delay would leave RIGHT silent (its input was zero).
    // Cross-coupling is what puts energy on the right channel.
    REQUIRE(energy(out.r) > 5.0);
    REQUIRE(v::check_any_nonzero(out.r));
}

TEST_CASE("Ping-pong bypass is a clean passthrough", "[pingpong]") {
    format::HeadlessHost h(create_ping_pong);
    h.prepare(48000.0, 2048);
    h.state().set_value(kPingBypass, 1.0f);
    std::vector<float> L(2048), R(2048);
    for (int n = 0; n < 2048; ++n) { L[n] = 0.3f * std::sin(0.02f * n); R[n] = 0.2f * std::cos(0.017f * n); }
    auto out = render(h, L, R);
    for (int n = 0; n < 2048; ++n) { REQUIRE(out.l[n] == L[n]); REQUIRE(out.r[n] == R[n]); }
}
