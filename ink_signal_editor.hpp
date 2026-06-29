#pragma once

// Shared dark Ink & Signal editor builder for the example effects.
//
// build_effect_editor(store, spec) lays out a titled panel of parameter-bound
// knobs (wrapped into rows of four) plus an optional bypass toggle, themed with
// the in-core pulp::design Ink & Signal palette. Each effect supplies a small
// EffectEditorSpec; the same param-bound tree is what the plugin shows and what
// the headless screenshot tests render. Dark-only by design.

#include <pulp/design/design_system.hpp>
#include <pulp/state/binding.hpp>
#include <pulp/state/parameter.hpp>
#include <pulp/view/param_attachment.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/widgets.hpp>

#include <memory>
#include <string>
#include <vector>

namespace pulp::examples::classic {

struct EditorKnob {
    state::ParamID id;
    std::string caption;
};

struct EffectEditorSpec {
    std::string title;
    std::string subtitle;
    std::vector<EditorKnob> knobs;
    state::ParamID bypass_id = 0;   // 0 = no bypass row
    bool has_bypass = true;
};

// Root view that owns the parameter bindings so they outlive construction.
// Poll the bindings periodically to pick up host automation.
class InkSignalEditorView : public view::View {
public:
    std::vector<state::Binding> bindings;
};

inline std::unique_ptr<view::View> build_effect_editor(state::StateStore& store,
                                                       const EffectEditorSpec& spec) {
    using namespace pulp::view;
    const Theme theme = pulp::design::ink_signal_theme(/*dark=*/true);
    auto tok = [&](const char* k, Color fb) { auto c = theme.color(k); return c ? *c : fb; };
    const Color bg   = tok("bg.primary",     Color{20, 24, 30, 255});
    const Color text = tok("text.primary",   Color{240, 244, 248, 255});
    const Color sub  = tok("text.secondary", Color{150, 160, 170, 255});

    constexpr float kPad = 24.0f, kCell = 110.0f, kGap = 16.0f, kRowH = 116.0f;
    const int n = static_cast<int>(spec.knobs.size());
    const int cols = n < 4 ? (n < 1 ? 1 : n) : 4;
    const int rows = (n + 3) / 4;

    auto root = std::make_unique<InkSignalEditorView>();
    root->set_theme(theme);
    root->set_background_color(bg);
    root->flex().direction = FlexDirection::column;
    root->flex().align_items = FlexAlign::start;
    root->flex().gap = 12.0f;
    root->flex().padding = kPad;

    // Min width so the title/subtitle fit even for 0/1-knob panels.
    float width = 2 * kPad + cols * kCell + (cols - 1) * kGap;
    if (width < 360.0f) width = 360.0f;
    const float height = 2 * kPad + 26.0f + 12.0f + 16.0f + 12.0f
                       + rows * kRowH + (rows - 1) * kGap
                       + (spec.has_bypass ? 12.0f + 28.0f : 0.0f);
    root->set_bounds({0, 0, width, height});

    auto label = [&](const std::string& t, float size, Color c, float w, float h) {
        auto l = std::make_unique<Label>(t);
        l->set_font_size(size);
        l->set_text_color(c);
        l->flex().preferred_width = w; l->flex().preferred_height = h;
        l->flex().flex_grow = 0; l->flex().flex_shrink = 0;
        return l;
    };
    root->add_child(label(spec.title, 18.0f, text, width - 2 * kPad, 24.0f));
    if (!spec.subtitle.empty())
        root->add_child(label(spec.subtitle, 11.0f, sub, width - 2 * kPad, 14.0f));

    for (int r = 0; r < rows; ++r) {
        auto knob_row = std::make_unique<View>();
        knob_row->flex().direction = FlexDirection::row;
        knob_row->flex().align_items = FlexAlign::start;
        knob_row->flex().gap = kGap;
        knob_row->flex().preferred_height = kRowH; knob_row->flex().flex_grow = 0;

        for (int c = 0; c < 4 && r * 4 + c < n; ++c) {
            const EditorKnob& k = spec.knobs[r * 4 + c];
            auto cell = std::make_unique<View>();
            cell->flex().direction = FlexDirection::column;
            cell->flex().align_items = FlexAlign::center;
            cell->flex().gap = 8.0f;
            cell->flex().preferred_width = kCell; cell->flex().preferred_height = kRowH;
            cell->flex().flex_grow = 0; cell->flex().flex_shrink = 0;

            auto [knob, binding] = attach_knob(store, k.id, 84.0f);
            knob->set_label("");   // value readout stays; our caption is the name
            knob->flex().preferred_width = 84.0f; knob->flex().preferred_height = 84.0f;
            knob->flex().flex_grow = 0; knob->flex().flex_shrink = 0;
            cell->add_child(std::move(knob));
            cell->add_child(label(k.caption, 12.0f, text, 96.0f, 16.0f));
            root->bindings.push_back(std::move(binding));
            knob_row->add_child(std::move(cell));
        }
        root->add_child(std::move(knob_row));
    }

    if (spec.has_bypass && spec.bypass_id != 0) {
        auto bypass_row = std::make_unique<View>();
        bypass_row->flex().direction = FlexDirection::row;
        bypass_row->flex().align_items = FlexAlign::center;
        bypass_row->flex().gap = 10.0f;
        bypass_row->flex().preferred_height = 28.0f; bypass_row->flex().flex_grow = 0;
        auto [toggle, tbind] = attach_toggle(store, spec.bypass_id);
        toggle->set_label("");
        toggle->flex().preferred_width = 44.0f; toggle->flex().preferred_height = 24.0f;
        toggle->flex().flex_grow = 0; toggle->flex().flex_shrink = 0;
        bypass_row->add_child(std::move(toggle));
        bypass_row->add_child(label("Bypass", 13.0f, text, 80.0f, 18.0f));
        root->bindings.push_back(std::move(tbind));
        root->add_child(std::move(bypass_row));
    }

    return root;
}

} // namespace pulp::examples::classic
