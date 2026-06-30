#pragma once

// Dark Ink & Signal editor for the Compressor / Expander. See ../ink_signal_editor.hpp.

#include "compressor_expander.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_compressor_expander_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "COMPRESSOR",
        .subtitle = "stereo-linked dynamics",
        .controls = {{kMode, "Mode", Control::Kind::Combo, {"Compressor", "Expander"}},
                     {kThreshold, "Thresh"},
                     {kRatio, "Ratio"},
                     {kAttack, "Atk"},
                     {kRelease, "Rel"},
                     {kMakeup, "Makeup"}},
        .bypass_id = kCxBypass,
    });
}

} // namespace pulp::examples::classic
