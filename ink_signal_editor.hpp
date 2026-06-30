#pragma once

// Shared dark Ink & Signal editor builder for the example effects.
//
// build_effect_editor(store, spec) lays out a titled panel of parameter-bound
// controls (wrapped into rows of four) plus an optional bypass toggle, themed
// with the in-core pulp::design Ink & Signal palette. Each control carries a
// widget KIND so a panel can mix knobs, toggles, combo boxes, faders, and
// header steppers instead of forcing every parameter into a knob. An optional
// read-only footer (status line + item list) surfaces live-style data such as
// the MIDI Inspector's recent-event log.
//
// Each effect supplies a small EffectEditorSpec; the same param-bound tree is
// what the plugin shows and what the headless screenshot tests render.
// Dark-only by design.

#include <pulp/design/design_system.hpp>
#include <pulp/state/binding.hpp>
#include <pulp/state/parameter.hpp>
#include <pulp/view/design_frame_view.hpp>
#include <pulp/view/param_attachment.hpp>
#include <pulp/view/text_editor.hpp>
#include <pulp/view/ui_components.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/widgets.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pulp::examples::classic {

// A single parameter-bound control. `kind` selects the widget; `items` supplies
// the option labels for Combo / Stepper controls (ignored otherwise).
struct Control {
    state::ParamID id;
    std::string caption;
    enum class Kind { Knob, Toggle, Combo, Fader, Stepper } kind = Kind::Knob;
    std::vector<std::string> items;  // Combo / Stepper option labels
};

// Back-compat alias: a knob-kind control described by id + caption only. Editors
// that only need knobs can keep using the `knobs` list below.
struct EditorKnob {
    state::ParamID id;
    std::string caption;
};

struct EffectEditorSpec {
    std::string title;
    std::string subtitle;
    // Preferred: heterogeneous controls. When non-empty this wins over `knobs`.
    std::vector<Control> controls;
    // Legacy knob-only list — each entry is treated as a Knob-kind Control.
    std::vector<EditorKnob> knobs;
    // Optional read-only footer (e.g. MIDI Inspector log). `status_line` renders
    // above `log_items`; both empty = no footer.
    std::string status_line;
    std::vector<std::string> log_items;
    state::ParamID bypass_id = 0;   // 0 = no bypass row
    bool has_bypass = true;

    // Optional free-text field bound to NON-parameter plugin state. The
    // StateStore only holds numeric params, so a string memo is reached through
    // these callbacks instead of an id. When `text_get` is set, an editable
    // multi-line TextEditor renders below the controls: it seeds from text_get()
    // and writes every edit back via text_set(). Both must be set together; the
    // captured target must outlive the returned view (the processor owns it).
    std::function<std::string()> text_get;
    std::function<void(const std::string&)> text_set;
    std::string text_caption;       // label above the editor
    std::string text_placeholder;   // shown when the field is empty
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

    // Normalize the spec into a single working list of controls so the layout
    // grid is identical regardless of which input list the editor populated.
    std::vector<Control> controls = spec.controls;
    if (controls.empty()) {
        controls.reserve(spec.knobs.size());
        for (const auto& k : spec.knobs)
            controls.push_back(Control{k.id, k.caption, Control::Kind::Knob, {}});
    }

    constexpr float kPad = 24.0f, kCell = 110.0f, kGap = 16.0f, kRowH = 116.0f;
    const int n = static_cast<int>(controls.size());
    const int cols = n < 4 ? (n < 1 ? 1 : n) : 4;
    const int rows = (n + 3) / 4;

    const bool has_footer = !spec.log_items.empty() || !spec.status_line.empty();
    constexpr float kStatusH = 18.0f, kLogRowH = 20.0f;
    const float footer_h = has_footer
        ? (12.0f + (spec.status_line.empty() ? 0.0f : kStatusH + 6.0f)
           + spec.log_items.size() * kLogRowH + 12.0f)
        : 0.0f;

