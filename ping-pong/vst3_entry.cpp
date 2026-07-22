// Ping-Pong Delay VST3 entry point.
#include "ping_pong.hpp"
#include <pulp/format/vst3_entry.hpp>

static const Steinberg::FUID PingPongUID(0x50554C50, 0x50494E47, 0x00000001,
                                         0x00000001);

PULP_VST3_PLUGIN(PingPongUID, "Ping-Pong Delay", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_ping_pong)
