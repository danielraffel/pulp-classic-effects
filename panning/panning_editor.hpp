#pragma once

// Dark Ink & Signal editor for the static Panning effect — see
// ../ink_signal_editor.hpp.

#include "panning.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_panning_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PANNING",
        .subtitle = "static stereo panner",
        .grid_cols = 3,
        .controls = {{kMethod, "Method", Control::Kind::Combo, {"Pan+Pre", "ITD+ILD"}},
                     {kPan, "Pan"}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
