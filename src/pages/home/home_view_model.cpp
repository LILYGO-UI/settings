#include "pages/home/home_view_model.hpp"

#include <cstdio>

namespace lilygo::settings {

HomeViewModel::HomeViewModel(SettingsModel &model) noexcept : model_(model)
{
}

void HomeViewModel::publish()
{
    const auto *connected = model_.connected_wifi();
    if (!model_.wifi_available())
        presentation_.wifi = "Unavailable";
    else if (!model_.wifi_on())
        presentation_.wifi = "Off";
    else if (connected)
        presentation_.wifi = connected->ssid;
    else if (!model_.wifi_interfaces().empty() && model_.wifi_interfaces().front().connected)
        presentation_.wifi = "Connected";
    else
        presentation_.wifi = "Not Connected";

    if (!model_.bluetooth_available())
        presentation_.bluetooth = "Unavailable";
    else
        presentation_.bluetooth = model_.bluetooth_on() ? "On" : "Off";

    if (model_.ethernet_interfaces().empty())
        presentation_.ethernet = "Unavailable";
    else
        presentation_.ethernet = model_.ethernet_interfaces().front().connected ? "Connected" : "Not Connected";

    const auto &battery = model_.battery();
    if (!battery.available)
        presentation_.battery = "Unavailable";
    else if (battery.has_capacity) {
        char text[32];
        std::snprintf(text, sizeof(text), "%d%%", battery.capacity_percent);
        presentation_.battery = text;
    } else
        presentation_.battery = battery.status;
    revision_.publish();
}

const HomePresentation &HomeViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *HomeViewModel::revision_subject() noexcept
{
    return revision_.get();
}

}  // namespace lilygo::settings
