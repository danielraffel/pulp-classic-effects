// AU v2 (.component) entry point. Defines PitchShiftAUFactory for Info.plist.
#include "pitch_shift.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(PitchShiftAU, pulp::examples::classic::create_pitch_shift)
