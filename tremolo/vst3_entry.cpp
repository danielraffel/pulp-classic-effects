// Tremolo VST3 entry point.
#include "tremolo.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID TremoloUID(0x50554C50, 0x54524D00, 0x00000001,
                                        0x00000001);

PULP_VST3_PLUGIN(TremoloUID, "Tremolo", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/danielraffel/pulp",
                 pulp::examples::classic::create_tremolo)
