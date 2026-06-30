#pragma once

// Dark Ink & Signal editor for the Tremolo effect. See ../ink_signal_editor.hpp.

#include "tremolo.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_tremolo_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "TREMOLO",
        .subtitle = "amplitude modulation",
        .controls = {{kRate, "Rate"},
                     {kDepth, "Depth"},
                     {kWaveform, "Wave", Control::Kind::Combo, {"Sine", "Triangle", "Square"}}},
        .bypass_id = kBypass,
    });
}

} // namespace pulp::examples::classic
