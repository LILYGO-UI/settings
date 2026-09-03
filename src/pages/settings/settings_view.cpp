#include "pages/settings/settings_view.hpp"

#include "components/settings_theme.hpp"
#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cstdio>
#include <cstring>

namespace lilygo::settings {

SettingsView::SettingsView(SettingsViewModel &view_model, AppRouter &router) noexcept
    : view_model_(view_model),
      router_(router),
      home_view_(view_model.home()),
      wifi_view_(view_model.wifi()),
      bluetooth_view_(view_model.bluetooth()),
      ethernet_view_(view_model.ethernet()),
      battery_view_(view_model.battery()),
      general_view_(view_model.general()),
      about_view_(view_model.about())
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

    view_model_.refresh();
    if (!create_pages() || !bind_navigation()) {
        std::fprintf(stderr, "[settings] UI contract is incomplete\n");
        destroy();
        return nullptr;
    }

    router_.attach(pages_);
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
    battery_view_.destroy();
    general_view_.destroy();
    about_view_.destroy();
    if (surface_ && lv_obj_is_valid(surface_)) lv_obj_delete(surface_);
    pages_.fill(nullptr);
    surface_ = nullptr;
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
    else if (std::strcmp(object_name, "settings_home_battery_row") == 0)
        destination = SettingsPage::battery;
    else if (std::strcmp(object_name, "settings_home_general_row") == 0)
        destination = SettingsPage::general;
    else if (std::strcmp(object_name, "settings_general_about_row") == 0)
        destination = SettingsPage::about;
    else if (std::strcmp(object_name, "settings_about_back") == 0)
        destination = SettingsPage::general;
    else if (std::strstr(object_name, "_back") != nullptr)
        destination = SettingsPage::home;
    else
        return;

    router_.navigate(destination);
    if (destination == SettingsPage::wifi && view_model_.wifi().networks().empty())
        view_model_.wifi().scan();
    else if (destination == SettingsPage::bluetooth && view_model_.bluetooth().devices().empty())
        view_model_.bluetooth().scan();
}

bool SettingsView::create_pages()
{
    pages_[static_cast<std::size_t>(SettingsPage::home)]      = home_view_.create(surface_);
    pages_[static_cast<std::size_t>(SettingsPage::wifi)]      = wifi_view_.create(surface_);
    pages_[static_cast<std::size_t>(SettingsPage::bluetooth)] = bluetooth_view_.create(surface_);
    pages_[static_cast<std::size_t>(SettingsPage::ethernet)]  = ethernet_view_.create(surface_);
    pages_[static_cast<std::size_t>(SettingsPage::battery)]   = battery_view_.create(surface_);
    pages_[static_cast<std::size_t>(SettingsPage::general)]   = general_view_.create(surface_);
    pages_[static_cast<std::size_t>(SettingsPage::about)]     = about_view_.create(surface_);
    for (lv_obj_t *page : pages_) {
        if (!page) return false;
    }
    return true;
}

bool SettingsView::bind_navigation()
{
    constexpr const char *navigation_objects[] = {
        "settings_home_wifi_row",    "settings_home_bluetooth_row", "settings_home_ethernet_row",
        "settings_home_battery_row", "settings_home_general_row",   "settings_wifi_back",
        "settings_bluetooth_back",   "settings_ethernet_back",      "settings_battery_back",
        "settings_general_back",     "settings_general_about_row",  "settings_about_back",
    };
    for (const char *name : navigation_objects) {
        lv_obj_t *object = components::find(surface_, name);
        if (!object) return false;
        lv_obj_add_event_cb(object, navigation_clicked, LV_EVENT_CLICKED, this);
    }
    return true;
}

}  // namespace lilygo::settings
