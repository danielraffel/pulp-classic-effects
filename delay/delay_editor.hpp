#pragma once

// Dark Ink & Signal editor for the Delay effect — see ../ink_signal_editor.hpp
// for the shared builder. Factored as a free function so the headless screenshot
// test renders the exact same param-bound tree the plugin shows.

#include "delay.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_delay_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "DELAY",
        .subtitle = "classic feedback echo",
        .grid_cols = 3,
        .knobs = {{kTimeMs, "Time"}, {kFeedback, "Feedback"}, {kDelayMix, "Mix"}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
