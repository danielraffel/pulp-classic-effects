#include <catch2/catch_test_macros.hpp>

#include "tremolo.hpp"

#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_tremolo;
using pulp::examples::classic::kBypass;
using pulp::examples::classic::kDepth;
using pulp::examples::classic::kRate;
namespace v = pulp::format::validation;

namespace {

// Render `frames` of constant 1.0 input and return the interleaved-by-channel
// output of channel 0.
std::vector<float> render_constant(format::HeadlessHost& host, int frames) {
    const int block = 512;
    std::vector<float> out_ch0;
    out_ch0.reserve(frames);
    audio::Buffer<float> in(2, block), out(2, block);
    for (int produced = 0; produced < frames; produced += block) {
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i) in.channel(ch)[i] = 1.0f;
        const float* in_ptrs[2] = {in.channel(0).data(), in.channel(1).data()};
        audio::BufferView<const float> in_view(in_ptrs, 2, block);
        auto out_view = out.view();
        host.process(out_view, in_view);
        for (int i = 0; i < block; ++i) out_ch0.push_back(out.channel(0)[i]);
    }
    return out_ch0;
}

} // namespace

TEST_CASE("Tremolo descriptor and parameters", "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    auto desc = host.descriptor();
    REQUIRE(desc.name == "Tremolo");
    REQUIRE(desc.category == format::PluginCategory::Effect);
    REQUIRE(host.state().param_count() == 4);
}

TEST_CASE("Tremolo at full depth modulates amplitude between ~0 and ~1",
          "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    host.state().set_value(kRate, 8.0f);   // fast enough to span a period quickly
    host.state().set_value(kDepth, 100.0f);

    // 48k / 8 Hz = 6000 samples per cycle; render two cycles.
    auto out = render_constant(host, 12000);

    REQUIRE(v::check_finite(out));
    const float lo = *std::min_element(out.begin(), out.end());
    const float hi = *std::max_element(out.begin(), out.end());
    // Constant 1.0 input * gain in [0,1]: the trough should approach 0 and the
    // crest should approach 1 over a full LFO cycle.
    REQUIRE(lo < 0.1f);
    REQUIRE(hi > 0.9f);
    REQUIRE(v::check_peak_below(out, 1.0001f)); // never amplifies past unity
}

TEST_CASE("Tremolo full-depth triangle at low rate never exceeds unity",
          "[tremolo]") {
    // The band-limited triangle's leaky integrator can overshoot ±1 at startup;
    // full-depth gain must still stay <= unity (regression for the LFO clamp).
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    host.state().set_value(pulp::examples::classic::kWaveform, 1.0f); // triangle
    host.state().set_value(kRate, 0.1f);                              // slow
    host.state().set_value(kDepth, 100.0f);

    auto out = render_constant(host, 8192);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 1.0001f));
}

TEST_CASE("Tremolo at zero depth is unity gain", "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    host.state().set_value(kDepth, 0.0f);

    auto out = render_constant(host, 2048);
    for (float s : out) REQUIRE(std::fabs(s - 1.0f) < 1.0e-6f);
}

TEST_CASE("Tremolo bypass passes through", "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    host.state().set_value(kBypass, 1.0f);
    host.state().set_value(kDepth, 100.0f); // ignored when bypassed

    auto out = render_constant(host, 1024);
    for (float s : out) REQUIRE(std::fabs(s - 1.0f) < 1.0e-6f);
}

TEST_CASE("Tremolo parameters round-trip and state is stable", "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    for (const auto& info : host.state().all_params()) {
        REQUIRE(v::check_param_round_trip(info.range, info.range.default_value).ok);
    }
    host.state().set_value(kRate, 6.0f);
    host.state().set_value(kDepth, 75.0f);
    REQUIRE(v::check_state_round_trip(host).ok);
}
