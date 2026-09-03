#include "pages/battery/battery_view_model.hpp"

#include <algorithm>
#include <cstdio>

namespace lilygo::settings {

BatteryViewModel::BatteryViewModel(SettingsModel &model) noexcept : model_(model)
{
}

void BatteryViewModel::publish()
{
    const auto &battery = model_.battery();
    if (!battery.available) {
        presentation_ = {"--",
                         "No Battery Detected",
                         "--",
                         "--",
                         "--",
                         "--",
                         "--",
                         "--",
                         "--",
                         0,
                         components::theme::battery_healthy.rgb};
        revision_.publish();
        return;
    }

    char text[96];
    presentation_.percent   = "--";
    presentation_.bar_value = 0;
    if (battery.has_capacity) {
        std::snprintf(text, sizeof(text), "%d%%", battery.capacity_percent);
        presentation_.percent   = text;
        presentation_.bar_value = std::clamp(battery.capacity_percent, 0, 100);
    }
    presentation_.bar_color  = battery.capacity_percent < 20   ? components::theme::battery_critical.rgb
                               : battery.capacity_percent < 50 ? components::theme::battery_low.rgb
                                                               : components::theme::battery_healthy.rgb;
    presentation_.status     = battery.status;
    presentation_.technology = battery.technology;
    presentation_.health     = battery.health;

    if (battery.has_charge_full && battery.has_charge_full_design && battery.charge_full_design_uah > 0) {
        const auto health = std::min<std::int64_t>(100, battery.charge_full_uah * 100 / battery.charge_full_design_uah);
        std::snprintf(text, sizeof(text), "%lld%%", static_cast<long long>(health));
        presentation_.capacity = text;
    } else
        presentation_.capacity = "--";

    if (battery.has_cycle_count) {
        std::snprintf(text, sizeof(text), "%d", battery.cycle_count);
        presentation_.cycles = text;
    } else
        presentation_.cycles = "--";
    if (battery.has_voltage) {
        std::snprintf(text, sizeof(text), "%.3f V", static_cast<double>(battery.voltage_uv) / 1000000.0);
        presentation_.voltage = text;
    } else
        presentation_.voltage = "--";
    if (battery.has_current) {
        std::snprintf(text, sizeof(text), "%+.3f A", static_cast<double>(battery.current_ua) / 1000000.0);
        presentation_.current = text;
    } else
        presentation_.current = "--";
    if (battery.has_temperature) {
        std::snprintf(text, sizeof(text), "%.1f C", static_cast<double>(battery.temperature_tenths_c) / 10.0);
        presentation_.temperature = text;
    } else
        presentation_.temperature = "--";
    revision_.publish();
}

const BatteryPresentation &BatteryViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *BatteryViewModel::revision_subject() noexcept
{
    return revision_.get();
}

}  // namespace lilygo::settings
