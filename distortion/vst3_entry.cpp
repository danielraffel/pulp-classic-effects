// Distortion VST3 entry point.
#include "distortion.hpp"
#include <pulp/format/vst3_entry.hpp>

static const Steinberg::FUID DistortionUID(0x50554C50, 0x44495354, 0x00000001,
                                           0x00000001);

PULP_VST3_PLUGIN(DistortionUID, "Distortion", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/danielraffel/pulp",
                 pulp::examples::classic::create_distortion)
