#include "pages/settings/settings_view.hpp"

#include "assets/settings_icons.hpp"
#include "components/settings_theme.hpp"
#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

#include <cstdio>
#include <cstdint>
#include <cstring>

namespace lilygo::settings {

namespace {

constexpr const char *kLandscapeRows[] = {
    "settings_landscape_wifi_row",     "settings_landscape_bluetooth_row",
    "settings_landscape_ethernet_row", "settings_landscape_battery_row",
    "settings_landscape_keyboard_row", "settings_landscape_general_row",
    "settings_landscape_cellular_row",
};

void style_landscape_row(lv_obj_t *row, bool selected)
{
    if (!row) return;
    lv_obj_set_style_bg_color(row, lv_color_hex(components::theme::navigation_selected.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, selected ? components::theme::navigation_selected.opacity : LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_set_style_radius(row, selected ? 8 : 0, LV_PART_MAIN);

    const std::uint32_t child_count = lv_obj_get_child_count(row);
    for (std::uint32_t index = 0; index < child_count; ++index) {
        lv_obj_t *child = lv_obj_get_child(row, static_cast<std::int32_t>(index));
        if (!child || !lv_obj_check_type(child, &lv_label_class)) continue;
        components::theme::SemanticColor color = components::theme::container_title;
        if (index == 0)
            color = components::theme::primary_action;
        else if (index == 2)
            color = components::theme::muted_text;
        else if (index == 3)
            color = components::theme::disclosure;
        if (selected) color = components::theme::accent_content;
        lv_obj_set_style_text_color(child, lv_color_hex(color.rgb), LV_PART_MAIN);
        lv_obj_set_style_text_opa(child, color.opacity, LV_PART_MAIN);
    }
}

}  // namespace

SettingsView::SettingsView(SettingsViewModel &view_model, AppRouter &router) noexcept
    : view_model_(view_model),
      router_(router),
      home_view_(view_model.home()),
      wifi_view_(view_model.wifi()),
      bluetooth_view_(view_model.bluetooth()),
      ethernet_view_(view_model.ethernet()),
      cellular_view_(view_model.cellular()),
      battery_view_(view_model.battery()),
      keyboard_view_(view_model.keyboard()),
      general_view_(view_model.general()),
      about_view_(view_model.about()),
      sharing_view_(view_model.sharing())
{
}

lv_obj_t *SettingsView::create(lv_obj_t *parent)
{
    if (!parent || !lv_obj_is_valid(parent) || !components::fonts_available()) return nullptr;
    destroy();
    surface_ = lv_obj_create(parent);
    if (!surface_) return nullptr;
    lv_obj_set_name_static(surface_, "settings_surface");
    lv_obj_remove_style_all(surface_);
    lv_obj_set_size(surface_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(surface_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(surface_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(surface_, lv_color_hex(components::theme::page_background.rgb), 0);
    lv_obj_set_style_bg_opa(surface_, components::theme::page_background.opacity, 0);
    static const std::int32_t surface_columns[] = {LV_GRID_FR(3), LV_GRID_FR(7), LV_GRID_TEMPLATE_LAST};
    static const std::int32_t surface_rows[]    = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(surface_, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(surface_, surface_columns, surface_rows);
    lv_obj_add_event_cb(surface_, surface_size_changed, LV_EVENT_SIZE_CHANGED, this);

    content_ = lv_obj_create(surface_);
    if (!content_) {
        destroy();
        return nullptr;
    }
    lv_obj_set_name_static(content_, "settings_content");
    lv_obj_remove_style_all(content_);
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_height(content_, LV_PCT(100));
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    view_model_.refresh();
    if (!create_pages() || !create_landscape_sidebar() || !bind_navigation()) {
        std::fprintf(stderr, "[settings] UI contract is incomplete\n");
        destroy();
        return nullptr;
    }

    router_.attach(pages_);
    update_layout();
    view_model_.wifi().scan();
    refresh_timer_ = lv_timer_create(refresh_timer, 5000, this);
    command_timer_ = lv_timer_create(command_timer, 200, this);
    return surface_;
}

void SettingsView::destroy() noexcept
{
    if (refresh_timer_) lv_timer_delete(refresh_timer_);
    if (command_timer_) lv_timer_delete(command_timer_);
    refresh_timer_ = command_timer_ = nullptr;
    view_model_.stop();

    router_.reset();
    home_view_.destroy();
    wifi_view_.destroy();
    bluetooth_view_.destroy();
    ethernet_view_.destroy();
    cellular_view_.destroy();
    battery_view_.destroy();
    keyboard_view_.destroy();
    general_view_.destroy();
    about_view_.destroy();
    sharing_view_.destroy();
    if (surface_ && lv_obj_is_valid(surface_)) lv_obj_delete(surface_);
    pages_.fill(nullptr);
    content_ = nullptr;
    landscape_sidebar_ = nullptr;
    surface_ = nullptr;
    landscape_ = false;
    updating_layout_ = false;
}

void SettingsView::refresh_timer(lv_timer_t *timer)
{
    auto *self = static_cast<SettingsView *>(lv_timer_get_user_data(timer));
    self->view_model_.refresh();
}

void SettingsView::command_timer(lv_timer_t *timer)
{
    auto *self = static_cast<SettingsView *>(lv_timer_get_user_data(timer));
    self->view_model_.poll_command();
}

void SettingsView::surface_size_changed(lv_event_t *event)
{
    auto *self = static_cast<SettingsView *>(lv_event_get_user_data(event));
    if (self) self->update_layout();
}

void SettingsView::navigation_clicked(lv_event_t *event)
{
    auto *self = static_cast<SettingsView *>(lv_event_get_user_data(event));
    self->navigate_from(lv_obj_get_name(lv_event_get_current_target_obj(event)));
}

void SettingsView::navigate_from(const char *object_name)
{
    if (!object_name) return;
    SettingsPage destination = router_.current_page();
    if (std::strcmp(object_name, "settings_home_wifi_row") == 0)
        destination = SettingsPage::wifi;
    else if (std::strcmp(object_name, "settings_home_bluetooth_row") == 0)
        destination = SettingsPage::bluetooth;
    else if (std::strcmp(object_name, "settings_home_ethernet_row") == 0)
        destination = SettingsPage::ethernet;
    else if (std::strcmp(object_name, "settings_home_cellular_row") == 0 ||
             std::strcmp(object_name, "settings_landscape_cellular_row") == 0)
        destination = SettingsPage::cellular;
    else if (std::strcmp(object_name, "settings_home_battery_row") == 0)
        destination = SettingsPage::battery;
    else if (std::strcmp(object_name, "settings_home_general_row") == 0)
        destination = SettingsPage::general;
    else if (std::strcmp(object_name, "settings_home_keyboard_row") == 0)
        destination = SettingsPage::keyboard;
    else if (std::strcmp(object_name, "settings_landscape_wifi_row") == 0)
        destination = SettingsPage::wifi;
    else if (std::strcmp(object_name, "settings_landscape_bluetooth_row") == 0)
        destination = SettingsPage::bluetooth;
    else if (std::strcmp(object_name, "settings_landscape_ethernet_row") == 0)
        destination = SettingsPage::ethernet;
    else if (std::strcmp(object_name, "settings_landscape_battery_row") == 0)
        destination = SettingsPage::battery;
    else if (std::strcmp(object_name, "settings_landscape_keyboard_row") == 0)
        destination = SettingsPage::keyboard;
    else if (std::strcmp(object_name, "settings_landscape_general_row") == 0)
        destination = SettingsPage::general;
    else if (std::strcmp(object_name, "settings_general_about_row") == 0)
        destination = SettingsPage::about;
    else if (std::strcmp(object_name, "settings_general_sharing_row") == 0)
        destination = SettingsPage::sharing;
    else if (std::strcmp(object_name, "settings_sharing_back") == 0)
        destination = SettingsPage::general;
    else if (std::strcmp(object_name, "settings_about_back") == 0)
        destination = SettingsPage::general;
    else if (std::strstr(object_name, "_back") != nullptr)
        destination = SettingsPage::home;
    else
        return;

    router_.navigate(destination);
    update_landscape_selection();
    if (destination == SettingsPage::sharing) view_model_.sharing().refresh();
    if (destination == SettingsPage::cellular) view_model_.cellular().refresh();
    if (destination == SettingsPage::keyboard) view_model_.keyboard().refresh();
    if (destination == SettingsPage::wifi && view_model_.wifi().networks().empty())
        view_model_.wifi().scan();
    else if (destination == SettingsPage::bluetooth && view_model_.bluetooth().devices().empty())
        view_model_.bluetooth().scan();
}

void SettingsView::update_layout() noexcept
{
    if (!surface_ || !lv_obj_is_valid(surface_) || !landscape_sidebar_ || !content_ || updating_layout_) return;

    const auto width  = lv_obj_get_content_width(surface_);
    const auto height = lv_obj_get_content_height(surface_);
    if (width <= 0 || height <= 0) return;

    updating_layout_ = true;
    landscape_       = width > height;
    if (landscape_) {
        lv_obj_clear_flag(landscape_sidebar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_grid_cell(landscape_sidebar_, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_grid_cell(content_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    } else {
        lv_obj_add_flag(landscape_sidebar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_grid_cell(content_, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    }
    home_view_.set_landscape(landscape_);
    lv_obj_mark_layout_as_dirty(surface_);
    updating_layout_ = false;
    update_landscape_selection();
}

void SettingsView::update_landscape_selection() noexcept
{
    if (!landscape_sidebar_) return;
    const auto current = router_.current_page();
    const char *selected_name = nullptr;
    switch (current) {
        case SettingsPage::wifi:
            selected_name = kLandscapeRows[0];
            break;
        case SettingsPage::bluetooth:
            selected_name = kLandscapeRows[1];
            break;
        case SettingsPage::ethernet:
            selected_name = kLandscapeRows[2];
            break;
        case SettingsPage::battery:
            selected_name = kLandscapeRows[3];
            break;
        case SettingsPage::cellular:
            selected_name = kLandscapeRows[6];
            break;
        case SettingsPage::keyboard:
            selected_name = kLandscapeRows[4];
            break;
        case SettingsPage::general:
        case SettingsPage::about:
        case SettingsPage::sharing:
            selected_name = kLandscapeRows[5];
            break;
        case SettingsPage::home:
        case SettingsPage::count:
            break;
    }
    for (const char *name : kLandscapeRows) {
        lv_obj_t *row = components::find(landscape_sidebar_, name);
        style_landscape_row(row, selected_name && std::strcmp(name, selected_name) == 0);
    }
}

bool SettingsView::create_landscape_sidebar()
{
    landscape_sidebar_ = lv_obj_create(surface_);
    if (!landscape_sidebar_) return false;
    lv_obj_set_name_static(landscape_sidebar_, "settings_landscape_sidebar");
    lv_obj_remove_style_all(landscape_sidebar_);
    lv_obj_set_width(landscape_sidebar_, LV_PCT(100));
    lv_obj_set_style_max_width(landscape_sidebar_, 360, LV_PART_MAIN);
    lv_obj_set_height(landscape_sidebar_, LV_PCT(100));
    lv_obj_set_style_pad_left(landscape_sidebar_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(landscape_sidebar_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(landscape_sidebar_, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(landscape_sidebar_, 48, LV_PART_MAIN);
    lv_obj_set_style_pad_row(landscape_sidebar_, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(landscape_sidebar_, lv_color_hex(components::theme::container_background.rgb),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(landscape_sidebar_, components::theme::container_background.opacity, LV_PART_MAIN);
    lv_obj_set_style_border_color(landscape_sidebar_, lv_color_hex(components::theme::border.rgb), LV_PART_MAIN);
    lv_obj_set_style_border_opa(landscape_sidebar_, components::theme::border.opacity, LV_PART_MAIN);
    lv_obj_set_style_border_width(landscape_sidebar_, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(landscape_sidebar_, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN);
    lv_obj_set_layout(landscape_sidebar_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(landscape_sidebar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(landscape_sidebar_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(landscape_sidebar_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(landscape_sidebar_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(landscape_sidebar_, LV_OBJ_FLAG_SCROLLABLE);
    // The first size event selects the correct arrangement once the parent has
    // a resolved geometry; keep the portrait-safe state until then.
    lv_obj_add_flag(landscape_sidebar_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = components::create_label(landscape_sidebar_, "Settings", 38, components::theme::container_title,
                                                "settings_landscape_title");
    if (!title) return false;
    lv_obj_set_height(title, 44);

    // Keep the navigation categories visually aligned with the compact home
    // page: connectivity is one group, followed by General and Keyboard.
    lv_obj_t *connectivity_group = components::create_group(landscape_sidebar_, "settings_landscape_connectivity_group");
    lv_obj_t *general_group = components::create_group(landscape_sidebar_, "settings_landscape_general_group");
    lv_obj_t *keyboard_group = components::create_group(landscape_sidebar_, "settings_landscape_keyboard_group");
    if (!connectivity_group || !general_group || !keyboard_group) return false;

    const auto wifi = components::create_image_menu_row(connectivity_group, &assets::wifi_icon, "Wi-Fi", "", false,
                                                         false, kLandscapeRows[0]);
    const auto bluetooth = components::create_image_menu_row(connectivity_group, &assets::bluetooth_icon, "Bluetooth", "",
                                                              false, false, kLandscapeRows[1]);
    const auto ethernet = components::create_image_menu_row(connectivity_group, &assets::ethernet_icon, "Ethernet", "",
                                                            false, false, kLandscapeRows[2]);
    const auto cellular = components::create_menu_row(connectivity_group, LV_SYMBOL_CALL, "Cellular", "", false,
                                                       false, kLandscapeRows[6]);
    const auto battery = components::create_image_menu_row(connectivity_group, &assets::battery_icon, "Battery", "", false,
                                                           false, kLandscapeRows[3]);
    const auto general = components::create_image_menu_row(general_group, &assets::general_icon, "General", "", false,
                                                           false, kLandscapeRows[5]);
    const auto keyboard = components::create_menu_row(keyboard_group, LV_SYMBOL_KEYBOARD, "Keyboard", "", false, false,
                                                       kLandscapeRows[4]);
    for (lv_obj_t *row : {wifi.root, bluetooth.root, ethernet.root, cellular.root, battery.root, keyboard.root, general.root}) {
        if (!row) return false;
        lv_obj_set_height(row, 60);
    }
    update_landscape_selection();
    return true;
}

bool SettingsView::create_pages()
{
    pages_[static_cast<std::size_t>(SettingsPage::home)]      = home_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::wifi)]      = wifi_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::bluetooth)] = bluetooth_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::ethernet)]  = ethernet_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::cellular)]  = cellular_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::battery)]   = battery_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::keyboard)]  = keyboard_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::general)]   = general_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::about)]     = about_view_.create(content_);
    pages_[static_cast<std::size_t>(SettingsPage::sharing)]   = sharing_view_.create(content_, surface_);
    for (lv_obj_t *page : pages_) {
        if (!page) return false;
    }
    return true;
}

bool SettingsView::bind_navigation()
{
    constexpr const char *navigation_objects[] = {
        "settings_home_wifi_row",     "settings_home_bluetooth_row", "settings_home_ethernet_row",
        "settings_home_battery_row",  "settings_home_general_row",   "settings_wifi_back",
        "settings_home_cellular_row", "settings_cellular_back", "settings_landscape_cellular_row",
        "settings_bluetooth_back",    "settings_ethernet_back",      "settings_battery_back",
        "settings_general_back",      "settings_general_about_row",  "settings_about_back",
        "settings_general_sharing_row", "settings_sharing_back",
        "settings_home_keyboard_row", "settings_keyboard_back", "settings_landscape_wifi_row",
        "settings_landscape_bluetooth_row", "settings_landscape_ethernet_row", "settings_landscape_battery_row",
        "settings_landscape_keyboard_row", "settings_landscape_general_row",
    };
    for (const char *name : navigation_objects) {
        lv_obj_t *object = components::find(surface_, name);
        if (!object) return false;
        lv_obj_add_event_cb(object, navigation_clicked, LV_EVENT_CLICKED, this);
    }
    return true;
}

}  // namespace lilygo::settings
