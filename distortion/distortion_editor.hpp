#pragma once

// Dark Ink & Signal editor for the Distortion — see ../ink_signal_editor.hpp.

#include "distortion.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_distortion_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "DISTORTION",
        .subtitle = "tanh drive + tone",
        .knobs = {{kDistDrive, "Drive"}, {kDistTone, "Tone"},
                  {kDistLevel, "Level"}, {kDistMix, "Mix"}},
        .bypass_id = kDistBypass,
    });
}

} // namespace pulp::examples::classic
