#pragma once

// Dark Ink & Signal editor for the Distortion — see ../ink_signal_editor.hpp.

#include "distortion.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_distortion_editor(state::StateStore& store) {
    // Control order mirrors truce's editor: Type combo, then the In / Out / Tone
    // gain knobs. The Type option labels match the truce reference exactly.
    return build_effect_editor(store, EffectEditorSpec{
        .title = "DISTORTION",
        .subtitle = "waveshaper + tone tilt",
        .controls = {{kDistType, "Type", Control::Kind::Combo,
                      {"Hard Clip", "Soft Clip", "Exponential", "Full Rect", "Half Rect"}},
                     {kDistInGain, "In"},
                     {kDistOutGain, "Out"},
                     {kDistTone, "Tone"}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
