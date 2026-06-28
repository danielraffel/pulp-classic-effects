#include <catch2/catch_test_macros.hpp>
#include "ring_mod.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <algorithm>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_ring_mod;
using pulp::examples::classic::kCarrierHz;
using pulp::examples::classic::kMix;
using pulp::examples::classic::kRmBypass;
namespace v = pulp::format::validation;

namespace {
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
}

TEST_CASE("RingMod modulates a DC input with the carrier", "[ring-mod]") {
    format::HeadlessHost h(create_ring_mod);
    h.prepare(48000.0, 1024);
    h.state().set_value(kCarrierHz, 200.0f);
    h.state().set_value(kMix, 100.0f);
    auto out = render_dc(h, 1024);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 1.001f));
    const float lo = *std::min_element(out.begin(), out.end());
    const float hi = *std::max_element(out.begin(), out.end());
    REQUIRE(lo < -0.5f);   // DC * sine carrier swings negative
    REQUIRE(hi > 0.5f);
}

TEST_CASE("RingMod dry at mix=0 and bypass pass through", "[ring-mod]") {
    format::HeadlessHost h(create_ring_mod);
    h.prepare(48000.0, 256);
    h.state().set_value(kMix, 0.0f);
    for (float s : render_dc(h, 256)) REQUIRE(std::fabs(s - 1.0f) < 1e-5f);
    h.state().set_value(kMix, 100.0f);
    h.state().set_value(kRmBypass, 1.0f);
    for (float s : render_dc(h, 256)) REQUIRE(std::fabs(s - 1.0f) < 1e-5f);
}
