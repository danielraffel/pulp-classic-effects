#include <catch2/catch_test_macros.hpp>
#include "wah.hpp"
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
constexpr int kN = 8000;
constexpr int kTail0 = 4000;

// Filter-type indices (kWahFilterType).
constexpr float kResLP    = 0.0f;
constexpr float kBandPass = 1.0f;
constexpr float kPeaking  = 2.0f;
// Mode indices (kWahMode).
constexpr float kManual    = 0.0f;
constexpr float kAutomatic = 1.0f;

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
std::vector<float> sine(float amp, float hz, int n, float sr = kSr) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) s[i] = amp * std::sin(2.0f * kPi * hz * i / sr);
    return s;
}
double rms_range(const std::vector<float>& x, int lo, int hi) {
    double acc = 0.0; int c = 0;
    for (int n = lo; n < hi && n < (int)x.size(); ++n) { acc += (double)x[n] * x[n]; ++c; }
    return std::sqrt(acc / std::max(1, c));
}
double rms_tail(const std::vector<float>& x) { return rms_range(x, kTail0, (int)x.size()); }

double gain_at(format::HeadlessHost& h, float hz, float amp, int n = kN) {
    auto in = sine(amp, hz, n);
    auto out = render(h, in);
    REQUIRE(v::check_finite(out));
    return rms_tail(out) / rms_tail(in);
}
}

TEST_CASE("Wah parameters round-trip across the full set", "[wah]") {
    format::HeadlessHost h(create_wah);
    h.prepare(kSr, kN);
    struct Case { state::ParamID id; float value; };
    const Case cases[] = {
        {kWahMode, kAutomatic}, {kWahMix, 0.25f}, {kWahFreq, 800.0f},
        {kWahQ, 5.0f}, {kWahGain, 12.0f}, {kWahFilterType, kBandPass},
        {kWahLfoRate, 3.0f}, {kWahLfoEnvMix, 0.4f}, {kWahEnvAttack, 0.01f},
        {kWahEnvRelease, 0.5f},
    };
    for (const auto& c : cases) {
        h.state().set_value(c.id, c.value);
        REQUIRE(std::fabs(h.state().get_value(c.id) - c.value) < 1e-3f);
    }
}

TEST_CASE("Wah manual mode centres the resonance on the Freq param", "[wah]") {
    // Band-pass centred at 400 Hz passes a 400 Hz tone; centred at 1300 Hz it is
    // off-band and attenuated. Proves the centre tracks the Freq param.
    double on, off;
    {
        format::HeadlessHost h(create_wah);
        h.prepare(kSr, kN);
        h.state().set_value(kWahMode, kManual);
        h.state().set_value(kWahFilterType, kBandPass);
        h.state().set_value(kWahMix, 1.0f);    // isolate the filter (no dry blend)
        h.state().set_value(kWahGain, 0.0f);   // unity makeup
        h.state().set_value(kWahQ, 6.0f);
        h.state().set_value(kWahFreq, 400.0f);
        on = gain_at(h, 400.0f, 0.2f);
    }
    {
        format::HeadlessHost h(create_wah);
        h.prepare(kSr, kN);
        h.state().set_value(kWahMode, kManual);
        h.state().set_value(kWahFilterType, kBandPass);
        h.state().set_value(kWahMix, 1.0f);
        h.state().set_value(kWahGain, 0.0f);
        h.state().set_value(kWahQ, 6.0f);
        h.state().set_value(kWahFreq, 1300.0f);
        off = gain_at(h, 400.0f, 0.2f);
    }
    REQUIRE(off < 0.5);        // 400 Hz rejected when centred at 1300 Hz
    REQUIRE(on > 2.0 * off);   // and clearly passed when centred at 400 Hz
}

