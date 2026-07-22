// Flanger VST3 entry point.
#include "flanger.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID FlangerUID(0x50554C50, 0x464C4E47, 0x00000001,
                                        0x00000001);

PULP_VST3_PLUGIN(FlangerUID, "Flanger", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_flanger)
