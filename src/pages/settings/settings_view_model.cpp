#include "pages/settings/settings_view_model.hpp"

#include <utility>

namespace lilygo::settings {

SettingsViewModel::SettingsViewModel(SettingsEnvironment environment)
    : model_(std::move(environment)),
      home_(model_),
      wifi_(model_, commands_),
      bluetooth_(model_, commands_),
      ethernet_(model_),
      battery_(model_),
      general_(model_),
      about_(model_)
{
}

SettingsViewModel::~SettingsViewModel()
{
    stop();
}

void SettingsViewModel::refresh()
{
    if (commands_.busy()) {
        refresh_pending_ = true;
        return;
    }
    refresh_pending_ = false;
    model_.refresh_all();
    wifi_.refresh();
    bluetooth_.refresh();
    publish_passive_pages();
    wifi_radio_sync_pending_ = true;
    bluetooth_sync_pending_  = true;
    start_next_background_command();
}

void SettingsViewModel::poll_command()
{
    const auto result = commands_.poll();
    if (!result) {
        publish_command_state_if_changed();
        return;
    }

    switch (result->command) {
        case SettingsCommand::wifi_scan:
        case SettingsCommand::wifi_radio_sync:
        case SettingsCommand::wifi_sync:
        case SettingsCommand::wifi_connect:
        case SettingsCommand::wifi_disconnect:
        case SettingsCommand::wifi_power:
            wifi_.command_finished(*result);
            break;
        case SettingsCommand::bluetooth_scan:
        case SettingsCommand::bluetooth_devices:
        case SettingsCommand::bluetooth_sync:
        case SettingsCommand::bluetooth_connect:
        case SettingsCommand::bluetooth_power:
            bluetooth_.command_finished(*result);
            break;
        case SettingsCommand::none:
            break;
    }
    if (result->command == SettingsCommand::wifi_radio_sync) wifi_sync_pending_ = true;
    if (result->success) {
        if (result->command == SettingsCommand::wifi_connect || result->command == SettingsCommand::wifi_disconnect)
            wifi_sync_pending_ = true;
        else if (result->command == SettingsCommand::wifi_power)
            wifi_radio_sync_pending_ = true;
        else if (result->command == SettingsCommand::bluetooth_power)
            bluetooth_sync_pending_ = true;
    }
    publish_passive_pages();
    publish_command_state_if_changed();

    if (commands_.busy()) return;
    if (bluetooth_.resume_pending_scan()) {
        bluetooth_sync_pending_ = false;
        return;
    }
    if (wifi_radio_sync_pending_ || wifi_sync_pending_ || bluetooth_sync_pending_) {
        start_next_background_command();
        if (commands_.busy()) return;
    }
    if (refresh_pending_) {
        refresh();
        return;
    }
}

void SettingsViewModel::stop() noexcept
{
    commands_.stop();
    bluetooth_.cancel_pending_scan();
    refresh_pending_ = wifi_radio_sync_pending_ = wifi_sync_pending_ = bluetooth_sync_pending_ = false;
    published_foreground_busy_                                                                 = false;
}
bool SettingsViewModel::busy() const noexcept
{
    return commands_.busy();
}
HomeViewModel &SettingsViewModel::home() noexcept
{
    return home_;
}
WifiViewModel &SettingsViewModel::wifi() noexcept
{
    return wifi_;
}
BluetoothViewModel &SettingsViewModel::bluetooth() noexcept
{
    return bluetooth_;
}
EthernetViewModel &SettingsViewModel::ethernet() noexcept
{
    return ethernet_;
}
BatteryViewModel &SettingsViewModel::battery() noexcept
{
    return battery_;
}
GeneralViewModel &SettingsViewModel::general() noexcept
{
    return general_;
}
AboutViewModel &SettingsViewModel::about() noexcept
{
    return about_;
}

void SettingsViewModel::publish_passive_pages()
{
    home_.publish();
    ethernet_.publish();
    battery_.publish();
    general_.publish();
    about_.publish();
}

void SettingsViewModel::publish_command_state_if_changed()
{
    const bool foreground_busy = commands_.busy() && !settings_command_is_background(commands_.active_command());
    if (foreground_busy == published_foreground_busy_) return;
    published_foreground_busy_ = foreground_busy;
    wifi_.publish();
    bluetooth_.publish();
}

void SettingsViewModel::start_next_background_command()
{
    if (commands_.busy()) return;
    if (wifi_radio_sync_pending_) {
        wifi_radio_sync_pending_ = false;
        if (wifi_.sync_radio()) return;
    }
    if (wifi_sync_pending_) {
        wifi_sync_pending_ = false;
        if (wifi_.sync()) return;
    }
    if (bluetooth_sync_pending_) {
        bluetooth_sync_pending_ = false;
        (void)bluetooth_.sync_controller();
    }
}

}  // namespace lilygo::settings
