// AU v2 (.component) entry point. Defines TremoloAUFactory for Info.plist.
#include "tremolo.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(TremoloAU, pulp::examples::classic::create_tremolo)
