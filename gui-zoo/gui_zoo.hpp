#pragma once

// gui-zoo — a small, deterministic UI fixture for screenshot validation.
//
// Builds a static View tree out of Pulp's own widgets (panels with borders,
// labels, knobs) using Yoga flex layout, so render_to_png(ScreenshotBackend::
// skia) produces a deterministic artifact for content-floor + baseline
// comparison. Pulp lays views out with Yoga, so sizing/positioning is expressed
// via FlexStyle (flex()), NOT absolute set_bounds(). This is the "does the
// view/widget/layout/Skia paint path render real pixels" fixture — it's built
// and screenshotted headlessly, not loaded as a plugin.

#include <pulp/view/view.hpp>
#include <pulp/view/widgets.hpp>

#include <memory>

namespace pulp::examples::guizoo {

inline std::unique_ptr<pulp::view::View> build_gui_zoo() {
    using namespace pulp::view;
    using canvas::Color;

    auto fixed = [](View& v, float w, float h) {
        auto& f = v.flex();
        f.preferred_width = w;
        f.preferred_height = h;
        f.flex_grow = 0;
        f.flex_shrink = 0;
    };

    auto root = std::make_unique<View>();
    root->set_background_color(Color::rgba8(28, 29, 34, 255)); // dark panel
    {
        auto& f = root->flex();
        f.direction = FlexDirection::column;
        f.align_items = FlexAlign::start;
        f.preferred_width = 400;
        f.preferred_height = 240;
        f.padding = 16;
        f.gap = 16;
    }

    // Title.
    auto title = std::make_unique<Label>("Pulp GUI Zoo");
    title->set_font_size(20.0f);
    title->set_text_color(Color::rgba8(235, 235, 240, 255));
    title->flex().preferred_height = 28;
    root->add_child(std::move(title));

    // Row of colored swatch panels with borders (deterministic geometry).
    auto swatch_row = std::make_unique<View>();
    {
        auto& f = swatch_row->flex();
        f.direction = FlexDirection::row;
        f.align_items = FlexAlign::center;
        f.gap = 12;
        f.preferred_height = 56;
    }
    const Color swatches[4] = {
        Color::rgba8(132, 243, 237, 255), // teal
        Color::rgba8(127, 178, 255, 255), // blue
        Color::rgba8(255, 170, 120, 255), // orange
        Color::rgba8(200, 130, 245, 255), // violet
    };
    for (const auto& c : swatches) {
        auto sw = std::make_unique<View>();
        sw->set_background_color(c);
        sw->set_border(Color::rgba8(235, 235, 240, 255), 2.0f, 8.0f);
        fixed(*sw, 52, 52);
        swatch_row->add_child(std::move(sw));
    }
    root->add_child(std::move(swatch_row));

    // Row of two labeled knobs.
    auto knob_row = std::make_unique<View>();
    {
        auto& f = knob_row->flex();
        f.direction = FlexDirection::row;
        f.align_items = FlexAlign::center;
        f.gap = 40;
        f.preferred_height = 96;
    }
    const char* names[2] = {"Rate", "Depth"};
    const float values[2] = {0.3f, 0.75f};
    for (int i = 0; i < 2; ++i) {
        auto knob = std::make_unique<Knob>();
        knob->set_value(values[i]);
        knob->set_label(names[i]);
        knob->set_show_label(true);
        fixed(*knob, 80, 80);
        knob_row->add_child(std::move(knob));
    }
    root->add_child(std::move(knob_row));

    return root;
}

} // namespace pulp::examples::guizoo
