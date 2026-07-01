#pragma once

// Dark Ink & Signal editor for the Wah-Wah effect. See ../ink_signal_editor.hpp.

#include "wah.hpp"
#include "../ink_signal_editor.hpp"

#include <memory>

namespace pulp::examples::classic {

inline std::unique_ptr<view::View> build_wah_editor(state::StateStore& store) {
    // Control order mirrors truce's editor: the FILTER group (type, freq, Q,
    // gain, mix) followed by the CONTROL group (mode, LFO rate, LFO/Env blend,
    // attack, release). The two discrete params are combo boxes whose option
    // labels match the truce reference exactly.
    return build_effect_editor(store, EffectEditorSpec{
        .title = "WAH-WAH",
        .subtitle = "swept resonant filter",
        .grid_cols = 6,
        .controls = {{kWahFilterType, "Type", Control::Kind::Combo,
                      {"Res. LP", "Band-Pass", "Peaking"}},
                     {kWahFreq, "Freq"},
                     {kWahQ, "Q"},
                     {kWahGain, "Gain"},
                     {kWahMix, "Mix"},
                     {kWahMode, "Mode", Control::Kind::Combo, {"Manual", "Automatic"}},
                     {kWahLfoRate, "Rate"},
                     {kWahLfoEnvMix, "LFO/Env"},
                     {kWahEnvAttack, "Atk"},
                     {kWahEnvRelease, "Rel"}},
        .has_bypass = false,
    });
}

} // namespace pulp::examples::classic
