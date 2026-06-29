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

// Push every parameter to a value far from its default (the far end of its
// range). Returns true if at least one parameter actually moved — i.e. the
// editor has a control whose rendered state should now differ from the
// default-state baseline. Used to prove create_view()'s editor is bound to
// the store we passed, not rendering a hard-coded default tree.
bool push_params_off_default(state::StateStore& store) {
    bool moved = false;
    for (const auto& info : store.all_params()) {
        const float def = store.get_default(info.id);
        const float mid = 0.5f * (info.range.min + info.range.max);
        const float target = (def <= mid) ? info.range.max : info.range.min;
        if (target != def) { store.set_value(info.id, target); moved = true; }
    }
    return moved;
}

template <class Proc>
void check_editor(const std::string& name) {
    Proc proc;
    state::StateStore store;
    proc.define_parameters(store);
    proc.set_state_store(&store);
    // Build through the real plugin path: create_view() is what the VST3 / AU /
    // CLAP / Standalone adapters call to obtain the editor. Asserting it here
    // proves the wiring, and the baseline compare below proves it returns the
    // same dark Ink & Signal tree the screenshots were baked from.
    auto editor = proc.create_view();
    REQUIRE(editor);
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

    // Prove the editor is bound to the store create_view() was given, not
    // rendering fixed defaults: push every param to the far end of its range,
    // rebuild via the same create_view() path, and require the render to
    // visibly diverge from the (default-state) baseline. A create_view() wired
    // to the wrong/empty store would render identically and fail this.
    Proc proc2;
    state::StateStore store2;
    proc2.define_parameters(store2);
    proc2.set_state_store(&store2);
    if (push_params_off_default(store2)) {
        auto editor2 = proc2.create_view();
        REQUIRE(editor2);
        const auto b2 = editor2->bounds();
        const auto tmp2 = fs::temp_directory_path() / (name + "-offdefault.png");
        REQUIRE(view::render_to_file(*editor2, static_cast<uint32_t>(b2.width),
                                     static_cast<uint32_t>(b2.height), tmp2.string(),
                                     2.0f, view::ScreenshotBackend::skia));
        const auto cmp2 = view::compare_screenshot_files(baseline.string(), tmp2.string(), 24);
        INFO("off-default similarity=" << cmp2.similarity);
        REQUIRE(cmp2.valid);
        REQUIRE(cmp2.similarity < 0.999f);
        fs::remove(tmp2, ec);
    }
}
}  // namespace

TEST_CASE("Tremolo editor matches baseline", "[editor]")        { check_editor<TremoloProcessor>("tremolo"); }
TEST_CASE("Ring Mod editor matches baseline", "[editor]")       { check_editor<RingModProcessor>("ring-mod"); }
TEST_CASE("Delay editor matches baseline", "[editor]")          { check_editor<DelayProcessor>("delay"); }
TEST_CASE("Vibrato editor matches baseline", "[editor]")        { check_editor<VibratoProcessor>("vibrato"); }
TEST_CASE("Chorus editor matches baseline", "[editor]")         { check_editor<ChorusProcessor>("chorus"); }
TEST_CASE("Comp/Expander editor matches baseline", "[editor]")  { check_editor<CompressorExpanderProcessor>("compressor-expander"); }
TEST_CASE("Parametric EQ editor matches baseline", "[editor]")  { check_editor<ParametricEqProcessor>("parametric-eq"); }
TEST_CASE("Wah editor matches baseline", "[editor]")            { check_editor<WahProcessor>("wah"); }
TEST_CASE("Pitch Shift editor matches baseline", "[editor]")    { check_editor<PitchShiftProcessor>("pitch-shift"); }
TEST_CASE("Flanger editor matches baseline", "[editor]")        { check_editor<FlangerProcessor>("flanger"); }
TEST_CASE("Ping-Pong editor matches baseline", "[editor]")      { check_editor<PingPongProcessor>("ping-pong"); }
TEST_CASE("Phaser editor matches baseline", "[editor]")         { check_editor<PhaserProcessor>("phaser"); }
TEST_CASE("Distortion editor matches baseline", "[editor]")     { check_editor<DistortionProcessor>("distortion"); }
TEST_CASE("Auto-Pan editor matches baseline", "[editor]")       { check_editor<PanningProcessor>("panning"); }
TEST_CASE("Robotization editor matches baseline", "[editor]")   { check_editor<RobotizationProcessor>("robotization"); }
