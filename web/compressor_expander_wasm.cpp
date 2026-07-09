// WAMv2 entry point for the compressor-expander classic-effect demo.
// pulp_wam_make_processor() is the factory core/format/src/wasm/wam_entry.cpp
// calls to instantiate the headless DSP Processor for the worklet.
#include "compressor_expander.hpp"
#include <memory>
std::unique_ptr<pulp::format::Processor> pulp_wam_make_processor() {
    return pulp::examples::classic::create_compressor_expander();
}
