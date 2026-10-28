#include "pages/wifi/wifi_view.hpp"

#include "components/ui_helpers.hpp"
#include "components/settings_widgets.hpp"

#include <cm0/input.h>
#include <cm0/typography.h>

#include <cstdint>
#include <algorithm>

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

lv_obj_t *create_text_button(lv_obj_t *parent, const char *name, const char *text)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_name_static(button, name);
    lv_obj_set_size(button, LV_PCT(100), 52);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::theme::container_background.rgb), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::theme::pressed_background.rgb), LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 10, 0);
    auto *label = components::create_label(button, text, 18, components::theme::accent);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t *create_detail_value(lv_obj_t *group, const char *caption, const char *name)
{
    auto *row = lv_obj_create(group);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 68, 0);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_set_style_pad_row(row, 4, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(components::theme::border.rgb), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    auto *title = components::create_label(row, caption, 18, components::theme::container_title);
    lv_obj_set_width(title, LV_PCT(44));
    auto *value = components::create_label(row, "--", 18, components::theme::muted_text, name);
    lv_obj_set_width(value, LV_PCT(56));
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    // IPv6 addresses, DNS lists and long interface names must wrap, never truncate.
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_add_event_cb(
        row,
        [](lv_event_t *event) {
            auto *item         = lv_event_get_target_obj(event);
            const bool stacked = lv_obj_get_content_width(item) < 360;
            const auto flow    = stacked ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW;
            if (lv_obj_get_style_flex_flow(item, LV_PART_MAIN) == flow) return;
            lv_obj_set_flex_flow(item, flow);
            lv_obj_set_width(lv_obj_get_child(item, 0), LV_PCT(stacked ? 100 : 44));
            lv_obj_set_width(lv_obj_get_child(item, 1), LV_PCT(stacked ? 100 : 56));
            lv_obj_set_style_text_align(lv_obj_get_child(item, 1), stacked ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT,
                                        0);
        },
        LV_EVENT_SIZE_CHANGED, nullptr);
    return value;
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
    constrain_width(components::create_section_label(root_, "CURRENT NETWORK"));
    connected_group_ = components::create_group(root_, "settings_wifi_connected_group");
    constrain_width(connected_group_);
    constrain_width(components::create_section_label(root_, "KNOWN NETWORKS"));
    known_group_ = components::create_group(root_, "settings_wifi_known_group");
    constrain_width(known_group_);
    known_status_ =
        components::create_label(root_, "", 14, components::theme::muted_text, "settings_wifi_known_status");
    constrain_width(known_status_);
    constrain_width(components::create_section_label(root_, "OTHER NETWORKS"));
    network_group_ = components::create_group(root_, "settings_wifi_network_group");
    constrain_width(network_group_);
    lv_obj_t *footnote =
        components::create_label(root_, "Networks you join are saved on this device. Tap a known network to reconnect.",
                                 14, components::theme::muted_text);
    constrain_width(footnote);

    create_details();

    password_overlay_ = lv_obj_create(root_);
    cm0_input_set_focus_scope(password_overlay_);
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
    lv_obj_add_event_cb(keyboard_, password_keyboard_ready, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, password_keyboard_cancelled, LV_EVENT_CANCEL, this);
    lv_obj_add_event_cb(root_, layout_changed, LV_EVENT_SIZE_CHANGED, this);
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
    known_group_ = known_status_ = details_ = details_title_ = details_status_ = details_content_ = details_forget_ =
        manage_overlay_ = manage_title_ = manage_message_ = manage_join_ = manage_forget_ = manage_forget_label_ =
            nullptr;
    detail_tabs_.fill(nullptr);
    detail_panes_.fill(nullptr);
    detail_values_.fill(nullptr);
    managed_uuid_.clear();
    managed_ssid_.clear();
    confirming_forget_         = false;
    rendered_generation_       = std::numeric_limits<std::size_t>::max();
    rendered_known_generation_ = std::numeric_limits<std::size_t>::max();
    rendered_available_        = false;
    rendered_enabled_          = false;
    updating_                  = false;
}

