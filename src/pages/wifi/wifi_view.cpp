#include "pages/wifi/wifi_view.hpp"

#include "components/ui_helpers.hpp"
#include "components/settings_widgets.hpp"

#include <cm0/typography.h>

#include <cstdint>

namespace lilygo::settings {
namespace {

void constrain_width(lv_obj_t *object)
{
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_style_max_width(object, 680, 0);
}

lv_obj_t *create_dialog_button(lv_obj_t *parent, const char *name, const char *text,
                               components::theme::SemanticColor background)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_name_static(button, name);
    lv_obj_set_height(button, 64);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(background.rgb), 0);
    lv_obj_t *label = components::create_label(button, text, 22, components::theme::accent_content);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(label);
    return button;
}

}  // namespace

WifiView::WifiView(WifiViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *WifiView::create(lv_obj_t *parent)
{
    destroy();
    const lv_font_t *input_font    = lilygo_ui_font_get(22);
    const lv_font_t *keyboard_font = lilygo_ui_font_get(16);
    if (!input_font || !keyboard_font) return nullptr;

    root_ = components::create_page(parent, "settings_wifi");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);

    auto header = components::create_detail_header(root_, "Wi-Fi", "settings_wifi_back");
    constrain_width(header.root);
    auto toggle = components::create_toggle_row(root_, "Wi-Fi", nullptr, "settings_wifi_switch", true);
    constrain_width(toggle.root);
    switch_ = toggle.control;
    status_ =
        components::create_label(root_, "Scanning...", 16, components::theme::section_label, "settings_wifi_status");
    constrain_width(status_);
    constrain_width(components::create_section_label(root_, "MY NETWORK"));
    connected_group_ = components::create_group(root_, "settings_wifi_connected_group");
    constrain_width(connected_group_);
    constrain_width(components::create_section_label(root_, "OTHER NETWORKS"));
    network_group_ = components::create_group(root_, "settings_wifi_network_group");
    constrain_width(network_group_);
    lv_obj_t *footnote = components::create_label(
        root_, "Network names and connection details are managed on this device.", 14, components::theme::muted_text);
    constrain_width(footnote);

    password_overlay_ = lv_obj_create(root_);
    lv_obj_set_name_static(password_overlay_, "settings_password_overlay");
    lv_obj_remove_style_all(password_overlay_);
    lv_obj_set_size(password_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(password_overlay_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(password_overlay_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(password_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(password_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(password_overlay_, lv_color_hex(components::theme::modal_scrim.rgb), 0);
    lv_obj_set_style_bg_opa(password_overlay_, components::theme::modal_scrim.opacity, 0);

    lv_obj_t *dialog = components::create_surface(password_overlay_, "settings_password_dialog");
    lv_obj_set_width(dialog, LV_PCT(85));
    lv_obj_set_style_max_width(dialog, 640, 0);
    lv_obj_set_height(dialog, LV_SIZE_CONTENT);
    lv_obj_align(dialog, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_layout(dialog, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(dialog, 20, 0);
    lv_obj_set_style_pad_row(dialog, 12, 0);

    password_title_ = components::create_label(dialog, "Join Network", 22, components::theme::container_title,
                                               "settings_password_title");
    lv_obj_set_width(password_title_, LV_PCT(100));
    lv_obj_set_height(password_title_, 34);
    lv_obj_set_style_max_height(password_title_, 34, LV_PART_MAIN);
    lv_label_set_long_mode(password_title_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(password_title_, LV_TEXT_ALIGN_CENTER, 0);

    password_input_ = lv_textarea_create(dialog);
    lv_obj_set_name_static(password_input_, "settings_password_input");
    lv_obj_set_width(password_input_, LV_PCT(100));
    lv_obj_set_height(password_input_, 64);
    lv_textarea_set_one_line(password_input_, true);
    lv_textarea_set_password_mode(password_input_, true);
    lv_textarea_set_placeholder_text(password_input_, "Password");
    lv_obj_set_style_text_font(password_input_, input_font, 0);

    lv_obj_t *actions = lv_obj_create(dialog);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 64);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 4, 0);
    password_cancel_ = create_dialog_button(actions, "settings_password_cancel", "Cancel", components::theme::accent);
    password_join_ = create_dialog_button(actions, "settings_password_join", "Join", components::theme::primary_action);

    keyboard_ = lv_keyboard_create(password_overlay_);
    lv_obj_set_name_static(keyboard_, "settings_password_keyboard");
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_set_height(keyboard_, LV_PCT(38));
    lv_obj_set_style_text_font(keyboard_, keyboard_font, LV_PART_ITEMS);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_add_event_cb(switch_, switch_changed, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(password_cancel_, password_cancel_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(password_join_, password_connect_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(password_input_, password_input_ready, LV_EVENT_READY, this);
    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    lv_keyboard_set_textarea(keyboard_, password_input_);
    render();
    return root_;
}

void WifiView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = switch_ = status_ = connected_group_ = network_group_ = password_overlay_ = password_title_ =
        password_input_ = password_cancel_ = password_join_ = keyboard_ = nullptr;
    rendered_generation_                                                = std::numeric_limits<std::size_t>::max();
    rendered_available_                                                 = false;
    rendered_enabled_                                                   = false;
    updating_                                                           = false;
}

void WifiView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<WifiView *>(lv_observer_get_user_data(observer))->render();
}

void WifiView::switch_changed(lv_event_t *event)
{
    auto *self = static_cast<WifiView *>(lv_event_get_user_data(event));
    if (self->updating_) return;
    self->view_model_.set_enabled(lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED));
}

void WifiView::network_clicked(lv_event_t *event)
{
    auto *self         = static_cast<WifiView *>(lv_event_get_user_data(event));
    const auto encoded = reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (encoded == 0) return;
    if (self->view_model_.activate_network(encoded - 1U) == WifiActionResult::password_required)
        self->show_password_dialog();
}

void WifiView::password_cancel_clicked(lv_event_t *event)
{
    auto *self = static_cast<WifiView *>(lv_event_get_user_data(event));
    self->view_model_.cancel_pending_network();
    self->hide_password_dialog();
}

void WifiView::password_connect_clicked(lv_event_t *event)
{
    static_cast<WifiView *>(lv_event_get_user_data(event))->connect_pending_network();
}

void WifiView::password_input_ready(lv_event_t *event)
{
    static_cast<WifiView *>(lv_event_get_user_data(event))->connect_pending_network();
}

void WifiView::render() noexcept
{
    const auto &state = view_model_.presentation();
    updating_         = true;
    components::set_switch(switch_, state.enabled, state.control_enabled);
    updating_ = false;
    components::set_text(status_, state.status.c_str());
    if (rendered_generation_ != state.list_generation || rendered_available_ != state.available ||
        rendered_enabled_ != state.enabled)
        rebuild_networks();
}

void WifiView::rebuild_networks()
{
    lv_obj_clean(connected_group_);
    lv_obj_clean(network_group_);
    const auto &state = view_model_.presentation();
    if (!state.available) {
        create_empty_row(connected_group_, "Wi-Fi Unavailable");
        create_empty_row(network_group_, "Wi-Fi Unavailable");
    } else if (!state.enabled) {
        create_empty_row(connected_group_, "Wi-Fi Is Off");
        create_empty_row(network_group_, "Wi-Fi Is Off");
    } else if (view_model_.networks().empty()) {
        create_empty_row(connected_group_, "Not Connected");
        create_empty_row(network_group_, "No Networks Found");
    } else {
        std::size_t connected_count = 0;
        std::size_t other_count     = 0;
        const auto &networks        = view_model_.networks();
        for (std::size_t index = 0; index < networks.size(); ++index) {
            const auto &network = networks[index];
            lv_obj_t *group     = network.connected ? connected_group_ : network_group_;
            const bool locked   = network.security != "Open";
            lv_obj_t *row =
                components::create_dynamic_row(group, network.ssid.c_str(), network.connected ? "Connected" : "",
                                               locked ? network.security.c_str() : "Open", network.connected, locked);
            lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<std::uintptr_t>(index + 1U)));
            lv_obj_add_event_cb(row, network_clicked, LV_EVENT_CLICKED, this);
            network.connected ? ++connected_count : ++other_count;
        }
        if (connected_count == 0)
            create_empty_row(connected_group_, "Not Connected");
        else
            set_group_height(connected_group_, connected_count);
        if (other_count == 0)
            create_empty_row(network_group_, "No Other Networks");
        else
            set_group_height(network_group_, other_count);
    }
    rendered_generation_ = state.list_generation;
    rendered_available_  = state.available;
    rendered_enabled_    = state.enabled;
}

void WifiView::show_password_dialog()
{
    std::string title = "Join " + view_model_.pending_ssid();
    components::set_text(password_title_, title.c_str());
    lv_textarea_set_text(password_input_, "");
    lv_keyboard_set_textarea(keyboard_, password_input_);
    lv_obj_remove_flag(password_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(password_overlay_);
    lv_obj_send_event(password_input_, LV_EVENT_FOCUSED, nullptr);
}

void WifiView::hide_password_dialog() noexcept
{
    lv_keyboard_set_textarea(keyboard_, nullptr);
    lv_obj_add_flag(password_overlay_, LV_OBJ_FLAG_HIDDEN);
}

void WifiView::connect_pending_network()
{
    const char *password = lv_textarea_get_text(password_input_);
    if (!view_model_.connect_pending_network(password)) return;
    hide_password_dialog();
    lv_textarea_set_text(password_input_, "");
}

void WifiView::create_empty_row(lv_obj_t *group, const char *message)
{
    lv_obj_t *row = components::create_dynamic_row(group, message, "", "", false, false);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    set_group_height(group, 1);
}

void WifiView::set_group_height(lv_obj_t *group, std::size_t rows) noexcept
{
    if (group) lv_obj_set_height(group, static_cast<std::int32_t>(rows * 83U));
}

}  // namespace lilygo::settings