TEST_CASE("Wah Q raises the resonant peak", "[wah]") {
    // A resonant lowpass at 1000 Hz: higher Q makes the peak at the cutoff taller.
    auto peak = [](float q) {
        format::HeadlessHost h(create_wah);
        h.prepare(kSr, kN);
        h.state().set_value(kWahMode, kManual);
        h.state().set_value(kWahFilterType, kResLP);
        h.state().set_value(kWahMix, 1.0f);
        h.state().set_value(kWahGain, 0.0f);   // unity makeup
        h.state().set_value(kWahFreq, 1000.0f);
        h.state().set_value(kWahQ, q);
        return gain_at(h, 1000.0f, 0.2f);
    };
    REQUIRE(peak(12.0f) > 2.0 * peak(2.0f));
}

TEST_CASE("Wah Gain knob boosts the wet level", "[wah]") {
    // On the bandpass shape Gain is a wet makeup gain: +12 dB ~= 4x the level.
    auto with_gain = [](float gain_db) {
        format::HeadlessHost h(create_wah);
        h.prepare(kSr, kN);
        h.state().set_value(kWahMode, kManual);
        h.state().set_value(kWahFilterType, kBandPass);
        h.state().set_value(kWahMix, 1.0f);
        h.state().set_value(kWahQ, 4.0f);
        h.state().set_value(kWahFreq, 700.0f);
        h.state().set_value(kWahGain, gain_db);
        return gain_at(h, 700.0f, 0.2f);
    };
    const double g0  = with_gain(0.0f);
    const double g12 = with_gain(12.0f);
    REQUIRE(g12 > 3.0 * g0);   // 12 dB ~= 3.98x
}

// Negative control for the Mode switch: manual mode must ignore input level.
// If a regression made the wah always-automatic, this fails.
TEST_CASE("Wah manual mode is level-independent (mode switch matters)", "[wah]") {
    auto man_gain = [](float amp) {
        format::HeadlessHost h(create_wah);
        h.prepare(kSr, kN);
        h.state().set_value(kWahMode, kManual);
        h.state().set_value(kWahFilterType, kBandPass);
        h.state().set_value(kWahMix, 1.0f);
        h.state().set_value(kWahGain, 0.0f);
        h.state().set_value(kWahFreq, 700.0f);
        h.state().set_value(kWahQ, 6.0f);
        return gain_at(h, 700.0f, amp);
    };
    const double quiet = man_gain(0.02f);
    const double loud = man_gain(0.5f);
    REQUIRE(std::fabs(loud - quiet) < 0.05 * quiet + 1e-6);  // level has no effect
}

TEST_CASE("Wah automatic mode sweeps the centre with input level (envelope)", "[wah]") {
    // Pure-envelope blend (LFO/Env = 1). A quiet input keeps the centre near
    // 200 Hz (a 700 Hz probe is off-band, low gain); a loud input pushes the
    // centre up toward 700 Hz (probe near the peak, higher gain). A level-
    // independent filter would give the same gain for both — this is the
    // auto-wah behaviour.
    auto env_gain = [](float amp) {
        format::HeadlessHost h(create_wah);
        h.prepare(kSr, kN);
        h.state().set_value(kWahMode, kAutomatic);
        h.state().set_value(kWahFilterType, kBandPass);
        h.state().set_value(kWahMix, 1.0f);
        h.state().set_value(kWahGain, 0.0f);
        h.state().set_value(kWahQ, 5.0f);
        h.state().set_value(kWahLfoEnvMix, 1.0f);   // pure envelope, LFO silent
        h.state().set_value(kWahEnvAttack, 0.002f);
        h.state().set_value(kWahEnvRelease, 0.3f);
        return gain_at(h, 700.0f, amp);
    };
    const double quiet = env_gain(0.02f);
    const double loud  = env_gain(0.5f);
    REQUIRE(loud > 1.5 * quiet);   // level drives the centre frequency up
}

