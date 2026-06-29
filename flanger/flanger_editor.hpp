#pragma once

// Dark Ink & Signal editor for the Flanger effect — see ../ink_signal_editor.hpp.
// Free function so the headless screenshot test renders the same param-bound
// tree the plugin shows.

#include "flanger.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_flanger_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "FLANGER",
        .subtitle = "swept comb with feedback",
        .knobs = {{kFlangerRate, "Rate"}, {kFlangerDepth, "Depth"},
                  {kFlangerFeedback, "Feedback"}, {kFlangerMix, "Mix"}},
        .bypass_id = kFlangerBypass,
    });
}

} // namespace pulp::examples::classic
