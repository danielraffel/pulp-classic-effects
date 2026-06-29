// Phaser VST3 entry point.
#include "phaser.hpp"
#include <pulp/format/vst3_entry.hpp>

static const Steinberg::FUID PhaserUID(0x50554C50, 0x50485352, 0x00000001,
                                       0x00000001);

PULP_VST3_PLUGIN(PhaserUID, "Phaser", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/danielraffel/pulp",
                 pulp::examples::classic::create_phaser)
