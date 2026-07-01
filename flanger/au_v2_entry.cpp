// AU v2 (.component) entry point. Defines FlangerAUFactory for Info.plist.
#include "flanger.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(FlangerAU, pulp::examples::classic::create_flanger)
