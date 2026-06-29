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
        .knobs = {{kVibRateHz, "Rate"}, {kVibDepthMs, "Depth"}},
        .bypass_id = kVibBypass,
    });
}

} // namespace pulp::examples::classic
