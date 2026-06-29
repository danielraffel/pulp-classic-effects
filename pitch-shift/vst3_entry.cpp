// Pitch Shift VST3 entry point.
#include "pitch_shift.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID PitchShiftUID(0x50554C50, 0x50746368, 0x00000001,
                                           0x00000009);

PULP_VST3_PLUGIN(PitchShiftUID, "Pitch Shift", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/danielraffel/pulp",
                 pulp::examples::classic::create_pitch_shift)
