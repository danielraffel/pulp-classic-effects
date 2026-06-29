#pragma once

// Dark Ink & Signal editor for the Robotization effect — see ../ink_signal_editor.hpp.

#include "robotization.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_robotization_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "ROBOTIZATION",
        .subtitle = "zero-phase monotone voice",
        .knobs = {{kRobotMix, "Mix"}},
        .bypass_id = kRobotBypass,
    });
}

} // namespace pulp::examples::classic
