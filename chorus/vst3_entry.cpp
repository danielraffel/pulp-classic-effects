// Chorus VST3 entry point.
#include "chorus.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID ChorusUID(0x50554C50, 0x43686F72, 0x00000001,
                                       0x00000005);

PULP_VST3_PLUGIN(ChorusUID, "Chorus", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_chorus)