TEST_CASE("Wah automatic LFO sweeps the centre over time", "[wah]") {
    // Pure-LFO blend (LFO/Env = 0). With a steady tone the resonant peak sweeps
    // past the probe periodically, so the output amplitude is modulated: the
    // loudest windowed RMS is well above the quietest. Manual mode (no sweep)
    // stays flat — the negative control.
    constexpr int kLong = 72000;   // 1.5 s -> three LFO cycles at 2 Hz
    auto modulation_ratio = [](bool automatic) {
        format::HeadlessHost h(create_wah);
        h.prepare(kSr, kLong);
        h.state().set_value(kWahMode, automatic ? kAutomatic : kManual);
        h.state().set_value(kWahFilterType, kBandPass);
        h.state().set_value(kWahMix, 1.0f);
        h.state().set_value(kWahGain, 0.0f);
        h.state().set_value(kWahQ, 10.0f);
        h.state().set_value(kWahLfoEnvMix, 0.0f);   // pure LFO
        h.state().set_value(kWahLfoRate, 2.0f);     // slow enough that windows sit near the turning points
        h.state().set_value(kWahFreq, 750.0f);      // manual centre for the control
        auto out = render(h, sine(0.3f, 750.0f, kLong));
        REQUIRE(v::check_finite(out));
        // Windowed RMS across the second half (filter settled). A short window
        // resolves the swept peak crossing the probe vs the off-band turns.
        double lo = 1e9, hi = 0.0;
        constexpr int win = 480;   // 10 ms
        for (int s = kLong / 2; s + win <= kLong; s += win) {
            const double r = rms_range(out, s, s + win);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        return hi / std::max(1e-9, lo);
    };
    REQUIRE(modulation_ratio(true) > 2.0);    // LFO clearly modulates the output
    REQUIRE(modulation_ratio(false) < 1.2);   // manual mode is steady
}

TEST_CASE("Wah envelope attack time shapes the onset sweep", "[wah]") {
    // Pure-envelope mode, probe at 700 Hz. A fast attack drives the centre up to
    // ~700 Hz almost immediately, so the early window is already near the peak; a
    // slow attack leaves the centre near 200 Hz through the onset, so the early
    // window gain is lower. Validates the Atk param does something.
    auto onset_gain = [](float attack_s) {
        format::HeadlessHost h(create_wah);
        h.prepare(kSr, kN);
        h.state().set_value(kWahMode, kAutomatic);
        h.state().set_value(kWahFilterType, kBandPass);
        h.state().set_value(kWahMix, 1.0f);
        h.state().set_value(kWahGain, 0.0f);
        h.state().set_value(kWahQ, 5.0f);
        h.state().set_value(kWahLfoEnvMix, 1.0f);   // pure envelope
        h.state().set_value(kWahEnvAttack, attack_s);
        h.state().set_value(kWahEnvRelease, 1.0f);  // hold so only attack varies
        auto out = render(h, sine(0.5f, 700.0f, kN));
        REQUIRE(v::check_finite(out));
        return rms_range(out, 480, 2400);   // ~10-50 ms after onset (filter settled)
    };
    const double fast = onset_gain(0.0005f);   // centre snaps to 700 Hz
    const double slow = onset_gain(0.08f);     // centre still climbing
    REQUIRE(fast > 1.2 * slow);
}

TEST_CASE("Wah stays stable at maximum resonance", "[wah]") {
    format::HeadlessHost h(create_wah);
    h.prepare(kSr, kN);
    h.state().set_value(kWahMode, kAutomatic);
    h.state().set_value(kWahFilterType, kResLP);
    h.state().set_value(kWahMix, 1.0f);
    h.state().set_value(kWahQ, 20.0f);     // param max
    h.state().set_value(kWahGain, 0.0f);   // unity makeup — isolate resonance
    h.state().set_value(kWahLfoEnvMix, 0.0f);
    h.state().set_value(kWahLfoRate, 5.0f);
    auto out = render(h, sine(0.5f, 1000.0f, kN));
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_peak_below(out, 12.0f));   // resonant but bounded
}

TEST_CASE("Wah mix=0 is fully dry", "[wah]") {
    format::HeadlessHost h(create_wah);
    h.prepare(kSr, 512);
    h.state().set_value(kWahMode, kManual);
    h.state().set_value(kWahFilterType, kResLP);
    h.state().set_value(kWahGain, 20.0f);   // big makeup must not leak when dry
    h.state().set_value(kWahMix, 0.0f);
    auto in = sine(0.4f, 700.0f, 512);
    auto dry = render(h, in);
    for (int n = 0; n < 512; ++n) REQUIRE(std::fabs(dry[n] - in[n]) < 1e-6f);
}
