#pragma once

// Dark Ink & Signal editor for the Vibrato effect. See ../ink_signal_editor.hpp.

#include "vibrato.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_vibrato_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "VIBRATO",
        .subtitle = "LFO pitch modulation",
        .grid_cols = 6,
        .controls = {{kVibWidthSecs, "Width"},
                     {kVibRateHz, "Rate"},
                     {kVibWaveform, "Waveform", Control::Kind::Combo,
                      {"Sine", "Triangle", "Sawtooth", "Inv. Sawtooth"}},
                     {kVibInterp, "Interp", Control::Kind::Combo,
                      {"Nearest", "Linear", "Cubic"}}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
