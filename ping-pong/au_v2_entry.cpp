// AU v2 (.component) entry point. Defines PingPongAUFactory for Info.plist.
#include "ping_pong.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(PingPongAU, pulp::examples::classic::create_ping_pong)
