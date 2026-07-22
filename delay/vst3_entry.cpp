// Delay VST3 entry point.
#include "delay.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID DelayUID(0x50554C50, 0x446C6179, 0x00000001,
                                      0x00000003);

PULP_VST3_PLUGIN(DelayUID, "Delay", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_delay)
