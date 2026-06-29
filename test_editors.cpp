// Headless render check for every effect's dark Ink & Signal editor. Builds the
// exact param-bound tree each plugin shows and asserts it renders non-blank with
// real tonal range (relaxed content floor for a sparse panel). Skips when the
// Skia raster backend isn't compiled into the SDK build.
#include <catch2/catch_test_macros.hpp>

#include "effect_editors.hpp"

#include <pulp/state/store.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/screenshot_compare.hpp>

#include <cstdint>
#include <memory>

using namespace pulp;
using namespace pulp::examples::classic;

namespace {
void check_renders(view::View& editor) {
    const auto b = editor.bounds();
    auto png = view::render_to_png(editor, static_cast<uint32_t>(b.width),
                                   static_cast<uint32_t>(b.height), 2.0f,
                                   view::ScreenshotBackend::skia);
    if (png.empty()) { SKIP("Skia raster backend unavailable in this build"); }
    const auto stats = view::analyze_screenshot_content(png);
    INFO("unique_colors=" << stats.unique_colors
         << " lum_stddev=" << stats.luminance_stddev
         << " non_bg=" << stats.non_background_coverage);
    REQUIRE(stats.passes_content_floor(8, 2.0, 0.008, 0.95));
}

template <class Proc, class Build>
void run(Build build) {
    Proc proc;
    state::StateStore store;
    proc.define_parameters(store);
    auto editor = build(store);
    check_renders(*editor);
}
}  // namespace

TEST_CASE("Tremolo editor renders", "[editor]")        { run<TremoloProcessor>(build_tremolo_editor); }
TEST_CASE("Ring Mod editor renders", "[editor]")       { run<RingModProcessor>(build_ring_mod_editor); }
TEST_CASE("Delay editor renders", "[editor]")          { run<DelayProcessor>(build_delay_editor); }
TEST_CASE("Vibrato editor renders", "[editor]")        { run<VibratoProcessor>(build_vibrato_editor); }
TEST_CASE("Chorus editor renders", "[editor]")         { run<ChorusProcessor>(build_chorus_editor); }
TEST_CASE("Comp/Expander editor renders", "[editor]")  { run<CompressorExpanderProcessor>(build_compressor_expander_editor); }
TEST_CASE("Parametric EQ editor renders", "[editor]")  { run<ParametricEqProcessor>(build_parametric_eq_editor); }
TEST_CASE("Wah editor renders", "[editor]")            { run<WahProcessor>(build_wah_editor); }
TEST_CASE("Pitch Shift editor renders", "[editor]")    { run<PitchShiftProcessor>(build_pitch_shift_editor); }
