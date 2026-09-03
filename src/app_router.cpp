#include "app_router.hpp"

namespace lilygo::settings {

void AppRouter::attach(std::array<lv_obj_t *, static_cast<std::size_t>(SettingsPage::count)> pages) noexcept
{
    pages_ = pages;
    navigate(SettingsPage::home);
}

void AppRouter::navigate(SettingsPage page) noexcept
{
    const auto destination = static_cast<std::size_t>(page);
    if (destination >= pages_.size() || !pages_[destination]) return;
    for (std::size_t index = 0; index < pages_.size(); ++index) {
        if (!pages_[index]) continue;
        if (index == destination)
            lv_obj_remove_flag(pages_[index], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(pages_[index], LV_OBJ_FLAG_HIDDEN);
    }
    current_page_ = page;
}

void AppRouter::reset() noexcept
{
    pages_.fill(nullptr);
    current_page_ = SettingsPage::home;
}

SettingsPage AppRouter::current_page() const noexcept
{
    return current_page_;
}

}  // namespace lilygo::settings
