// Screenshot regression + bake for every effect's dark Ink & Signal editor.
//
// Each case builds the param-bound editor, renders it (Skia), asserts the frame
// isn't blank, and compares it pixel-wise against the committed baseline in
// screenshots/<name>.png. Set PULP_BAKE_SCREENSHOTS=1 to (re)generate the
// baselines instead of comparing. Skips cleanly when Skia isn't in the SDK
// build or a baseline is missing.
#include <catch2/catch_test_macros.hpp>

#include "effect_editors.hpp"

#include <pulp/state/store.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/screenshot_compare.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace pulp;
using namespace pulp::examples::classic;
namespace fs = std::filesystem;

#ifndef SCREENSHOTS_DIR
#define SCREENSHOTS_DIR "screenshots"
#endif

namespace {
bool bake_mode() {
    const char* e = std::getenv("PULP_BAKE_SCREENSHOTS");
    return e && *e && std::string(e) != "0";
}

template <class Proc, class Build>
void check_editor(Build build, const std::string& name) {
    Proc proc;
    state::StateStore store;
    proc.define_parameters(store);
    auto editor = build(store);
    const auto b = editor->bounds();
    const uint32_t w = static_cast<uint32_t>(b.width);
    const uint32_t h = static_cast<uint32_t>(b.height);
    const fs::path baseline = fs::path(SCREENSHOTS_DIR) / (name + ".png");

    auto png = view::render_to_png(*editor, w, h, 2.0f, view::ScreenshotBackend::skia);
    if (png.empty()) { SKIP("Skia raster backend unavailable in this build"); }
    const auto stats = view::analyze_screenshot_content(png);
    REQUIRE(stats.passes_content_floor(8, 2.0, 0.006, 0.95));   // never blank

    if (bake_mode()) {
        REQUIRE(view::render_to_file(*editor, w, h, baseline.string(), 2.0f,
                                     view::ScreenshotBackend::skia));
        return;
    }
    if (!fs::exists(baseline)) {
        SKIP("no baseline " + baseline.string() +
             " — run with PULP_BAKE_SCREENSHOTS=1 to bake");
    }
    // Regression: a deterministic re-render must match the committed baseline.
    const auto tmp = fs::temp_directory_path() / (name + "-fresh.png");
    REQUIRE(view::render_to_file(*editor, w, h, tmp.string(), 2.0f,
                                 view::ScreenshotBackend::skia));
    const auto cmp = view::compare_screenshot_files(baseline.string(), tmp.string(), 24);
    INFO("baseline=" << baseline.string() << " similarity=" << cmp.similarity);
    REQUIRE(cmp.valid);
    REQUIRE(cmp.passes(0.97f));
    std::error_code ec; fs::remove(tmp, ec);
}
}  // namespace

TEST_CASE("Tremolo editor matches baseline", "[editor]")        { check_editor<TremoloProcessor>(build_tremolo_editor, "tremolo"); }
TEST_CASE("Ring Mod editor matches baseline", "[editor]")       { check_editor<RingModProcessor>(build_ring_mod_editor, "ring-mod"); }
TEST_CASE("Delay editor matches baseline", "[editor]")          { check_editor<DelayProcessor>(build_delay_editor, "delay"); }
TEST_CASE("Vibrato editor matches baseline", "[editor]")        { check_editor<VibratoProcessor>(build_vibrato_editor, "vibrato"); }
TEST_CASE("Chorus editor matches baseline", "[editor]")         { check_editor<ChorusProcessor>(build_chorus_editor, "chorus"); }
TEST_CASE("Comp/Expander editor matches baseline", "[editor]")  { check_editor<CompressorExpanderProcessor>(build_compressor_expander_editor, "compressor-expander"); }
TEST_CASE("Parametric EQ editor matches baseline", "[editor]")  { check_editor<ParametricEqProcessor>(build_parametric_eq_editor, "parametric-eq"); }
TEST_CASE("Wah editor matches baseline", "[editor]")            { check_editor<WahProcessor>(build_wah_editor, "wah"); }
TEST_CASE("Pitch Shift editor matches baseline", "[editor]")    { check_editor<PitchShiftProcessor>(build_pitch_shift_editor, "pitch-shift"); }
