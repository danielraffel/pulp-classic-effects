#pragma once

// Dark Ink & Signal editor for the Auto-Pan — see ../ink_signal_editor.hpp.

#include "panning.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_panning_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "AUTO-PAN",
        .subtitle = "equal-power stereo sweep",
        .knobs = {{kPanRate, "Rate"}, {kPanDepth, "Depth"}, {kPanWaveform, "Wave"}},
        .bypass_id = kPanBypass,
    });
}

} // namespace pulp::examples::classic
