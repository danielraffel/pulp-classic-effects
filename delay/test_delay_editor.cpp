#include <catch2/catch_test_macros.hpp>
#include "delay.hpp"
#include "delay_editor.hpp"
#include <pulp/state/store.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/screenshot_compare.hpp>

using namespace pulp;
using namespace pulp::examples::classic;

TEST_CASE("Delay editor renders the dark Ink & Signal UI", "[delay][editor]") {
    // Populate a store with the real delay parameters, then build the exact
    // param-bound tree the plugin shows and render it headlessly (Skia raster).
    DelayProcessor proc;
    state::StateStore store;
    proc.define_parameters(store);
    auto editor = build_delay_editor(store);

    auto png = view::render_to_png(*editor, 520, 300, 2.0f, view::ScreenshotBackend::skia);
    if (png.empty()) { SKIP("Skia raster backend unavailable in this build"); }
    // Content floor (relaxed for a sparse single-effect panel): non-blank with
    // real tonal range. Guards against an empty/broken editor render.
    auto stats = view::analyze_screenshot_content(png);
    INFO("delay_editor unique_colors=" << stats.unique_colors
         << " lum_stddev=" << stats.luminance_stddev
         << " non_bg=" << stats.non_background_coverage);
    REQUIRE(stats.passes_content_floor(8, 2.0, 0.008, 0.95));
}
