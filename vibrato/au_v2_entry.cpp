// AU v2 (.component) entry point. Defines VibratoAUFactory for Info.plist.
#include "vibrato.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(VibratoAU, pulp::examples::classic::create_vibrato)