    // A bound free-text field, when present, adds its own column block below the
    // controls: root gap + optional caption (+ its gap) + the 72px editor.
    const bool has_text = static_cast<bool>(spec.text_get) && static_cast<bool>(spec.text_set);
    constexpr float kTextEditorH = 72.0f;
    const float text_h = has_text
        ? (12.0f + (spec.text_caption.empty() ? 0.0f : 16.0f + 6.0f) + kTextEditorH)
        : 0.0f;

    auto root = std::make_unique<InkSignalEditorView>();
    root->set_theme(theme);
    root->set_background_color(bg);
    root->flex().direction = FlexDirection::column;
    root->flex().align_items = FlexAlign::start;
    root->flex().gap = 12.0f;
    root->flex().padding = kPad;

    // Min width so the title/subtitle (and any footer rows) fit even for
    // 0/1-control panels.
    float width = 2 * kPad + cols * kCell + (cols - 1) * kGap;
    if (width < 360.0f) width = 360.0f;
    if (has_footer && width < 420.0f) width = 420.0f;
    const float height = 2 * kPad + 26.0f + 12.0f + 16.0f + 12.0f
                       + rows * kRowH + (rows > 0 ? (rows - 1) * kGap : 0.0f)
                       + text_h
                       + footer_h
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

    // Build the widget for one control, binding it to the store and pushing any
    // resulting Binding onto the root so host automation can be polled later.
    auto make_widget = [&](const Control& ctl) -> std::unique_ptr<View> {
        switch (ctl.kind) {
            case Control::Kind::Toggle: {
                auto [toggle, binding] = attach_toggle(store, ctl.id);
                toggle->set_label("");
                toggle->flex().preferred_width = 52.0f; toggle->flex().preferred_height = 28.0f;
                toggle->flex().flex_grow = 0; toggle->flex().flex_shrink = 0;
                root->bindings.push_back(std::move(binding));
                return std::move(toggle);
            }
            case Control::Kind::Combo: {
                auto [combo, binding] = attach_combo(store, ctl.id, ctl.items);
                combo->flex().preferred_width = 96.0f; combo->flex().preferred_height = 28.0f;
                combo->flex().flex_grow = 0; combo->flex().flex_shrink = 0;
                root->bindings.push_back(std::move(binding));
                return std::move(combo);
            }
            case Control::Kind::Fader: {
                auto [fader, binding] = attach_fader(store, ctl.id);
                fader->set_label("");
                fader->set_orientation(Fader::Orientation::vertical);
                fader->flex().preferred_width = 40.0f; fader->flex().preferred_height = 84.0f;
                fader->flex().flex_grow = 0; fader->flex().flex_shrink = 0;
                root->bindings.push_back(std::move(binding));
                return std::move(fader);
            }
            case Control::Kind::Stepper: {
                // No attach_* helper exists for DesignStepper, so mirror
                // attach_combo's pattern by hand: seed from the stored value,
                // write the selected index back on user steps, and keep a
                // Binding for host-automation polling.
                state::Binding binding(store, ctl.id);
                const int sel = static_cast<int>(binding.get());
                auto stepper = std::make_unique<DesignStepper>(ctl.items, sel);
                const auto param_id = ctl.id;
                stepper->on_select = [&store, param_id](int index) {
                    store.set_value(param_id, static_cast<float>(index));
                };
                stepper->flex().preferred_width = 100.0f; stepper->flex().preferred_height = 28.0f;
                stepper->flex().flex_grow = 0; stepper->flex().flex_shrink = 0;
                root->bindings.push_back(std::move(binding));
                return std::move(stepper);
            }
            case Control::Kind::Knob:
            default: {
                auto [knob, binding] = attach_knob(store, ctl.id, 84.0f);
                knob->set_label("");   // value readout stays; our caption is the name
                knob->flex().preferred_width = 84.0f; knob->flex().preferred_height = 84.0f;
                knob->flex().flex_grow = 0; knob->flex().flex_shrink = 0;
                root->bindings.push_back(std::move(binding));
                return std::move(knob);
            }
        }
    };

