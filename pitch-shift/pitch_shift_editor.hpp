#pragma once

// Dark Ink & Signal editor for the Pitch Shift effect. See ../ink_signal_editor.hpp.

#include "pitch_shift.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_pitch_shift_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PITCH SHIFT",
        .subtitle = "+/-12 semitones",
        .knobs = {{kPitchSemitones, "Pitch"}, {kPitchMix, "Mix"}},
        .bypass_id = kPitchBypass,
    });
}

} // namespace pulp::examples::classic
