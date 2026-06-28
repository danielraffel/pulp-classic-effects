#include <catch2/catch_test_macros.hpp>

#include "mono_synth.hpp"

#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>

#include <cmath>
#include <vector>

using namespace pulp;
using pulp::examples::classic::create_mono_synth;
using pulp::examples::classic::kAttack;
using pulp::examples::classic::kRelease;
using pulp::examples::classic::kSustain;
namespace v = pulp::format::validation;

namespace {

// Render one block, optionally injecting a MIDI event at offset 0.
std::vector<float> render(format::HeadlessHost& host, int frames,
                          const midi::MidiEvent* ev) {
    audio::Buffer<float> a(2, frames), b(2, frames);
    const float* ip[2] = {a.channel(0).data(), a.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, frames);
    auto ov = b.view();
    midi::MidiBuffer min, mout;
    if (ev) min.add(*ev);
    host.process(ov, iv, min, mout, format::ProcessContext{});
    std::vector<float> out(frames);
    for (int i = 0; i < frames; ++i) out[i] = b.channel(0)[i];
    return out;
}

float peak(const std::vector<float>& v) {
    float p = 0.0f;
    for (float s : v) p = std::max(p, std::fabs(s));
    return p;
}

} // namespace

TEST_CASE("MonoSynth is an instrument that accepts MIDI", "[mono-synth]") {
    format::HeadlessHost host(create_mono_synth);
    auto d = host.descriptor();
    REQUIRE(d.category == format::PluginCategory::Instrument);
    REQUIRE(d.accepts_midi);
    REQUIRE(host.state().param_count() == 5);
}

TEST_CASE("MonoSynth is silent before any note", "[mono-synth]") {
    format::HeadlessHost host(create_mono_synth);
    host.prepare(48000.0, 256);
    auto out = render(host, 256, nullptr);
    REQUIRE(v::check_finite(out));
    REQUIRE(v::check_silent(out));
}

TEST_CASE("MonoSynth produces sound on note-on and decays after note-off",
          "[mono-synth]") {
    format::HeadlessHost host(create_mono_synth);
    host.prepare(48000.0, 512);
    host.state().set_value(kAttack, 0.001f);
    host.state().set_value(kSustain, 0.8f);
    host.state().set_value(kRelease, 0.02f);

    auto note_on = midi::MidiEvent::note_on(0, 69, 100); // A4 = 440 Hz
    auto sustained = render(host, 512, &note_on);
    REQUIRE(v::check_finite(sustained));
    REQUIRE(v::check_any_nonzero(sustained));        // voice is sounding
    REQUIRE(v::check_peak_below(sustained, 1.0001f));
    const float sustaining_peak = peak(sustained);
    REQUIRE(sustaining_peak > 0.1f);

    // Release and let it ring out: tail energy must fall well below sustain.
    auto note_off = midi::MidiEvent::note_off(0, 69, 0);
    render(host, 512, &note_off);                    // enter release
    float tail = 1.0f;
    for (int blk = 0; blk < 40; ++blk) tail = peak(render(host, 512, nullptr));
    REQUIRE(tail < sustaining_peak * 0.1f);
}

TEST_CASE("MonoSynth state round-trips", "[mono-synth]") {
    format::HeadlessHost host(create_mono_synth);
    host.prepare(48000.0, 256);
    host.state().set_value(kAttack, 0.2f);
    host.state().set_value(kRelease, 0.5f);
    REQUIRE(v::check_state_round_trip(host).ok);
}
