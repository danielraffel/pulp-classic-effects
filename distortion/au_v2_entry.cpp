// AU v2 (.component) entry point. Defines DistortionAUFactory for Info.plist.
#include "distortion.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(DistortionAU, pulp::examples::classic::create_distortion)
