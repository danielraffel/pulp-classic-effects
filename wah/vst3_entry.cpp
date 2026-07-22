// Wah VST3 entry point.
#include "wah.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID WahUID(0x50554C50, 0x57616821, 0x00000001,
                                    0x00000008);

PULP_VST3_PLUGIN(WahUID, "Wah-Wah", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_wah)
