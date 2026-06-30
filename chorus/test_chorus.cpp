#include <catch2/catch_test_macros.hpp>
#include "chorus.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace pulp;
using namespace pulp::examples::classic;
namespace v = pulp::format::validation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSr = 48000.0f;

// kChorusVoices stores a zero-based stepper index; index = count - 2.
float voices_index(int count) { return static_cast<float>(count - kChorusMinVoices); }

struct Stereo {
    std::vector<float> l, r;
};

// Render a mono signal fed identically to both input channels and return both
// output channels (chorus stereo spreading is the only source of L/R divergence).
Stereo render(format::HeadlessHost& h, const std::vector<float>& mono) {
    const int frames = static_cast<int>(mono.size());
    audio::Buffer<float> in(2, frames), out(2, frames);
    for (int n = 0; n < frames; ++n) { in.channel(0)[n] = mono[n]; in.channel(1)[n] = mono[n]; }
    const float* ip[2] = {in.channel(0).data(), in.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = out.view();
    midi::MidiBuffer a, b;
    h.process(ov, iv, a, b, format::ProcessContext{});
    Stereo s;
    s.l.assign(out.channel(0).data(), out.channel(0).data() + frames);
    s.r.assign(out.channel(1).data(), out.channel(1).data() + frames);
    return s;
}

std::vector<float> sine(float amp, float hz, int n, float sr = kSr) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}

// Mean |a - b| over a tail window (skips the initial delay-line fill).
double mean_abs_diff(const std::vector<float>& a, const std::vector<float>& b, int lo) {
    double acc = 0.0; int c = 0;
    const int n = static_cast<int>(std::min(a.size(), b.size()));
    for (int i = lo; i < n; ++i) { acc += std::fabs(double(a[i]) - double(b[i])); ++c; }
    return acc / std::max(1, c);
}

// Default-style setup with explicit, deterministic controls.
void configure(format::HeadlessHost& h, int voices, float depth, float width,
               bool stereo, float delay = 0.03f, float rate = 0.6f) {
    h.state().set_value(kChorusDelay, delay);
    h.state().set_value(kChorusWidth, width);
    h.state().set_value(kChorusDepth, depth);
    h.state().set_value(kChorusVoices, voices_index(voices));
    h.state().set_value(kChorusRate, rate);
    h.state().set_value(kChorusWaveform, 0.0f);  // Sine
    h.state().set_value(kChorusInterp, 1.0f);    // Linear
    h.state().set_value(kChorusStereo, stereo ? 1.0f : 0.0f);
}
}  // namespace

TEST_CASE("Chorus produces finite, audible, thickened output", "[chorus]") {
    format::HeadlessHost h(create_chorus);
    h.prepare(kSr, 8192);
    configure(h, /*voices=*/4, /*depth=*/1.0f, /*width=*/0.02f, /*stereo=*/false);

    auto in = sine(0.25f, 330.0f, 8192);
    auto out = render(h, in);
    REQUIRE(v::check_finite(out.l));
    REQUIRE(v::check_any_nonzero(out.l));
    REQUIRE(v::check_peak_below(out.l, 2.0f));
}

TEST_CASE("Chorus depth=0 is exact dry; depth>0 adds wet voices", "[chorus]") {
    auto in = sine(0.25f, 330.0f, 4096);

    // depth=0 with stereo off collapses every wet tap to zero -> exact dry.
    format::HeadlessHost dry(create_chorus);
    dry.prepare(kSr, 4096);
    configure(dry, /*voices=*/4, /*depth=*/0.0f, /*width=*/0.02f, /*stereo=*/false);
    auto out0 = render(dry, in);
    for (std::size_t i = 0; i < in.size(); ++i)
        REQUIRE(std::fabs(out0.l[i] - in[i]) < 1e-6f);

    // depth=1 must clearly deviate from the dry signal.
    format::HeadlessHost wet(create_chorus);
    wet.prepare(kSr, 4096);
    configure(wet, /*voices=*/4, /*depth=*/1.0f, /*width=*/0.02f, /*stereo=*/false);
    auto out1 = render(wet, in);
    REQUIRE(mean_abs_diff(out1.l, in, 1200) > 0.02);
}

