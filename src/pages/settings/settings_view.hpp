#ifndef LILYGO_UI_SETTINGS_PAGES_SETTINGS_SETTINGS_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_SETTINGS_SETTINGS_VIEW_HPP

#include "app_router.hpp"
#include "pages/about/about_view.hpp"
#include "pages/battery/battery_view.hpp"
#include "pages/bluetooth/bluetooth_view.hpp"
#include "pages/ethernet/ethernet_view.hpp"
#include "pages/cellular/cellular_view.hpp"
#include "pages/general/general_view.hpp"
#include "pages/home/home_view.hpp"
#include "pages/keyboard/keyboard_view.hpp"
#include "pages/settings/settings_view_model.hpp"
#include "pages/wifi/wifi_view.hpp"
#include "pages/sharing/sharing_view.hpp"

#include <lvgl.h>

#include <array>

namespace lilygo::settings {

class SettingsView {
public:
    SettingsView(SettingsViewModel &view_model, AppRouter &router) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    static void refresh_timer(lv_timer_t *timer);
    static void command_timer(lv_timer_t *timer);
    static void surface_size_changed(lv_event_t *event);
    static void navigation_clicked(lv_event_t *event);
    void navigate_from(const char *object_name);
    void update_layout() noexcept;
    void update_landscape_selection() noexcept;
    [[nodiscard]] bool create_landscape_sidebar();
    [[nodiscard]] bool create_pages();
    [[nodiscard]] bool bind_navigation();

    SettingsViewModel &view_model_;
    AppRouter &router_;
    HomeView home_view_;
    WifiView wifi_view_;
    BluetoothView bluetooth_view_;
    EthernetView ethernet_view_;
    CellularView cellular_view_;
    BatteryView battery_view_;
    KeyboardView keyboard_view_;
    GeneralView general_view_;
    AboutView about_view_;
    SharingView sharing_view_;
    lv_obj_t *surface_ = nullptr;
    lv_obj_t *content_ = nullptr;
    lv_obj_t *landscape_sidebar_ = nullptr;
    std::array<lv_obj_t *, static_cast<std::size_t>(SettingsPage::count)> pages_{};
    lv_timer_t *refresh_timer_ = nullptr;
    lv_timer_t *command_timer_ = nullptr;
    bool landscape_ = false;
    bool updating_layout_ = false;
};

}  // namespace lilygo::settings

#endif
