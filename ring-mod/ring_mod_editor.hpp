#pragma once

// Dark Ink & Signal editor for the Ring Mod effect. See ../ink_signal_editor.hpp.

#include "ring_mod.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_ring_mod_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "RING MOD",
        .subtitle = "metallic carrier modulation",
        .grid_cols = 4,
        .controls = {
            {kRmDepth, "Depth", Control::Kind::Knob},
            {kRmFreq, "Freq", Control::Kind::Knob},
            {kRmWaveform, "Wave", Control::Kind::Combo, waveform_labels()},
        },
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
