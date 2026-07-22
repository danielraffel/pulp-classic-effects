// Robotization VST3 entry point.
#include "robotization.hpp"
#include <pulp/format/vst3_entry.hpp>

static const Steinberg::FUID RobotizationUID(0x50554C50, 0x524F424F, 0x00000001,
                                             0x00000001);

PULP_VST3_PLUGIN(RobotizationUID, "Robotization", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_robotization)
