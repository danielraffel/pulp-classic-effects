#include <catch2/catch_test_macros.hpp>

#include "tremolo.hpp"

#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_tremolo;
using pulp::examples::classic::kTremDepth;
using pulp::examples::classic::kRate;
using pulp::examples::classic::kTremWaveform;
using pulp::examples::classic::TremoloWaveform;
namespace v = pulp::format::validation;

namespace {

// Render `frames` of constant 1.0 input and return channel 0's output. With a
// constant 1.0 input the output samples are exactly the per-sample tremolo gain,
// which makes the LFO behaviour directly observable.
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

// Render a constant input through a freshly prepared host with the given
// waveform/rate/depth. Each call resets phase so waveforms are comparable.
std::vector<float> render_waveform(TremoloWaveform wf, float rate, float depth,
                                   int frames) {
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    host.state().set_value(kTremWaveform, static_cast<float>(static_cast<int>(wf)));
    host.state().set_value(kRate, rate);
    host.state().set_value(kTremDepth, depth);
    return render_constant(host, frames);
}

// Count upward crossings of `threshold` — one per LFO cycle for shapes that
// pass smoothly through the midpoint, which lets us check the modulation rate.
int count_up_crossings(const std::vector<float>& x, float threshold) {
    int crossings = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] < threshold && x[i] >= threshold) ++crossings;
    return crossings;
}

float min_of(const std::vector<float>& x) {
    return *std::min_element(x.begin(), x.end());
}
float max_of(const std::vector<float>& x) {
    return *std::max_element(x.begin(), x.end());
}

// Mean absolute difference between two equal-length signals.
float mean_abs_diff(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n == 0) return 0.0f;
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) acc += std::fabs(a[i] - b[i]);
    return static_cast<float>(acc / static_cast<double>(n));
}

} // namespace

TEST_CASE("Tremolo descriptor and parameters", "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    auto desc = host.descriptor();
    REQUIRE(desc.name == "Tremolo");
    REQUIRE(desc.category == format::PluginCategory::Effect);
    // Depth, Rate, Waveform — no bypass parameter.
    REQUIRE(host.state().param_count() == 3);
}

TEST_CASE("Tremolo at full depth modulates amplitude between ~0 and ~1",
          "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    host.state().set_value(kRate, 8.0f);   // fast enough to span a period quickly
    host.state().set_value(kTremDepth, 1.0f);  // full depth

    // 48k / 8 Hz = 6000 samples per cycle; render two cycles.
    auto out = render_constant(host, 12000);

    REQUIRE(v::check_finite(out));
    // Constant 1.0 input * gain in [0,1]: the trough should approach 0 and the
    // crest should approach 1 over a full LFO cycle.
    REQUIRE(min_of(out) < 0.1f);
    REQUIRE(max_of(out) > 0.9f);
    REQUIRE(v::check_peak_below(out, 1.0001f)); // never amplifies past unity
}

TEST_CASE("Tremolo at zero depth is unity gain", "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    host.state().set_value(kTremDepth, 0.0f);

    auto out = render_constant(host, 2048);
    for (float s : out) REQUIRE(std::fabs(s - 1.0f) < 1.0e-6f);
}

TEST_CASE("Tremolo depth controls the modulation extent", "[tremolo]") {
    // Sine LFO, same rate, two depths. The gain law g = (1 - d) + d*m bottoms
    // out at (1 - d): deeper modulation reaches a lower trough.
    const int frames = 12000;  // two cycles at 8 Hz / 48k
    auto full = render_waveform(TremoloWaveform::Sine, 8.0f, 1.0f, frames);
    auto half = render_waveform(TremoloWaveform::Sine, 8.0f, 0.5f, frames);

    REQUIRE(v::check_finite(full));
    REQUIRE(v::check_finite(half));

    // Full depth digs essentially to silence; half depth only to ~0.5.
    REQUIRE(min_of(full) < 0.05f);
    REQUIRE(min_of(half) > 0.45f);
    REQUIRE(min_of(half) < 0.55f);
    // Less depth => a higher trough => a smaller peak-to-trough swing.
    const float swing_full = max_of(full) - min_of(full);
    const float swing_half = max_of(half) - min_of(half);
    REQUIRE(swing_half < swing_full);
}

TEST_CASE("Tremolo modulates amplitude at the LFO rate", "[tremolo]") {
    // One sine cycle produces exactly one upward midpoint crossing, so the
    // crossing count over a known span tracks rate * duration.
    const float rate = 5.0f;
    const int frames = 48000;  // 1 second at 48k => ~5 cycles
    auto out = render_waveform(TremoloWaveform::Sine, rate, 1.0f, frames);

    REQUIRE(v::check_finite(out));
    const int cycles = count_up_crossings(out, 0.5f);
    REQUIRE(cycles >= 4);
    REQUIRE(cycles <= 6);
}

TEST_CASE("Tremolo waveforms produce distinct modulation", "[tremolo]") {
    // Render one cycle of each shape at full depth and confirm every pair is
    // audibly different (mean absolute gain difference well above noise).
    const float rate = 8.0f;
    const int frames = 6000;  // one cycle at 8 Hz / 48k
    const TremoloWaveform shapes[] = {
        TremoloWaveform::Sine,     TremoloWaveform::Triangle,
        TremoloWaveform::Sawtooth, TremoloWaveform::InvSaw,
        TremoloWaveform::Square,   TremoloWaveform::SqSloped,
    };
    std::vector<std::vector<float>> rendered;
    for (auto wf : shapes) {
        auto out = render_waveform(wf, rate, 1.0f, frames);
        REQUIRE(v::check_finite(out));
        rendered.push_back(std::move(out));
    }
    for (std::size_t a = 0; a < rendered.size(); ++a)
        for (std::size_t b = a + 1; b < rendered.size(); ++b)
            REQUIRE(mean_abs_diff(rendered[a], rendered[b]) > 0.01f);
}

TEST_CASE("Tremolo parameters round-trip and state is stable", "[tremolo]") {
    format::HeadlessHost host(create_tremolo);
    host.prepare(48000.0, 512);
    for (const auto& info : host.state().all_params()) {
        REQUIRE(v::check_param_round_trip(info.range, info.range.default_value).ok);
    }
    host.state().set_value(kRate, 6.0f);
    host.state().set_value(kTremDepth, 0.75f);
    host.state().set_value(kTremWaveform,
                           static_cast<float>(static_cast<int>(TremoloWaveform::Square)));
    REQUIRE(v::check_state_round_trip(host).ok);
}
