#pragma once

// Dark Ink & Signal editor for the Chorus effect. See ../ink_signal_editor.hpp.

#include "chorus.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_chorus_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "CHORUS",
        .subtitle = "shimmering thickener",
        .knobs = {{kChorusRate, "Rate"}, {kChorusDepth, "Depth"}, {kChorusMix, "Mix"}},
        .bypass_id = kChorusBypass,
    });
}

} // namespace pulp::examples::classic
