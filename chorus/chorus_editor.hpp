#pragma once

// Dark Ink & Signal editor for the Chorus effect. See ../ink_signal_editor.hpp.
// Factored as a free function so the headless screenshot test renders the exact
// same param-bound tree the plugin shows.

#include "chorus.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_chorus_editor(state::StateStore& store) {
    // Control order mirrors truce's editor: Delay, Width, Depth, Voices on the
    // first row; Rate, Waveform, Interp, Stereo on the second. Voices is a
    // discrete stepper; Waveform/Interp are combo boxes whose option labels
    // match the truce reference; Stereo is a toggle. No bypass.
    return build_effect_editor(store, EffectEditorSpec{
        .title = "CHORUS",
        .subtitle = "multi-voice ensemble thickener",
        .controls = {{kChorusDelay, "Delay"},
                     {kChorusWidth, "Width"},
                     {kChorusDepth, "Depth"},
                     {kChorusVoices, "Voices", Control::Kind::Stepper, {"2", "3", "4", "5"}},
                     {kChorusRate, "Rate"},
                     {kChorusWaveform, "Wave", Control::Kind::Combo,
                      {"Sine", "Triangle", "Sawtooth", "Inv. Sawtooth"}},
                     {kChorusInterp, "Interp", Control::Kind::Combo,
                      {"Nearest", "Linear", "Cubic"}},
                     {kChorusStereo, "Stereo", Control::Kind::Toggle, {}}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
