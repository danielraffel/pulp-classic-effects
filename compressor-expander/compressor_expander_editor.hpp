#pragma once

// Dark Ink & Signal editor for the Comp/Expander. See ../ink_signal_editor.hpp.

#include "compressor_expander.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_compressor_expander_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "COMP / EXPANDER",
        .subtitle = "stereo-linked dynamics",
        .knobs = {{kCompThreshold, "Comp Thr"}, {kCompRatio, "Comp Ratio"},
                  {kExpThreshold, "Exp Thr"}, {kExpRatio, "Exp Ratio"},
                  {kAttackMs, "Attack"}, {kReleaseMs, "Release"}, {kMakeupDb, "Makeup"}},
        .bypass_id = kCxBypass,
    });
}

} // namespace pulp::examples::classic