    for (int r = 0; r < rows; ++r) {
        auto control_row = std::make_unique<View>();
        control_row->flex().direction = FlexDirection::row;
        control_row->flex().align_items = FlexAlign::start;
        control_row->flex().gap = kGap;
        control_row->flex().preferred_height = kRowH; control_row->flex().flex_grow = 0;

        for (int c = 0; c < 4 && r * 4 + c < n; ++c) {
            const Control& ctl = controls[r * 4 + c];
            auto cell = std::make_unique<View>();
            cell->flex().direction = FlexDirection::column;
            cell->flex().align_items = FlexAlign::center;
            cell->flex().justify_content = FlexJustify::center;
            cell->flex().gap = 8.0f;
            cell->flex().preferred_width = kCell; cell->flex().preferred_height = kRowH;
            cell->flex().flex_grow = 0; cell->flex().flex_shrink = 0;

            cell->add_child(make_widget(ctl));
            cell->add_child(label(ctl.caption, 12.0f, text, 96.0f, 16.0f));
            control_row->add_child(std::move(cell));
        }
        root->add_child(std::move(control_row));
    }

    // Optional free-text field for non-parameter plugin state (e.g. State Memo).
    // Bound through callbacks rather than a StateStore id: seed from text_get(),
    // and on every edit push the buffer back via text_set(). The TextEditor lives
    // as a child of root; the callbacks capture the processor. create_view() hands
    // the view to the host, which — per the standard editor lifecycle — destroys
    // the editor before the processor, so the captured target outlives it.
    if (spec.text_get && spec.text_set) {
        auto field = std::make_unique<View>();
        field->flex().direction = FlexDirection::column;
        field->flex().align_items = FlexAlign::start;
        field->flex().gap = 6.0f;
        field->flex().preferred_width = width - 2 * kPad;
        field->flex().flex_grow = 0; field->flex().flex_shrink = 0;

        if (!spec.text_caption.empty())
            field->add_child(label(spec.text_caption, 12.0f, sub, width - 2 * kPad, 16.0f));

        auto memo = std::make_unique<TextEditor>();
        memo->multi_line = true;
        memo->placeholder = spec.text_placeholder;
        memo->set_text(spec.text_get());
        memo->on_change = spec.text_set;   // copies the std::function target
        memo->flex().preferred_width = width - 2 * kPad;
        memo->flex().preferred_height = 72.0f;
        memo->flex().flex_grow = 0; memo->flex().flex_shrink = 0;
        field->add_child(std::move(memo));
        root->add_child(std::move(field));
    }

    // Optional read-only footer: a status line over a list of recent events.
    if (has_footer) {
        auto footer = std::make_unique<View>();
        footer->flex().direction = FlexDirection::column;
        footer->flex().align_items = FlexAlign::start;
        footer->flex().gap = 6.0f;
        footer->flex().preferred_width = width - 2 * kPad;
        footer->flex().preferred_height = footer_h - 12.0f;
        footer->flex().flex_grow = 0; footer->flex().flex_shrink = 0;

        if (!spec.status_line.empty())
            footer->add_child(label(spec.status_line, 11.0f, sub, width - 2 * kPad, kStatusH));

        if (!spec.log_items.empty()) {
            auto list = std::make_unique<ListBox>();
            list->set_items(spec.log_items);
            list->set_row_height(kLogRowH);
            list->flex().preferred_width = width - 2 * kPad;
            list->flex().preferred_height = spec.log_items.size() * kLogRowH;
            list->flex().flex_grow = 0; list->flex().flex_shrink = 0;
            footer->add_child(std::move(list));
        }
        root->add_child(std::move(footer));
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
