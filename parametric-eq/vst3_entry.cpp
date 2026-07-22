// Parametric EQ VST3 entry point.
#include "parametric_eq.hpp"
#include <pulp/format/vst3_entry.hpp>

// Unique, stable plugin ID — never change once shipped.
static const Steinberg::FUID ParametricEqUID(0x50554C50, 0x50617245, 0x00000001,
                                             0x00000007);

PULP_VST3_PLUGIN(ParametricEqUID, "Parametric EQ", Steinberg::Vst::PlugType::kFx,
                 "Pulp Examples", "0.1.0",
                 "https://github.com/Generous-Corp/pulp",
                 pulp::examples::classic::create_parametric_eq)
