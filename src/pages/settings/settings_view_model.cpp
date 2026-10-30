#include "pages/settings/settings_view_model.hpp"

#include <utility>

namespace lilygo::settings {

SettingsViewModel::SettingsViewModel(SettingsEnvironment environment)
    : model_(std::move(environment)),
      home_(model_),
      wifi_(model_, commands_),
      bluetooth_(model_, commands_),
      ethernet_(model_, commands_),
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
    keyboard_.refresh();
    sharing_.refresh();
    cellular_.refresh();
    // Keep battery telemetry and calibration progress live during network commands.
    if (!battery_.busy()) {
        model_.refresh_battery();
        battery_.publish();
        home_.publish();
    }
    if (commands_.busy() || battery_.busy()) {
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
    ethernet_sync_pending_   = true;
    start_next_background_command();
}

void SettingsViewModel::poll_command()
{
    sharing_.poll_command();
    cellular_.poll_command();
    home_.set_cellular_status(cellular_.presentation().summary);
    battery_.poll_command();
    const auto result = commands_.poll();
    if (!result) {
        publish_command_state_if_changed();
        if (!commands_.busy()) start_next_background_command();
        return;
    }

    switch (result->command) {
        case SettingsCommand::wifi_scan:
        case SettingsCommand::wifi_radio_sync:
        case SettingsCommand::wifi_sync:
        case SettingsCommand::wifi_profiles:
        case SettingsCommand::wifi_profile:
        case SettingsCommand::wifi_details:
        case SettingsCommand::wifi_details_profile:
        case SettingsCommand::wifi_forget:
        case SettingsCommand::wifi_connect:
        case SettingsCommand::wifi_disconnect:
        case SettingsCommand::wifi_power:
            wifi_.command_finished(*result);
            break;
        case SettingsCommand::bluetooth_scan:
        case SettingsCommand::bluetooth_devices:
        case SettingsCommand::bluetooth_paired_devices:
        case SettingsCommand::bluetooth_dbus_devices:
        case SettingsCommand::bluetooth_info:
        case SettingsCommand::bluetooth_sync:
        case SettingsCommand::bluetooth_pair:
        case SettingsCommand::bluetooth_connect:
        case SettingsCommand::bluetooth_power:
            bluetooth_.command_finished(*result);
            break;
        case SettingsCommand::ethernet_refresh:
        case SettingsCommand::ethernet_profile_lookup:
        case SettingsCommand::ethernet_modify:
        case SettingsCommand::ethernet_up:
            ethernet_.command_finished(*result);
            break;
        case SettingsCommand::sharing_sync:
        case SettingsCommand::sharing_power:
        case SettingsCommand::serial_console_sync:
        case SettingsCommand::serial_console_change:
        case SettingsCommand::sharing_reboot:
        case SettingsCommand::battery_control:
        case SettingsCommand::none:
            break;
    }
    if (result->command == SettingsCommand::wifi_radio_sync) wifi_sync_pending_ = true;
    if (result->success) {
        if (result->command == SettingsCommand::wifi_connect || result->command == SettingsCommand::wifi_disconnect || result->command == SettingsCommand::wifi_forget)
            wifi_sync_pending_ = true;
        else if (result->command == SettingsCommand::wifi_power)
            wifi_radio_sync_pending_ = true;
        else if (result->command == SettingsCommand::bluetooth_power)
            bluetooth_sync_pending_ = true;
    }
    publish_passive_pages();
    publish_command_state_if_changed();

    if (commands_.busy()) return;
    if (wifi_radio_sync_pending_ || wifi_sync_pending_ || bluetooth_sync_pending_) {
        start_next_background_command();
        if (commands_.busy()) return;
    }
    if (refresh_pending_) {
        refresh();
        return;
    }
    if (bluetooth_.resume_pending_scan()) return;
    if (wifi_.resume_background()) return;
    if (ethernet_sync_pending_) {
        ethernet_sync_pending_ = false;
        if (ethernet_.sync()) return;
    }
}

void SettingsViewModel::stop() noexcept
{
    commands_.stop();
    wifi_.stop();
    sharing_.stop();
    cellular_.stop();
    battery_.stop();
    bluetooth_.cancel_pending_scan();
    refresh_pending_ = wifi_radio_sync_pending_ = wifi_sync_pending_ = bluetooth_sync_pending_ =
        ethernet_sync_pending_ = false;
    published_foreground_busy_                                                                 = false;
}
bool SettingsViewModel::busy() const noexcept
{
    return commands_.busy() || sharing_.busy() || battery_.busy() || cellular_.busy();
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
CellularViewModel &SettingsViewModel::cellular() noexcept
{
    return cellular_;
}
GeneralViewModel &SettingsViewModel::general() noexcept
{
    return general_;
}
KeyboardViewModel &SettingsViewModel::keyboard() noexcept
{
    return keyboard_;
}
AboutViewModel &SettingsViewModel::about() noexcept
{
    return about_;
}

SharingViewModel &SettingsViewModel::sharing() noexcept
{
    return sharing_;
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
    ethernet_.publish();
}

void SettingsViewModel::start_next_background_command()
{
    if (commands_.busy()) return;
    if (wifi_radio_sync_pending_) {
        wifi_radio_sync_pending_ = false;
        if (wifi_.sync_radio()) {
            ethernet_.publish();
            return;
        }
    }
    if (wifi_sync_pending_) {
        wifi_sync_pending_ = false;
        if (wifi_.sync()) {
            ethernet_.publish();
            return;
        }
    }
    if (bluetooth_sync_pending_) {
        bluetooth_sync_pending_ = false;
        (void)bluetooth_.sync_controller();
        if (commands_.busy()) {
            ethernet_.publish();
            return;
        }
    }
    if (wifi_.resume_background()) return;
    if (ethernet_sync_pending_) {
        ethernet_sync_pending_ = false;
        (void)ethernet_.sync();
        ethernet_.publish();
    }
}

}  // namespace lilygo::settings
