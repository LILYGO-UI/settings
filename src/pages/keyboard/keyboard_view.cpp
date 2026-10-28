#include "pages/keyboard/keyboard_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

namespace lilygo::settings {

KeyboardView::KeyboardView(KeyboardViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *KeyboardView::create(lv_obj_t *parent)
{
    destroy();
    root_ = components::create_page(parent, "settings_keyboard");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 28, 0);

    auto header = components::create_detail_header(root_, "Keyboard", "settings_keyboard_back");
    lv_obj_set_style_max_width(header.root, 680, 0);

    lv_obj_t *card = components::create_surface(root_, "settings_keyboard_card");
    lv_obj_set_style_max_width(card, 680, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_pad_row(card, 20, 0);
    (void)components::create_label(card, "Key press vibration", 22, components::theme::container_title);
    value_ = components::create_label(card, "", 28, components::theme::text, "settings_keyboard_strength_value");

    // The full control is a 48 px touch target; the visible track remains slim.
    lv_obj_t *track = lv_obj_create(card);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, LV_PCT(100), 48);
    lv_obj_set_flex_flow(track, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(track, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(track, 12, 0);
    lv_obj_set_style_pad_right(track, 12, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    slider_ = lv_slider_create(track);
    lv_obj_set_name_static(slider_, "settings_keyboard_strength_slider");
    lv_obj_set_size(slider_, LV_PCT(100), 12);
    lv_obj_set_ext_click_area(slider_, 18);
    lv_slider_set_range(slider_, 0, 100);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(components::theme::control_track.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider_, LV_OPA_COVER, LV_PART_MAIN);
    for (auto part : {LV_PART_INDICATOR, LV_PART_KNOB}) {
        lv_obj_set_style_bg_color(slider_, lv_color_hex(components::theme::primary_action.rgb), part);
        lv_obj_set_style_bg_opa(slider_, LV_OPA_COVER, part);
    }
    lv_obj_set_style_opa(slider_, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(slider_, strength_changed, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(slider_, strength_changed, LV_EVENT_RELEASED, this);

    lv_obj_t *ends = lv_obj_create(card);
    lv_obj_remove_style_all(ends);
    lv_obj_set_size(ends, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ends, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(ends, LV_OBJ_FLAG_SCROLLABLE);
    auto *off    = components::create_label(ends, "Off", 14, components::theme::section_label);
    auto *strong = components::create_label(ends, "Strong", 14, components::theme::section_label);
    for (auto *label : {off, strong}) {
        lv_obj_set_width(label, 0);
        lv_obj_set_flex_grow(label, 1);
    }
    lv_obj_set_style_text_align(strong, LV_TEXT_ALIGN_RIGHT, 0);

    status_ = components::create_label(root_, "", 18, components::theme::section_label, "settings_keyboard_status");
    lv_obj_set_style_max_width(status_, 680, 0);
    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void KeyboardView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = slider_ = value_ = status_ = nullptr;
}

void KeyboardView::strength_changed(lv_event_t *event)
{
    auto *self = static_cast<KeyboardView *>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        if (self->view_model_.presentation().enabled)
            self->view_model_.set_strength(lv_slider_get_value(self->slider_));
    } else {
        self->render();
    }
}

void KeyboardView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<KeyboardView *>(lv_observer_get_user_data(observer))->render();
}

void KeyboardView::render() noexcept
{
    const auto &state = view_model_.presentation();
    components::set_text(value_, state.value.c_str());
    components::set_text(status_, state.status.c_str());
    lv_obj_set_state(slider_, LV_STATE_DISABLED, !state.enabled);
    if (!lv_slider_is_dragged(slider_)) lv_slider_set_value(slider_, state.strength, LV_ANIM_OFF);
}

}  // namespace lilygo::settings
