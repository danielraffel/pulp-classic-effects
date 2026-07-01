// AU v2 (.component) entry point. Defines ParametricEqAUFactory for Info.plist.
#include "parametric_eq.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(ParametricEqAU, pulp::examples::classic::create_parametric_eq)