TEST_CASE("Chorus thickens as the voice count grows", "[chorus]") {
    auto in = sine(0.25f, 330.0f, 8192);

    auto wet_content = [&](int voices) {
        format::HeadlessHost h(create_chorus);
        h.prepare(kSr, 8192);
        configure(h, voices, /*depth=*/1.0f, /*width=*/0.02f, /*stereo=*/false);
        auto out = render(h, in);
        REQUIRE(v::check_finite(out.l));
        return mean_abs_diff(out.l, in, 1500);  // deviation from dry = summed wet copies
    };

    const double two = wet_content(2);
    const double five = wet_content(5);
    // 5 voices sum four detuned wet copies vs one for 2 voices: more wet content.
    // Detuned copies partly cancel, so the growth is sub-linear but still clear.
    REQUIRE(five > two * 1.2);
}

TEST_CASE("Chorus voice count changes the output", "[chorus]") {
    auto in = sine(0.25f, 330.0f, 4096);

    format::HeadlessHost two(create_chorus);
    two.prepare(kSr, 4096);
    configure(two, /*voices=*/2, /*depth=*/1.0f, /*width=*/0.02f, /*stereo=*/false);
    auto a = render(two, in);

    format::HeadlessHost four(create_chorus);
    four.prepare(kSr, 4096);
    configure(four, /*voices=*/4, /*depth=*/1.0f, /*width=*/0.02f, /*stereo=*/false);
    auto b = render(four, in);

    REQUIRE(mean_abs_diff(a.l, b.l, 1200) > 0.02);
}

TEST_CASE("Chorus Width sets the modulation amount", "[chorus]") {
    auto in = sine(0.25f, 330.0f, 8192);

    // Single wet voice so the only difference is the LFO sweep depth.
    format::HeadlessHost flat(create_chorus);
    flat.prepare(kSr, 8192);
    configure(flat, /*voices=*/2, /*depth=*/1.0f, /*width=*/0.0f, /*stereo=*/false);
    auto a = render(flat, in);  // width=0 -> fixed delay, no pitch wobble

    format::HeadlessHost swept(create_chorus);
    swept.prepare(kSr, 8192);
    configure(swept, /*voices=*/2, /*depth=*/1.0f, /*width=*/0.05f, /*stereo=*/false);
    auto b = render(swept, in);  // width large -> deep sweep

    REQUIRE(mean_abs_diff(a.l, b.l, 1500) > 0.02);
}

TEST_CASE("Chorus Stereo decorrelates the channels", "[chorus]") {
    auto in = sine(0.25f, 330.0f, 4096);

    // Stereo off: identical input -> identical channels.
    format::HeadlessHost mono(create_chorus);
    mono.prepare(kSr, 4096);
    configure(mono, /*voices=*/4, /*depth=*/1.0f, /*width=*/0.02f, /*stereo=*/false);
    auto m = render(mono, in);
    REQUIRE(mean_abs_diff(m.l, m.r, 0) < 1e-6);

    // Stereo on: channels diverge (2-voice: L dry / R wet).
    format::HeadlessHost two(create_chorus);
    two.prepare(kSr, 4096);
    configure(two, /*voices=*/2, /*depth=*/1.0f, /*width=*/0.02f, /*stereo=*/true);
    auto s2 = render(two, in);
    REQUIRE(mean_abs_diff(s2.l, s2.r, 1200) > 0.05);

    // Stereo on with >2 voices: mirrored voice weights still decorrelate L/R.
    format::HeadlessHost five(create_chorus);
    five.prepare(kSr, 4096);
    configure(five, /*voices=*/5, /*depth=*/1.0f, /*width=*/0.02f, /*stereo=*/true);
    auto s5 = render(five, in);
    REQUIRE(mean_abs_diff(s5.l, s5.r, 1200) > 0.02);
}

TEST_CASE("Chorus interpolation and waveform modes all stay finite", "[chorus]") {
    auto in = sine(0.25f, 330.0f, 4096);
    for (float wave = 0.0f; wave <= 3.0f; wave += 1.0f) {
        for (float interp = 0.0f; interp <= 2.0f; interp += 1.0f) {
            format::HeadlessHost h(create_chorus);
            h.prepare(kSr, 4096);
            configure(h, /*voices=*/4, /*depth=*/1.0f, /*width=*/0.03f, /*stereo=*/true);
            h.state().set_value(kChorusWaveform, wave);
            h.state().set_value(kChorusInterp, interp);
            auto out = render(h, in);
            REQUIRE(v::check_finite(out.l));
            REQUIRE(v::check_finite(out.r));
            REQUIRE(v::check_any_nonzero(out.l));
        }
    }
}
