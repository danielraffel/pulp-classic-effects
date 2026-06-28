#include <catch2/catch_test_macros.hpp>

#include "midi_transpose.hpp"

#include <pulp/format/headless.hpp>
#include <pulp/format/validation_assertions.hpp>

using namespace pulp;
using pulp::examples::classic::create_midi_transpose;
using pulp::examples::classic::kSemitones;
namespace v = pulp::format::validation;

namespace {

// Run one empty audio block carrying midi_in, return the produced midi_out.
midi::MidiBuffer run(format::HeadlessHost& host, midi::MidiBuffer in) {
    audio::Buffer<float> a(2, 64), b(2, 64);
    const float* ip[2] = {a.channel(0).data(), a.channel(1).data()};
    audio::BufferView<const float> iv(ip, 2, 64);
    auto ov = b.view();
    midi::MidiBuffer out;
    host.process(ov, iv, in, out, format::ProcessContext{});
    return out;
}

} // namespace

TEST_CASE("MidiTranspose descriptor declares MIDI in/out", "[midi-transpose]") {
    format::HeadlessHost host(create_midi_transpose);
    auto d = host.descriptor();
    REQUIRE(d.category == format::PluginCategory::MidiEffect);
    REQUIRE(d.accepts_midi);
    REQUIRE(d.produces_midi);
}

TEST_CASE("MidiTranspose shifts notes by the offset", "[midi-transpose]") {
    format::HeadlessHost host(create_midi_transpose);
    host.prepare(48000.0, 64);
    host.state().set_value(kSemitones, 7.0f); // up a fifth

    midi::MidiBuffer in;
    in.add(midi::MidiEvent::note_on(0, 60, 100));
    in.add(midi::MidiEvent::note_off(0, 60, 0));
    auto out = run(host, in);

    midi::MidiBuffer expected;
    expected.add(midi::MidiEvent::note_on(0, 67, 100));
    expected.add(midi::MidiEvent::note_off(0, 67, 0));
    REQUIRE(v::check_midi_events_equal(expected, out).ok);
}

TEST_CASE("MidiTranspose zero offset is identity (notes + CC + sysex)",
          "[midi-transpose]") {
    format::HeadlessHost host(create_midi_transpose);
    host.prepare(48000.0, 64);
    host.state().set_value(kSemitones, 0.0f);

    midi::MidiBuffer in;
    in.add(midi::MidiEvent::note_on(0, 60, 100));
    in.add(midi::MidiEvent::cc(0, 7, 64));
    in.add_sysex({0xF0, 0x7D, 0x01, 0xF7}, 0);
    auto out = run(host, in);

    REQUIRE(v::check_midi_events_equal(in, out).ok);
}

TEST_CASE("MidiTranspose drops notes that transpose out of range",
          "[midi-transpose]") {
    format::HeadlessHost host(create_midi_transpose);
    host.prepare(48000.0, 64);
    host.state().set_value(kSemitones, 24.0f);

    midi::MidiBuffer in;
    in.add(midi::MidiEvent::note_on(0, 120, 100)); // 120 + 24 = 144 > 127
    in.add(midi::MidiEvent::note_on(0, 60, 100));  // 60 + 24 = 84, kept
    auto out = run(host, in);

    REQUIRE(out.size() == 1);
    REQUIRE(out[0].note() == 84);
}

TEST_CASE("MidiTranspose passes CC and pitch bend through unchanged",
          "[midi-transpose]") {
    format::HeadlessHost host(create_midi_transpose);
    host.prepare(48000.0, 64);
    host.state().set_value(kSemitones, 5.0f);

    midi::MidiBuffer in;
    in.add(midi::MidiEvent::cc(0, 1, 90));
    in.add(midi::MidiEvent::pitch_bend(0, 10000));
    auto out = run(host, in);
    REQUIRE(v::check_midi_events_equal(in, out).ok);
}
