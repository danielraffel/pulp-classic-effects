#pragma once

// Dark Ink & Signal editor for the Phaser — see ../ink_signal_editor.hpp.
//
// Layout follows the truce/JUCE phaser control order: Depth, Feedback and the
// Stages dropdown, then the sweep controls (Min Hz, Sweep, Rate), the Waveform
// dropdown and the Stereo toggle. The shared editor wraps these eight controls
// into rows of four.

#include "phaser.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_phaser_editor(state::StateStore& store) {
    return build_effect_editor(store, EffectEditorSpec{
        .title = "PHASER",
        .subtitle = "swept all-pass notches",
        .controls = {
            {kPhaserDepth, "Depth"},
            {kPhaserFeedback, "Fbk"},
            {kPhaserStages, "Stages", Control::Kind::Combo, {"2", "4", "6", "8", "10"}},
            {kPhaserMinFreq, "Min Hz"},
            {kPhaserSweep, "Sweep"},
            {kPhaserRate, "Rate"},
            {kPhaserWaveform, "Wave", Control::Kind::Combo,
             {"Sine", "Triangle", "Square", "Sawtooth"}},
            {kPhaserStereo, "Stereo", Control::Kind::Toggle, {}},
        },
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
