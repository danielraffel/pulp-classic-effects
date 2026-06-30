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
        // Order: Depth, Rate, Waveform. The Combo labels are the full waveform
        // set and must stay in the same order as TremoloWaveform.
        .controls = {{kTremDepth, "Depth"},
                     {kRate, "Rate"},
                     {kTremWaveform, "Wave", Control::Kind::Combo,
                      {"Sine", "Triangle", "Sawtooth", "Inv. Saw", "Square", "Sq. Sloped"}}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