void WifiView::create_details()
{
    details_ = components::create_page(root_, "settings_wifi_details");
    cm0_input_set_focus_scope(details_);
    lv_obj_add_flag(details_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(details_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(details_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_hor(details_, 16, 0);
    lv_obj_set_style_pad_bottom(details_, 12, 0);
    lv_obj_set_style_pad_row(details_, 8, 0);
    auto header = components::create_detail_header(details_, "Wi-Fi", "settings_wifi_details_back");
    constrain_width(header.root);
    lv_obj_add_event_cb(header.back, details_back_clicked, LV_EVENT_CLICKED, this);

    details_title_ = header.title;
    lv_obj_set_name_static(details_title_, "settings_wifi_details_ssid");
    details_status_ =
        components::create_label(details_, "", 14, components::theme::muted_text, "settings_wifi_details_status");
    constrain_width(details_status_);
    lv_obj_set_style_text_align(details_status_, LV_TEXT_ALIGN_CENTER, 0);

    auto *tabs = components::create_group(details_, "settings_wifi_detail_tabs");
    constrain_width(tabs);
    lv_obj_set_height(tabs, 52);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(tabs, 3, 0);
    lv_obj_set_style_pad_column(tabs, 3, 0);
    const char *titles[]    = {"TCP/IP", "DNS", "Hardware"};
    const char *tab_names[] = {"settings_wifi_tab_tcpip", "settings_wifi_tab_dns", "settings_wifi_tab_hardware"};
    for (std::size_t i = 0; i < detail_tabs_.size(); ++i) {
        auto *tab       = create_text_button(tabs, tab_names[i], titles[i]);
        detail_tabs_[i] = tab;
        lv_obj_set_width(tab, 0);
        lv_obj_set_height(tab, LV_PCT(100));
        lv_obj_set_flex_grow(tab, 1);
        lv_obj_set_style_text_font(lv_obj_get_child(tab, 0), lilygo_ui_font_get(16), 0);
        lv_obj_set_user_data(tab, reinterpret_cast<void *>(i));
        lv_obj_add_event_cb(tab, details_tab_clicked, LV_EVENT_CLICKED, this);
    }

    details_content_ = components::create_page(details_, "settings_wifi_details_content");
    constrain_width(details_content_);
    lv_obj_set_height(details_content_, 0);
    lv_obj_set_flex_grow(details_content_, 1);
    lv_obj_set_style_pad_bottom(details_content_, 8, 0);
    const char *pane_names[] = {"settings_wifi_tcpip", "settings_wifi_dns", "settings_wifi_hardware"};
    for (std::size_t i = 0; i < detail_panes_.size(); ++i) {
        detail_panes_[i] = components::create_page(details_content_, pane_names[i]);
        lv_obj_set_height(detail_panes_[i], LV_SIZE_CONTENT);
        lv_obj_remove_flag(detail_panes_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_row(detail_panes_[i], 10, 0);
    }

    (void)components::create_section_label(detail_panes_[0], "IPV4 ADDRESS");
    auto *ipv4        = components::create_group(detail_panes_[0]);
    detail_values_[0] = create_detail_value(ipv4, "Configure IPv4", "settings_wifi_ipv4_method");
    detail_values_[1] = create_detail_value(ipv4, "IP Address", "settings_wifi_ipv4_address");
    detail_values_[2] = create_detail_value(ipv4, "Subnet Mask", "settings_wifi_ipv4_subnet");
    detail_values_[3] = create_detail_value(ipv4, "Router", "settings_wifi_ipv4_gateway");
    (void)components::create_section_label(detail_panes_[0], "IPV6 ADDRESS");
    auto *ipv6        = components::create_group(detail_panes_[0]);
    detail_values_[4] = create_detail_value(ipv6, "Configure IPv6", "settings_wifi_ipv6_method");
    detail_values_[5] = create_detail_value(ipv6, "IP Address", "settings_wifi_ipv6_address");
    detail_values_[6] = create_detail_value(ipv6, "Router", "settings_wifi_ipv6_gateway");

    (void)components::create_section_label(detail_panes_[1], "DNS SERVERS");
    auto *dns = components::create_surface(detail_panes_[1]);
    detail_values_[7] =
        components::create_label(dns, "--", 20, components::theme::muted_text, "settings_wifi_dns_servers");
    (void)components::create_section_label(detail_panes_[1], "SEARCH DOMAINS");
    auto *search = components::create_surface(detail_panes_[1]);
    detail_values_[8] =
        components::create_label(search, "--", 20, components::theme::muted_text, "settings_wifi_dns_search_domains");

    (void)components::create_section_label(detail_panes_[2], "WI-FI HARDWARE");
    auto *hardware     = components::create_group(detail_panes_[2]);
    detail_values_[9]  = create_detail_value(hardware, "Interface", "settings_wifi_interface");
    detail_values_[10] = create_detail_value(hardware, "MAC Address", "settings_wifi_mac");
    detail_values_[11] = create_detail_value(hardware, "MTU", "settings_wifi_mtu");
    details_forget_    = create_text_button(details_, "settings_wifi_details_forget", "Forget This Network");
    constrain_width(details_forget_);
    lv_obj_add_event_cb(details_forget_, details_forget_clicked, LV_EVENT_CLICKED, this);
    select_details_tab(0);

    manage_overlay_ = lv_obj_create(root_);
    lv_obj_remove_style_all(manage_overlay_);
    lv_obj_set_name_static(manage_overlay_, "settings_wifi_manage_overlay");
    cm0_input_set_focus_scope(manage_overlay_);
    lv_obj_add_flag(manage_overlay_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(manage_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(manage_overlay_, lv_color_hex(components::theme::modal_scrim.rgb), 0);
    lv_obj_set_style_bg_opa(manage_overlay_, components::theme::modal_scrim.opacity, 0);
    lv_obj_remove_flag(manage_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    auto *sheet = components::create_surface(manage_overlay_, "settings_wifi_manage_sheet");
    lv_obj_set_width(sheet, LV_PCT(90));
    lv_obj_set_style_max_width(sheet, 460, 0);
    lv_obj_center(sheet);
    manage_title_ =
        components::create_label(sheet, "", 22, components::theme::container_title, "settings_wifi_manage_title");
    manage_message_      = components::create_label(sheet, "", 16, components::theme::muted_text);
    manage_join_         = create_text_button(sheet, "settings_wifi_known_join", "Join Network");
    manage_forget_       = create_text_button(sheet, "settings_wifi_forget_confirm", "Forget This Network");
    manage_forget_label_ = lv_obj_get_child(manage_forget_, 0);
    auto *cancel         = create_text_button(sheet, "settings_wifi_manage_cancel", "Cancel");
    lv_obj_add_event_cb(manage_join_, manage_join_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(manage_forget_, manage_forget_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(cancel, manage_cancel_clicked, LV_EVENT_CLICKED, this);
    resize_overlays();
}

void WifiView::select_details_tab(std::size_t index)
{
    if (index >= detail_tabs_.size()) return;
    for (std::size_t i = 0; i < detail_tabs_.size(); ++i) {
        lv_obj_set_flag(detail_panes_[i], LV_OBJ_FLAG_HIDDEN, i != index);
        lv_obj_set_state(detail_tabs_[i], LV_STATE_CHECKED, i == index);
        lv_obj_set_style_bg_color(
            detail_tabs_[i],
            lv_color_hex((i == index ? components::theme::accent : components::theme::container_background).rgb), 0);
        lv_obj_set_style_text_color(
            lv_obj_get_child(detail_tabs_[i], 0),
            lv_color_hex((i == index ? components::theme::accent_content : components::theme::accent).rgb), 0);
    }
    lv_obj_scroll_to_y(details_content_, 0, LV_ANIM_OFF);
}

void WifiView::details_tab_clicked(lv_event_t *event)
{
    auto *self = static_cast<WifiView *>(lv_event_get_user_data(event));
    self->select_details_tab(reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
}

void WifiView::layout_changed(lv_event_t *event)
{
    static_cast<WifiView *>(lv_event_get_user_data(event))->resize_overlays();
}

void WifiView::resize_overlays()
{
    if (!root_) return;
    // Full-page overlays follow their actual parent, including its padded edges.
    for (auto *overlay : {details_, manage_overlay_}) {
        if (!overlay) continue;
        lv_obj_set_size(overlay, lv_obj_get_width(root_), lv_obj_get_height(root_));
        lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, -lv_obj_get_style_pad_left(root_, LV_PART_MAIN),
                     -lv_obj_get_style_pad_top(root_, LV_PART_MAIN));
    }
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
    if (encoded <= self->view_model_.networks().size() && self->view_model_.networks()[encoded - 1U].connected) {
        self->show_details();
        return;
    }
    if (self->view_model_.activate_network(encoded - 1U) == WifiActionResult::password_required)
        self->show_password_dialog();
}

void WifiView::known_clicked(lv_event_t *event)
{
    auto *self         = static_cast<WifiView *>(lv_event_get_user_data(event));
    const auto encoded = reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (encoded == 0 || encoded > self->view_model_.known_networks().size()) return;
    if (self->view_model_.known_networks()[encoded - 1U].connected)
        self->show_details();
    else if (!self->view_model_.presentation().enabled) {
        const auto &network = self->view_model_.known_networks()[encoded - 1U];
        self->show_network_actions(network.uuid, network.ssid, false);
    } else
        (void)self->view_model_.activate_known_network(encoded - 1U);
}

void WifiView::known_info_clicked(lv_event_t *event)
{
    auto *self         = static_cast<WifiView *>(lv_event_get_user_data(event));
    const auto encoded = reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (encoded == 0 || encoded > self->view_model_.known_networks().size()) return;
    const auto &network = self->view_model_.known_networks()[encoded - 1U];
    if (network.connected)
        self->show_details();
    else
        self->show_network_actions(network.uuid, network.ssid, false);
}

void WifiView::show_details()
{
    select_details_tab(0);
    resize_overlays();
    view_model_.open_details();
    lv_obj_move_foreground(details_);
}

void WifiView::details_back_clicked(lv_event_t *event)
{
    static_cast<WifiView *>(lv_event_get_user_data(event))->view_model_.close_details();
}

void WifiView::render_details()
{
    const auto &state = view_model_.presentation();
    lv_obj_set_flag(details_, LV_OBJ_FLAG_HIDDEN, !state.details_open);
    if (!state.details_open) return;
    const auto &details = view_model_.details();
    components::set_text(details_title_, details.ssid.c_str());
    components::set_text(details_status_, state.details_status.c_str());
    const std::string *values[] = {&details.ipv4_method,  &details.ipv4_address, &details.subnet_mask,
                                   &details.ipv4_gateway, &details.ipv6_method,  &details.ipv6_address,
                                   &details.ipv6_gateway, &details.dns_servers,  &details.search_domains,
                                   &details.interface,    &details.mac,          &details.mtu};
    for (std::size_t i = 0; i < detail_values_.size(); ++i)
        components::set_text(detail_values_[i], state.details_available ? values[i]->c_str() : "--");
    lv_obj_set_state(details_forget_, LV_STATE_DISABLED,
                     details.profile_uuid.empty() || !state.control_enabled || state.details_loading);
}

void WifiView::details_forget_clicked(lv_event_t *event)
{
    auto *self          = static_cast<WifiView *>(lv_event_get_user_data(event));
    const auto &details = self->view_model_.details();
    if (!details.profile_uuid.empty()) self->show_network_actions(details.profile_uuid, details.ssid, true);
}

void WifiView::show_network_actions(const std::string &uuid, const std::string &ssid, bool confirm)
{
    resize_overlays();
    managed_uuid_      = uuid;
    managed_ssid_      = ssid;
    confirming_forget_ = confirm;
    components::set_text(manage_title_, ssid.c_str());
    components::set_text(manage_message_,
                         confirm ? "Forget this network and its saved password? You will need to join it again."
                                 : "This network is saved on your device.");
    components::set_text(manage_forget_label_, confirm ? "Forget Network" : "Forget This Network");
    lv_obj_set_flag(manage_join_, LV_OBJ_FLAG_HIDDEN, confirm);
    lv_obj_set_state(manage_join_, LV_STATE_DISABLED,
                     !view_model_.presentation().enabled || !view_model_.presentation().control_enabled);
    lv_obj_remove_flag(manage_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(manage_overlay_);
}

void WifiView::hide_network_actions()
{
    lv_obj_add_flag(manage_overlay_, LV_OBJ_FLAG_HIDDEN);
    managed_uuid_.clear();
    managed_ssid_.clear();
    confirming_forget_ = false;
}

void WifiView::manage_cancel_clicked(lv_event_t *event)
{
    static_cast<WifiView *>(lv_event_get_user_data(event))->hide_network_actions();
}

void WifiView::manage_join_clicked(lv_event_t *event)
{
    auto *self           = static_cast<WifiView *>(lv_event_get_user_data(event));
    const auto &networks = self->view_model_.known_networks();
    const auto found     = std::find_if(networks.begin(), networks.end(),
                                        [&](const auto &network) { return network.uuid == self->managed_uuid_; });
    if (found != networks.end())
        (void)self->view_model_.activate_known_network(static_cast<std::size_t>(found - networks.begin()));
    self->hide_network_actions();
}

void WifiView::manage_forget_clicked(lv_event_t *event)
{
    auto *self = static_cast<WifiView *>(lv_event_get_user_data(event));
    if (!self->confirming_forget_) {
        self->show_network_actions(self->managed_uuid_, self->managed_ssid_, true);
        return;
    }
    (void)self->view_model_.forget_network(self->managed_uuid_);
    self->hide_network_actions();
}

lv_obj_t *WifiView::add_info_button(lv_obj_t *row, const char *name, std::size_t index, lv_event_cb_t callback)
{
    static const std::int32_t columns[] = {24, LV_GRID_FR(1), 20, 44, LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {LV_GRID_FR(1), LV_GRID_FR(1), 1, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(row, columns, rows);
    auto *lock = lv_obj_get_child(row, 3);
    lv_obj_set_grid_cell(lock, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 2);
    lv_obj_add_flag(lv_obj_get_child(row, 4), LV_OBJ_FLAG_HIDDEN);
    auto *info = create_text_button(row, "settings_wifi_info", "\xEF\x81\x9A");  // Font Awesome info-circle.
    lv_obj_set_name(info, name);
    lv_obj_set_size(info, 44, 44);
    lv_obj_set_style_radius(info, 22, 0);
    lv_obj_set_grid_cell(info, LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 2);
    lv_obj_remove_flag(info, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_user_data(info, reinterpret_cast<void *>(index + 1U));
    lv_obj_add_event_cb(info, callback, LV_EVENT_CLICKED, this);
    return info;
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

void WifiView::password_keyboard_ready(lv_event_t *event)
{
    static_cast<WifiView *>(lv_event_get_user_data(event))->connect_pending_network();
}

void WifiView::password_keyboard_cancelled(lv_event_t *event)
{
    auto *self = static_cast<WifiView *>(lv_event_get_user_data(event));
    self->view_model_.cancel_pending_network();
    self->hide_password_dialog();
}

void WifiView::render() noexcept
{
    const auto &state = view_model_.presentation();
    updating_         = true;
    components::set_switch(switch_, state.enabled, state.control_enabled);
    updating_ = false;
    components::set_text(status_, state.status.c_str());
    const bool repeats_current_network =
        std::any_of(view_model_.networks().begin(), view_model_.networks().end(),
                    [&](const auto &network) { return network.connected && network.ssid == state.status; });
    lv_obj_set_flag(status_, LV_OBJ_FLAG_HIDDEN, repeats_current_network);
    components::set_text(known_status_, state.known_status.c_str());
    lv_obj_set_flag(known_status_, LV_OBJ_FLAG_HIDDEN, state.known_status.empty());
    if (rendered_generation_ != state.list_generation || rendered_available_ != state.available ||
        rendered_enabled_ != state.enabled || rendered_known_generation_ != state.known_generation)
        rebuild_networks();
    if (view_model_.known_networks().empty()) {
        auto *row = lv_obj_get_child(known_group_, 0);
        if (row)
            components::set_text(lv_obj_get_child(row, 1),
                                 state.known_status.empty() ? "Reading Known Networks..." : state.known_status.c_str());
    }
    render_details();
}

void WifiView::rebuild_networks()
{
    lv_obj_clean(connected_group_);
    lv_obj_clean(network_group_);
    lv_obj_clean(known_group_);
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
            const bool known    = std::any_of(
                view_model_.known_networks().begin(), view_model_.known_networks().end(), [&](const auto &item) {
                    return item.ssid == network.ssid && (item.security == "Open") == (network.security == "Open");
                });
            if (!network.connected && known) continue;
            lv_obj_t *group   = network.connected ? connected_group_ : network_group_;
            const bool locked = network.security != "Open";
            lv_obj_t *row =
                components::create_dynamic_row(group, network.ssid.c_str(), network.connected ? "Connected" : "",
                                               locked ? network.security.c_str() : "Open", network.connected, locked);
            lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<std::uintptr_t>(index + 1U)));
            lv_obj_add_event_cb(row, network_clicked, LV_EVENT_CLICKED, this);
            if (network.connected)
                (void)add_info_button(row, "settings_wifi_connected_details", index, network_clicked);
            else {
                lv_obj_set_grid_cell(lv_obj_get_child(row, 1), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 2);
                lv_obj_add_flag(lv_obj_get_child(row, 2), LV_OBJ_FLAG_HIDDEN);
            }
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
    const auto &known = view_model_.known_networks();
    for (std::size_t index = 0; index < known.size(); ++index) {
        const auto &network = known[index];
        auto *row           = components::create_dynamic_row(known_group_, network.ssid.c_str(),
                                                   network.connected  ? "Connected"
                                                             : network.in_range ? "Saved"
                                                                                : "Not in Range",
                                                             "", network.connected, network.security != "Open");
        lv_obj_set_user_data(row, reinterpret_cast<void *>(index + 1U));
        lv_obj_add_event_cb(row, known_clicked, LV_EVENT_CLICKED, this);
        const auto name = "settings_wifi_known_info_" + std::to_string(index);
        (void)add_info_button(row, name.c_str(), index, known_info_clicked);
    }
    if (known.empty())
        create_empty_row(known_group_, "Reading Known Networks...");
    else
        set_group_height(known_group_, known.size());
    rendered_generation_       = state.list_generation;
    rendered_known_generation_ = state.known_generation;
    rendered_available_        = state.available;
    rendered_enabled_          = state.enabled;
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
    auto *row = components::create_dynamic_row(group, message, "", "", false, false, false);
    lv_obj_set_grid_cell(lv_obj_get_child(row, 1), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 2);
    lv_obj_add_flag(lv_obj_get_child(row, 2), LV_OBJ_FLAG_HIDDEN);
    set_group_height(group, 1);
}

void WifiView::set_group_height(lv_obj_t *group, std::size_t rows) noexcept
{
    if (group) lv_obj_set_height(group, static_cast<std::int32_t>(rows * 83U));
}

}  // namespace lilygo::settings
