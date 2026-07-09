// WAMv2 entry point for the parametric-eq classic-effect demo.
// pulp_wam_make_processor() is the factory core/format/src/wasm/wam_entry.cpp
// calls to instantiate the headless DSP Processor for the worklet.
#include "parametric_eq.hpp"
#include <memory>
std::unique_ptr<pulp::format::Processor> pulp_wam_make_processor() {
    return pulp::examples::classic::create_parametric_eq();
}
