#pragma once

// Dark Ink & Signal editor for the Parametric EQ. See ../ink_signal_editor.hpp.

#include "parametric_eq.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_parametric_eq_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PARAMETRIC EQ",
        .subtitle = "three-band shelving + bell",
        .knobs = {{kLowFreq, "Low Freq"}, {kLowGain, "Low Gain"},
                  {kMidFreq, "Mid Freq"}, {kMidGain, "Mid Gain"}, {kMidQ, "Mid Q"},
                  {kHighFreq, "High Freq"}, {kHighGain, "High Gain"}},
        .bypass_id = kEqBypass,
    });
}

} // namespace pulp::examples::classic
