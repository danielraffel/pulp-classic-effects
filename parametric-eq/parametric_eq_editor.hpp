#pragma once

// Dark Ink & Signal editor for the Parametric EQ. See ../ink_signal_editor.hpp.

#include "parametric_eq.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>
#include <vector>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_parametric_eq_editor(state::StateStore& store) {
    // Editor order matches the reference: Freq, Q, Gain, then the Type dropdown.
    std::vector<std::string> type_items(kFilterTypeLabels.begin(), kFilterTypeLabels.end());
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PARAMETRIC EQ",
        .subtitle = "single-band selectable filter",
        .controls = {
            {kEqFreq, "Freq", Control::Kind::Knob},
            {kQ,    "Q",    Control::Kind::Knob},
            {kGain, "Gain", Control::Kind::Knob},
            {kType, "Type", Control::Kind::Combo, type_items},
        },
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
