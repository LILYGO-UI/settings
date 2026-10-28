#include "pages/sharing/sharing_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>
#include <cm0/input.h>

namespace lilygo::settings {

SharingView::SharingView(SharingViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *SharingView::create(lv_obj_t *parent, lv_obj_t *modal_parent)
{
    destroy();
    root_ = components::create_page(parent, "settings_sharing");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 16, 0);

    auto header = components::create_detail_header(root_, "Sharing", "settings_sharing_back");
    lv_obj_set_style_max_width(header.root, 680, 0);
    auto row = components::create_toggle_row(root_, "Remote Login", "settings_sharing_remote_login_row",
                                             "settings_sharing_remote_login_switch");
    lv_obj_set_style_max_width(row.root, 680, 0);
    control_ = row.control;
    lv_obj_add_event_cb(control_, enabled_changed, LV_EVENT_VALUE_CHANGED, this);

    auto *description = components::create_label(root_, "Allow remote access using SSH, including after restarting.",
                                                 18, components::theme::section_label, "settings_sharing_description");
    lv_obj_set_style_max_width(description, 680, 0);
    status_ = components::create_label(root_, "", 18, components::theme::section_label, "settings_sharing_status");
    lv_obj_set_style_max_width(status_, 680, 0);
    auto serial = components::create_toggle_row(root_, "Serial Console", "settings_sharing_serial_row",
                                                "settings_sharing_serial_switch");
    lv_obj_set_style_max_width(serial.root, 680, 0);
    lv_label_set_long_mode(serial.title, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(serial.title, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(serial.title, LV_COORD_MAX, 0);
    serial_control_ = serial.control;
    lv_obj_add_event_cb(serial_control_, serial_enabled_changed, LV_EVENT_VALUE_CHANGED, this);
    auto *serial_description =
        components::create_label(root_, "Allow login and boot messages over serial. Requires a restart.", 18,
                                 components::theme::section_label, "settings_sharing_serial_description");
    lv_obj_set_style_max_width(serial_description, 680, 0);
    serial_status_ =
        components::create_label(root_, "", 18, components::theme::section_label, "settings_sharing_serial_status");
    lv_obj_set_style_max_width(serial_status_, 680, 0);
    create_reboot_dialog(modal_parent);
    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void SharingView::destroy() noexcept
{
    if (reboot_overlay_ && lv_obj_is_valid(reboot_overlay_)) lv_obj_delete(reboot_overlay_);
    reboot_overlay_ = reboot_now_ = reboot_later_ = reboot_error_ = nullptr;
    serial_control_ = serial_status_ = nullptr;
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = control_ = status_ = nullptr;
}

void SharingView::enabled_changed(lv_event_t *event)
{
    auto *self = static_cast<SharingView *>(lv_event_get_user_data(event));
    self->view_model_.set_enabled(lv_obj_has_state(self->control_, LV_STATE_CHECKED));
}

void SharingView::serial_enabled_changed(lv_event_t *event)
{
    auto *self = static_cast<SharingView *>(lv_event_get_user_data(event));
    self->view_model_.set_serial_enabled(lv_obj_has_state(self->serial_control_, LV_STATE_CHECKED));
}

void SharingView::reboot_now_clicked(lv_event_t *event)
{
    static_cast<SharingView *>(lv_event_get_user_data(event))->view_model_.reboot_now();
}

void SharingView::reboot_later_clicked(lv_event_t *event)
{
    static_cast<SharingView *>(lv_event_get_user_data(event))->view_model_.reboot_later();
}

void SharingView::create_reboot_dialog(lv_obj_t *parent)
{
    // The caller's surface contains both the page and sidebar; cover it all so
    // pointer and physical-keyboard navigation stay inside the modal.
    reboot_overlay_ = lv_obj_create(parent);
    lv_obj_set_name_static(reboot_overlay_, "settings_sharing_reboot_overlay");
    lv_obj_remove_style_all(reboot_overlay_);
    lv_obj_set_size(reboot_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(reboot_overlay_, LV_ALIGN_CENTER, 0, 0);
    for (auto flag : {LV_OBJ_FLAG_FLOATING, LV_OBJ_FLAG_HIDDEN, LV_OBJ_FLAG_CLICKABLE})
        lv_obj_add_flag(reboot_overlay_, flag);
    lv_obj_remove_flag(reboot_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(reboot_overlay_, lv_color_hex(components::theme::modal_scrim.rgb), 0);
    lv_obj_set_style_bg_opa(reboot_overlay_, components::theme::modal_scrim.opacity, 0);
    lv_obj_set_style_pad_all(reboot_overlay_, 16, 0);
    lv_obj_set_flex_flow(reboot_overlay_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(reboot_overlay_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    cm0_input_set_focus_scope(reboot_overlay_);

    auto *dialog = components::create_surface(reboot_overlay_, "settings_sharing_reboot_dialog");
    lv_obj_set_style_max_width(dialog, 520, 0);
    lv_obj_set_style_max_height(dialog, LV_PCT(100), 0);
    lv_obj_set_style_pad_all(dialog, 20, 0);
    lv_obj_set_style_pad_row(dialog, 12, 0);
    lv_obj_set_scroll_dir(dialog, LV_DIR_VER);
    (void)components::create_label(dialog, "Restart Required", 22, components::theme::container_title,
                                   "settings_sharing_reboot_title");
    (void)components::create_label(dialog, "Serial Console settings are saved. Restart now to apply them?", 18,
                                   components::theme::section_label);
    reboot_error_ =
        components::create_label(dialog, "", 18, components::theme::section_label, "settings_sharing_reboot_error");

    for (bool primary : {false, true}) {
        auto *button = lv_button_create(dialog);
        lv_obj_set_name_static(button, primary ? "settings_sharing_reboot_now" : "settings_sharing_reboot_later");
        lv_obj_set_size(button, LV_PCT(100), 52);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        const auto background = primary ? components::theme::primary_action : components::theme::pressed_background;
        lv_obj_set_style_bg_color(button, lv_color_hex(background.rgb), 0);
        lv_obj_set_style_bg_opa(button, background.opacity, 0);
        auto *label = components::create_label(button, primary ? "Restart Now" : "Restart Later", 22,
                                               primary ? components::theme::accent_content : components::theme::text);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(button, primary ? reboot_now_clicked : reboot_later_clicked, LV_EVENT_CLICKED, this);
        if (primary)
            reboot_now_ = button;
        else
            reboot_later_ = button;
    }
}

void SharingView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<SharingView *>(lv_observer_get_user_data(observer))->render();
}

void SharingView::render() noexcept
{
    const auto &state = view_model_.presentation();
    components::set_text(status_, state.status.c_str());
    lv_obj_set_state(control_, LV_STATE_CHECKED, state.enabled);
    lv_obj_set_state(control_, LV_STATE_DISABLED, !state.control_enabled);
    components::set_text(serial_status_, state.serial_status.c_str());
    lv_obj_set_state(serial_control_, LV_STATE_CHECKED, state.serial_enabled);
    lv_obj_set_state(serial_control_, LV_STATE_DISABLED, !state.serial_control_enabled);
    const bool was_hidden = lv_obj_has_flag(reboot_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_state(reboot_now_, LV_STATE_DISABLED, state.reboot_busy);
    lv_obj_set_state(reboot_later_, LV_STATE_DISABLED, state.reboot_busy);
    components::set_text(reboot_error_, state.reboot_busy ? "Restarting..." : state.reboot_error.c_str());
    lv_obj_set_flag(reboot_error_, LV_OBJ_FLAG_HIDDEN, !state.reboot_busy && state.reboot_error.empty());
    lv_obj_set_flag(reboot_overlay_, LV_OBJ_FLAG_HIDDEN, !state.show_reboot_prompt);
    if (was_hidden && state.show_reboot_prompt) {
        lv_obj_move_foreground(reboot_overlay_);
        if (lv_obj_get_group(reboot_later_)) lv_group_focus_obj(reboot_later_);
        lv_obj_send_event(reboot_later_, LV_EVENT_FOCUSED, nullptr);
    } else if (!was_hidden && !state.show_reboot_prompt && lv_obj_is_visible(serial_control_)) {
        if (lv_obj_get_group(serial_control_)) lv_group_focus_obj(serial_control_);
    }
}

}  // namespace lilygo::settings
