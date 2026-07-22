// Ring Modulator VST3 entry point.
#include "ring_mod.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID RingModUID(0x50554C50, 0x526E674D, 0x00000001,
                                        0x00000002);

PULP_VST3_PLUGIN(RingModUID, "Ring Mod", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_ring_mod)
