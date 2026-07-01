#pragma once

// Dark Ink & Signal editor for the Robotization / Whisperization effect — see
// ../ink_signal_editor.hpp. Four dropdowns (Effect, FFT Size, Hop, Window) in
// the same order as the book's reference plugin; no Mix knob, no bypass.

#include "robotization.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_robotization_editor(state::StateStore& store) {
    using R = RobotizationProcessor;
    return build_effect_editor(store, EffectEditorSpec{
        .title = "ROBOTIZATION",
        .subtitle = "STFT phase manipulation",
        .grid_cols = 6,
        .controls = {
            {kEffect,  "Effect",   Control::Kind::Combo, R::effect_labels()},
            {kRobotFftSize, "FFT",      Control::Kind::Combo, R::fft_size_labels(), 1},
            {kRobotHop,     "Hop",      Control::Kind::Combo, R::hop_labels(), 1},
            {kRobotWindow,  "Window",   Control::Kind::Combo, R::window_labels()},
        },
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
