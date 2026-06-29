#pragma once

// Dark Ink & Signal editor for the Delay effect.
//
// Factored as a free function `build_delay_editor(store)` so a headless
// screenshot test can build and render the exact same param-bound tree the
// plugin shows. Knobs/toggle are bound to the StateStore via the SDK's
// attach_* helpers, so the UI drives (and reflects) the real parameters.
// Dark-only: example plugins ship a single Ink & Signal theme.

#include "delay.hpp"

#include <pulp/design/design_system.hpp>
#include <pulp/state/binding.hpp>
#include <pulp/view/param_attachment.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/widgets.hpp>

#include <memory>
#include <string>
#include <vector>

namespace pulp::examples::classic {

// A root View that owns the parameter bindings so they outlive construction.
// Poll the bindings periodically to pick up host automation.
class DelayEditorView : public view::View {
public:
    std::vector<state::Binding> bindings;
};

inline std::unique_ptr<view::View> build_delay_editor(state::StateStore& store) {
    using namespace pulp::view;
    const Theme theme = pulp::design::ink_signal_theme(/*dark=*/true);
    auto tok = [&](const char* k, Color fb) { auto c = theme.color(k); return c ? *c : fb; };
    const Color bg   = tok("bg.primary",     Color{20, 24, 30, 255});
    const Color text = tok("text.primary",   Color{240, 244, 248, 255});
    const Color sub  = tok("text.secondary", Color{150, 160, 170, 255});

    auto root = std::make_unique<DelayEditorView>();
    root->set_bounds({0, 0, 520, 300});
    root->set_theme(theme);
    root->set_background_color(bg);
    root->flex().direction = FlexDirection::column;
    root->flex().align_items = FlexAlign::start;
    root->flex().gap = 12.0f;
    root->flex().padding = 24.0f;

    auto label = [&](const std::string& t, float size, Color c, float w, float h) {
        auto l = std::make_unique<Label>(t);
        l->set_font_size(size);
        l->set_text_color(c);
        l->flex().preferred_width = w; l->flex().preferred_height = h;
        l->flex().flex_grow = 0; l->flex().flex_shrink = 0;
        return l;
    };
    root->add_child(label("DELAY", 18.0f, text, 300.0f, 24.0f));
    root->add_child(label("classic feedback echo", 11.0f, sub, 300.0f, 14.0f));

    auto knob_row = std::make_unique<View>();
    knob_row->flex().direction = FlexDirection::row;
    knob_row->flex().align_items = FlexAlign::start;
    knob_row->flex().gap = 16.0f;
    knob_row->flex().preferred_height = 124.0f; knob_row->flex().flex_grow = 0;

    struct K { state::ParamID id; const char* name; };
    for (const K& k : {K{kTimeMs, "Time"}, K{kFeedback, "Feedback"}, K{kDelayMix, "Mix"}}) {
        auto cell = std::make_unique<View>();
        cell->flex().direction = FlexDirection::column;
        cell->flex().align_items = FlexAlign::center;
        cell->flex().gap = 8.0f;
        cell->flex().preferred_width = 110.0f; cell->flex().preferred_height = 116.0f;
        cell->flex().flex_grow = 0; cell->flex().flex_shrink = 0;

        auto [knob, binding] = attach_knob(store, k.id, 84.0f);
        knob->set_label("");   // value readout stays; our caption below is the name
        knob->flex().preferred_width = 84.0f; knob->flex().preferred_height = 84.0f;
        knob->flex().flex_grow = 0; knob->flex().flex_shrink = 0;
        cell->add_child(std::move(knob));
        cell->add_child(label(k.name, 12.0f, text, 96.0f, 16.0f));
        root->bindings.push_back(std::move(binding));
        knob_row->add_child(std::move(cell));
    }
    root->add_child(std::move(knob_row));

    // Bypass: pill + caption in a row so the label never overlaps the toggle.
    auto bypass_row = std::make_unique<View>();
    bypass_row->flex().direction = FlexDirection::row;
    bypass_row->flex().align_items = FlexAlign::center;
    bypass_row->flex().gap = 10.0f;
    bypass_row->flex().preferred_height = 28.0f; bypass_row->flex().flex_grow = 0;
    auto [toggle, tbind] = attach_toggle(store, kDelayBypass);
    toggle->set_label("");   // caption sits beside the pill (below), not over it
    toggle->flex().preferred_width = 44.0f; toggle->flex().preferred_height = 24.0f;
    toggle->flex().flex_grow = 0; toggle->flex().flex_shrink = 0;
    bypass_row->add_child(std::move(toggle));
    bypass_row->add_child(label("Bypass", 13.0f, text, 80.0f, 18.0f));
    root->bindings.push_back(std::move(tbind));
    root->add_child(std::move(bypass_row));

    return root;
}

} // namespace pulp::examples::classic
