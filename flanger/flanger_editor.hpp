#pragma once

// Dark Ink & Signal editor for the Flanger effect — see ../ink_signal_editor.hpp.
// Free function so the headless screenshot test renders the same param-bound
// tree the plugin shows.

#include "flanger.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_flanger_editor(state::StateStore& store) {
    // Control order mirrors truce's editor: the swept-delay knobs (Delay, Width,
    // Depth, Feedback, Rate) followed by the discrete shapers (Waveform, Interp
    // combos) and the two toggles (Inverted, Stereo). The combo option labels
    // match the truce reference exactly. No bypass.
    return build_effect_editor(store, EffectEditorSpec{
        .title = "FLANGER",
        .subtitle = "swept comb with feedback",
        .grid_cols = 6,
        .controls = {{kFlangerDelay, "Delay"},
                     {kFlangerWidth, "Width"},
                     {kFlangerDepth, "Depth"},
                     {kFlangerFeedback, "Feedback"},
                     {kFlangerRate, "Rate"},
                     {kFlangerWaveform, "Waveform", Control::Kind::Combo,
                      {"Sine", "Triangle", "Sawtooth", "Inv. Saw"}},
                     {kFlangerInterp, "Interp", Control::Kind::Combo,
                      {"Nearest", "Linear", "Cubic"}},
                     {kFlangerInverted, "Inverted", Control::Kind::Toggle},
                     {kFlangerStereo, "Stereo", Control::Kind::Toggle}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
