// AU v2 (.component) entry point. Defines DelayAUFactory for Info.plist.
#include "delay.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(DelayAU, pulp::examples::classic::create_delay)
