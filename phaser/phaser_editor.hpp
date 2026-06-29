#pragma once

// Dark Ink & Signal editor for the Phaser — see ../ink_signal_editor.hpp.

#include "phaser.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_phaser_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PHASER",
        .subtitle = "swept all-pass notches",
        .knobs = {{kPhaserRate, "Rate"}, {kPhaserDepth, "Depth"},
                  {kPhaserFeedback, "Feedback"}, {kPhaserMix, "Mix"}},
        .bypass_id = kPhaserBypass,
    });
}

} // namespace pulp::examples::classic
