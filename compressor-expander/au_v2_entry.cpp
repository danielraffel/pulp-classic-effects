// AU v2 (.component) entry point. Defines CompressorExpanderAUFactory for Info.plist.
#include "compressor_expander.hpp"
#include <pulp/format/au_v2_entry.hpp>
PULP_AU_PLUGIN(CompressorExpanderAU, pulp::examples::classic::create_compressor_expander)
