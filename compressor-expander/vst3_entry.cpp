// Compressor / Expander VST3 entry point.
#include "compressor_expander.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID CompExpUID(0x50554C50, 0x43784578, 0x00000001,
                                        0x00000006);

PULP_VST3_PLUGIN(CompExpUID, "Compressor", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/danielraffel/pulp",
                 pulp::examples::classic::create_compressor_expander)
