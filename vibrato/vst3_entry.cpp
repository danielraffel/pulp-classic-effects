// Vibrato VST3 entry point.
#include "vibrato.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID VibratoUID(0x50554C50, 0x56696272, 0x00000001,
                                        0x00000004);

PULP_VST3_PLUGIN(VibratoUID, "Vibrato", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/danielraffel/pulp",
                 pulp::examples::classic::create_vibrato)
