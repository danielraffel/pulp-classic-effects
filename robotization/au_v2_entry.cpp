// AU v2 (.component) entry point. Defines RobotizationAUFactory for Info.plist.
#include "robotization.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(RobotizationAU, pulp::examples::classic::create_robotization)
