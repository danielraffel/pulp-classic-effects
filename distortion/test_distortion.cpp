#include <catch2/catch_test_macros.hpp>
#include "distortion.hpp"
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>
#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_distortion;
using pulp::examples::classic::kDistDrive;
using pulp::examples::classic::kDistTone;
using pulp::examples::classic::kDistLevel;
using pulp::examples::classic::kDistMix;
using pulp::examples::classic::kDistBypass;
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
std::vector<float> sine(float hz, int n, float amp, float sr = 48000.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
double rms(const std::vector<float>& x, int from = 0) {
    double e = 0.0; int n0 = from; for (int n = n0; n < (int)x.size(); ++n) e += (double)x[n]*x[n];
    return std::sqrt(e / (x.size() - n0));
}
double sum_abs_diff(const std::vector<float>& a, const std::vector<float>& b, int from) {
    double d = 0.0; for (int n = from; n < (int)a.size(); ++n) d += std::fabs(a[n]-b[n]); return d;
}
}

TEST_CASE("Distortion parameters round-trip", "[distortion]") {
    format::HeadlessHost h(create_distortion);
    h.prepare(48000.0, 4096);
    for (state::ParamID id : {kDistDrive, kDistTone, kDistLevel, kDistMix}) {
        const auto& range = h.state().info(id)->range;
        for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float raw = range.min + frac * (range.max - range.min);
            REQUIRE(v::check_param_round_trip(range, raw));
        }
    }
    REQUIRE(v::check_state_round_trip(h));
}

TEST_CASE("Distortion drive raises the level of a quiet signal", "[distortion]") {
    auto quiet = sine(220.0f, 4096, 0.05f);   // small so the saturator's curve matters

    format::HeadlessHost lo(create_distortion);
    lo.prepare(48000.0, 4096);
    lo.state().set_value(kDistDrive, 2.0f);
    lo.state().set_value(kDistMix, 100.0f);
    auto out_lo = render(lo, quiet);

    format::HeadlessHost hi(create_distortion);
    hi.prepare(48000.0, 4096);
    hi.state().set_value(kDistDrive, 50.0f);
    hi.state().set_value(kDistMix, 100.0f);
    auto out_hi = render(hi, quiet);

    REQUIRE(v::check_finite(out_hi));
    REQUIRE(v::check_peak_below(out_hi, 1.0f));
    // tanh is near-linear for small drive but saturates hard at high drive, so
    // a quiet input comes out far louder with more drive.
    REQUIRE(rms(out_hi, 512) > rms(out_lo, 512) * 1.5);
}

TEST_CASE("Distortion tone changes the spectrum", "[distortion]") {
    auto input = sine(2000.0f, 4096, 0.4f);

    format::HeadlessHost dark(create_distortion);
    dark.prepare(48000.0, 4096);
    dark.state().set_value(kDistDrive, 20.0f);
    dark.state().set_value(kDistTone, 0.0f);
    dark.state().set_value(kDistMix, 100.0f);
    auto out_dark = render(dark, input);

    format::HeadlessHost bright(create_distortion);
    bright.prepare(48000.0, 4096);
    bright.state().set_value(kDistDrive, 20.0f);
    bright.state().set_value(kDistTone, 100.0f);
    bright.state().set_value(kDistMix, 100.0f);
    auto out_bright = render(bright, input);

    REQUIRE(v::check_finite(out_dark));
    REQUIRE(v::check_finite(out_bright));
    // The tone low-pass must audibly change the output.
    REQUIRE(sum_abs_diff(out_bright, out_dark, 512) > 50.0);
}

TEST_CASE("Distortion mix=0 and bypass are clean passthroughs", "[distortion]") {
    auto input = sine(440.0f, 2048, 0.3f);

    format::HeadlessHost dry(create_distortion);
    dry.prepare(48000.0, 2048);
    dry.state().set_value(kDistMix, 0.0f);
    auto out_dry = render(dry, input);
    for (int n = 0; n < (int)out_dry.size(); ++n) REQUIRE(out_dry[n] == input[n]);

    format::HeadlessHost byp(create_distortion);
    byp.prepare(48000.0, 2048);
    byp.state().set_value(kDistBypass, 1.0f);
    auto out_byp = render(byp, input);
    for (int n = 0; n < (int)out_byp.size(); ++n) REQUIRE(out_byp[n] == input[n]);
}
