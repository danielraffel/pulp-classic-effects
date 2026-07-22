// Panning VST3 entry point.
#include "panning.hpp"
#include <pulp/format/vst3_entry.hpp>

static const Steinberg::FUID PanningUID(0x50554C50, 0x50414E00, 0x00000001,
                                        0x00000001);

PULP_VST3_PLUGIN(PanningUID, "Panning", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_panning)
