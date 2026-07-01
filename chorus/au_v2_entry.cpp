// AU v2 (.component) entry point. Defines ChorusAUFactory for Info.plist.
#include "chorus.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(ChorusAU, pulp::examples::classic::create_chorus)
