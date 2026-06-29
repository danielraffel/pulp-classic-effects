#pragma once

// Dark Ink & Signal editor for the Ping-Pong Delay — see ../ink_signal_editor.hpp.

#include "ping_pong.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_ping_pong_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PING-PONG",
        .subtitle = "stereo bouncing echo",
        .knobs = {{kPingTime, "Time"}, {kPingFeedback, "Feedback"}, {kPingMix, "Mix"}},
        .bypass_id = kPingBypass,
    });
}

} // namespace pulp::examples::classic
