#include "pages/home/home_view.hpp"

#include "assets/settings_icons.hpp"
#include "components/settings_theme.hpp"
#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

namespace lilygo::settings {
namespace {

void constrain_width(lv_obj_t *object, std::int32_t maximum = 680)
{
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_style_max_width(object, maximum, 0);
}

}  // namespace

HomeView::HomeView(HomeViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *HomeView::create(lv_obj_t *parent)
{
    destroy();
    root_ = components::create_page(parent, "settings_home");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_top(root_, 14, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 8, 0);

    lv_obj_t *title = components::create_label(root_, "Settings", 38, components::theme::container_title);
    constrain_width(title);
    lv_obj_set_height(title, 52);

    connectivity_ = components::create_group(root_, "settings_home_connectivity_group");
    constrain_width(connectivity_);
    const auto wifi      = components::create_image_menu_row(connectivity_, &assets::wifi_icon, "Wi-Fi", "CM0 Lab", true,
                                                             true, "settings_home_wifi_row");
    const auto bluetooth = components::create_image_menu_row(connectivity_, &assets::bluetooth_icon, "Bluetooth", "On",
                                                             true, true, "settings_home_bluetooth_row");
    const auto ethernet  = components::create_image_menu_row(connectivity_, &assets::ethernet_icon, "Ethernet",
                                                             "Connected", true, true, "settings_home_ethernet_row");
    const auto cellular = components::create_image_menu_row(connectivity_, &assets::cellular_icon, "Cellular", "Checking...",
                                                             true, true, "settings_home_cellular_row");
    const auto battery = components::create_image_menu_row(connectivity_, &assets::battery_icon, "Battery", "82%", false,
                                                           true, "settings_home_battery_row");
    for (lv_obj_t *row : {wifi.root, bluetooth.root, ethernet.root, cellular.root, battery.root}) lv_obj_set_height(row, 64);
    wifi_      = wifi.detail;
    bluetooth_ = bluetooth.detail;
    ethernet_  = ethernet.detail;
    cellular_  = cellular.detail;
    battery_   = battery.detail;

    general_group_ = components::create_group(root_, "settings_home_general_group");
    constrain_width(general_group_);
    const auto general = components::create_image_menu_row(general_group_, &assets::general_icon, "General", "", false, true,
                                                           "settings_home_general_row");
    lv_obj_set_height(general.root, 64);

    keyboard_group_ = components::create_group(root_, "settings_home_keyboard_group");
    constrain_width(keyboard_group_);
    const auto keyboard = components::create_menu_row(keyboard_group_, LV_SYMBOL_KEYBOARD, "Keyboard", "", false, true,
                                                      "settings_home_keyboard_row");
    lv_obj_set_height(keyboard.root, 64);

    footer_ =
        components::create_label(root_, "LILYGO UI", 14, components::theme::muted_text, "settings_home_footer");
    constrain_width(footer_);
    lv_obj_set_style_text_align(footer_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_height(footer_, 20);

    landscape_welcome_ = components::create_surface(root_, "settings_home_landscape_welcome");
    constrain_width(landscape_welcome_);
    lv_obj_set_height(landscape_welcome_, 148);
    lv_obj_set_style_pad_all(landscape_welcome_, 24, 0);
    lv_obj_set_style_pad_row(landscape_welcome_, 10, 0);
    (void)components::create_label(landscape_welcome_, "Choose a category", 28, components::theme::container_title,
                                   "settings_home_landscape_welcome_title");
    (void)components::create_label(landscape_welcome_, "Select a category from the sidebar to view its settings.", 18,
                                   components::theme::muted_text, "settings_home_landscape_welcome_hint");
    landscape_status_ = components::create_label(landscape_welcome_, "CM0 Lab", 22, components::theme::text,
                                                 "settings_home_landscape_status");
    set_landscape(false);

    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void HomeView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = wifi_ = bluetooth_ = ethernet_ = cellular_ = battery_ = connectivity_ = general_group_ = footer_ = keyboard_group_ =
        landscape_welcome_ = landscape_status_ = nullptr;
    landscape_ = false;
}

void HomeView::set_landscape(bool landscape) noexcept
{
    landscape_ = landscape;
    for (lv_obj_t *object : {connectivity_, general_group_, keyboard_group_, footer_}) {
        if (!object) continue;
        lv_obj_set_flag(object, LV_OBJ_FLAG_HIDDEN, landscape);
    }
    if (landscape_welcome_) lv_obj_set_flag(landscape_welcome_, LV_OBJ_FLAG_HIDDEN, !landscape);
    if (root_) lv_obj_mark_layout_as_dirty(root_);
}

void HomeView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<HomeView *>(lv_observer_get_user_data(observer))->render();
}

void HomeView::render() noexcept
{
    const auto &state = view_model_.presentation();
    components::set_text(wifi_, state.wifi.c_str());
    components::set_text(bluetooth_, state.bluetooth.c_str());
    components::set_text(ethernet_, state.ethernet.c_str());
    components::set_text(cellular_, state.cellular.c_str());
    components::set_text(battery_, state.battery.c_str());
    components::set_text(landscape_status_, state.wifi.c_str());
}

}  // namespace lilygo::settings
