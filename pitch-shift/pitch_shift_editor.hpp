#pragma once

// Dark Ink & Signal editor for the Pitch Shift effect. See ../ink_signal_editor.hpp.

#include "pitch_shift.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_pitch_shift_editor(state::StateStore& store) {
    // Control order mirrors truce's editor: the Shift knob followed by the three
    // STFT combos. Combo option labels match the truce / book reference exactly.
    // No Mix, no Bypass — a phase vocoder is an inline spectral processor.
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PITCH SHIFT",
        .subtitle = "phase-vocoder pitch shifter",
        .grid_cols = 4,
        .controls = {{kShift, "Shift"},
                     {kPsFftSize, "FFT", Control::Kind::Combo,
                      {"256", "512", "1024", "2048", "4096"}, 1},
                     {kPsHop, "Hop", Control::Kind::Combo, {"1/2", "1/4", "1/8"}, 1},
                     {kPsWindow, "Window", Control::Kind::Combo,
                      {"Bartlett", "Hann", "Hamming"}, 1}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
