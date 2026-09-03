#ifndef LILYGO_UI_SETTINGS_APP_ROUTER_HPP
#define LILYGO_UI_SETTINGS_APP_ROUTER_HPP

#include <lvgl.h>

#include <array>
#include <cstddef>

namespace lilygo::settings {

enum class SettingsPage : std::size_t {
    home,
    wifi,
    bluetooth,
    ethernet,
    battery,
    general,
    about,
    count,
};

class AppRouter {
public:
    void attach(std::array<lv_obj_t *, static_cast<std::size_t>(SettingsPage::count)> pages) noexcept;
    void navigate(SettingsPage page) noexcept;
    void reset() noexcept;
    [[nodiscard]] SettingsPage current_page() const noexcept;

private:
    std::array<lv_obj_t *, static_cast<std::size_t>(SettingsPage::count)> pages_{};
    SettingsPage current_page_ = SettingsPage::home;
};

}  // namespace lilygo::settings

#endif
