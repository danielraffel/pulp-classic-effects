#pragma once

// Dark Ink & Signal editor for the Wah effect. See ../ink_signal_editor.hpp.

#include "wah.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_wah_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "WAH",
        .subtitle = "swept resonant filter",
        .knobs = {{kWahMode, "Mode"}, {kWahFreq, "Freq"}, {kWahResonance, "Reso"},
                  {kWahSensitivity, "Sens"}, {kWahMix, "Mix"}},
        .bypass_id = kWahBypass,
    });
}

} // namespace pulp::examples::classic
