#pragma once

// Dark Ink & Signal editor for the Ping-Pong Delay — see ../ink_signal_editor.hpp.

#include "ping_pong.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_ping_pong_editor(state::StateStore& store) {
    // Knob order mirrors the reference editor: Bal, Time, Fbk, Mix. No bypass.
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PING-PONG",
        .subtitle = "stereo bouncing echo",
        .grid_cols = 4,
        .knobs = {{kPingBalance, "Bal"}, {kPingTime, "Time"},
                  {kPingFeedback, "Fbk"}, {kPingMix, "Mix"}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
