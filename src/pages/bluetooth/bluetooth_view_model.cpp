#include "pages/bluetooth/bluetooth_view_model.hpp"

#include "domain/settings_platform.hpp"

#include <chrono>
#include <string_view>

namespace lilygo::settings {
namespace {

constexpr auto background_command_timeout = std::chrono::seconds(4);

bool has_output(std::string_view output)
{
    return output.find_first_not_of(" \t\r\n") != std::string_view::npos;
}

bool reports_unavailable_service(std::string_view output)
{
    return output.find("BlueZ service unavailable") != std::string_view::npos ||
           output.find("Failed to connect to bluetoothd") != std::string_view::npos;
}

bool is_scan_command(SettingsCommand command)
{
    return command == SettingsCommand::bluetooth_scan || command == SettingsCommand::bluetooth_devices ||
           command == SettingsCommand::bluetooth_paired_devices || command == SettingsCommand::bluetooth_dbus_devices ||
           command == SettingsCommand::bluetooth_info;
}

}  // namespace

BluetoothViewModel::BluetoothViewModel(SettingsModel &model, SettingsCommandRunner &commands) noexcept
    : model_(model), commands_(commands)
{
}

void BluetoothViewModel::publish()
{
    update_list_generation();
    const bool foreground_busy    = commands_.busy() && !settings_command_is_background(commands_.active_command());
    presentation_.available       = model_.bluetooth_available();
    presentation_.enabled         = model_.bluetooth_on();
    presentation_.control_enabled = model_.bluetooth_control_enabled() && !foreground_busy;
    presentation_.status          = status_override_.empty() ? derived_status() : status_override_;
    presentation_.list_generation = list_generation_;
    revision_.publish();
}

bool BluetoothViewModel::sync_controller()
{
    if (!model_.bluetoothctl_available() || commands_.busy()) return false;
    return commands_.start(SettingsCommand::bluetooth_sync, {model_.environment().bluetoothctl, "show"}, {},
                           background_command_timeout);
}

bool BluetoothViewModel::resume_pending_scan()
{
    if (!scan_pending_ || commands_.busy()) return false;
    if (!model_.bluetoothctl_available()) {
        scan_pending_    = false;
        status_override_ = "BlueZ Is Unavailable";
        publish();
        return false;
    }
    if (model_.bluetooth_on() && model_.bluetooth_uses_service_backend()) {
        scan_pending_ = false;
        scan();
        return commands_.busy();
    }
    if (sync_controller()) return true;
    scan_pending_    = false;
    status_override_ = "Cannot Start Operation";
    publish();
    return false;
}

void BluetoothViewModel::cancel_pending_scan() noexcept
{
    scan_pending_    = false;
    continuous_scan_ = false;
    pending_pair_address_.clear();
    clear_pending_device_info();
}

void BluetoothViewModel::start_continuous_scan()
{
    continuous_scan_ = true;
    if (commands_.busy()) {
        scan_pending_    = true;
        status_override_ = "Preparing Bluetooth...";
        publish();
        return;
    }
    scan();
}

void BluetoothViewModel::stop_continuous_scan() noexcept
{
    continuous_scan_ = false;
    scan_pending_    = false;
    clear_pending_device_info();
    if (commands_.busy() && is_scan_command(commands_.active_command()))
        commands_.stop();
}

void BluetoothViewModel::refresh()
{
    status_override_.clear();
    if (continuous_scan_) scan_pending_ = true;
    publish();
}

void BluetoothViewModel::scan()
{
    if (commands_.busy()) {
        if (settings_command_is_background(commands_.active_command())) {
            scan_pending_    = true;
            status_override_ = "Preparing Bluetooth...";
            publish();
            return;
        }
        status_override_ = "Another Operation Is In Progress";
        publish();
        return;
    }
    if (!model_.bluetooth_on()) {
        status_override_ = "Bluetooth Is Off";
        publish();
        return;
    }
    if (!model_.bluetooth_uses_service_backend()) {
        status_override_ = "BlueZ Is Unavailable";
        publish();
        return;
    }
    scan_pending_ = false;
    (void)start(SettingsCommand::bluetooth_scan, {model_.environment().bluetoothctl, "--timeout", "8", "scan", "on"},
                "Searching...");
}

void BluetoothViewModel::set_enabled(bool enabled)
{
    if (!enabled) scan_pending_ = false;
    if (commands_.busy() && !settings_command_is_background(commands_.active_command())) {
        status_override_ = "Another Operation Is In Progress";
        publish();
        return;
    }
    if (!model_.bluetooth_control_enabled()) {
        status_override_ = "Cannot Change Bluetooth";
        publish();
        return;
    }

    if (model_.bluetooth_uses_service_backend()) {
        model_.begin_bluetooth_power_change(enabled);
        if (!start(SettingsCommand::bluetooth_power,
                   {model_.environment().bluetoothctl, "power", enabled ? "on" : "off"},
                   enabled ? "Turning On..." : "Turning Off...")) {
            model_.complete_bluetooth_power_change(false);
            model_.refresh_connectivity();
            publish();
        }
        return;
    }

    if (commands_.busy()) commands_.stop();
    const bool rfkill_changed = model_.set_bluetooth_radio_enabled(enabled);
    model_.refresh_connectivity();
    status_override_ = rfkill_changed ? (enabled ? "On" : "Off") : "Cannot Change Bluetooth";
    publish();
}

void BluetoothViewModel::connect(std::size_t index)
{
    if (index >= model_.bluetooth_devices().size() || !model_.bluetooth_on()) return;
    if (!model_.bluetooth_uses_service_backend()) {
        status_override_ = "BlueZ Is Unavailable";
        publish();
        return;
    }
    const auto &device = model_.bluetooth_devices()[index];
    if (commands_.busy() && is_scan_command(commands_.active_command())) {
        commands_.stop();
        clear_pending_device_info();
    }
    scan_pending_ = continuous_scan_;
    if (device.paired) {
        (void)start(SettingsCommand::bluetooth_connect,
                    {model_.environment().bluetoothctl, "--timeout", "15", "connect", device.address},
                    "Connecting...");
    } else {
        pending_pair_address_ = device.address;
        if (!start(SettingsCommand::bluetooth_pair,
                   {model_.environment().bluetoothctl, "--timeout", "30", "pair", device.address}, "Pairing..."))
            pending_pair_address_.clear();
    }
}

void BluetoothViewModel::command_finished(const SettingsCommandResult &result)
{
    switch (result.command) {
        case SettingsCommand::bluetooth_scan:
            if (result.success) {
                // Some BlueZ versions report the friendly name in the scan
                // event but omit it from the subsequent `devices` listing.
                // Keep that name cached so the second command cannot erase it.
                (void)model_.update_bluetooth_devices(result.output);
                (void)start_device_listing();
            } else {
                status_override_ = "Bluetooth Search Failed";
                publish();
                request_next_scan();
            }
            break;
        case SettingsCommand::bluetooth_devices:
        case SettingsCommand::bluetooth_dbus_devices:
            if (result.command == SettingsCommand::bluetooth_dbus_devices && !result.success) {
                // Keep a graceful fallback for systems where the helper or
                // system bus is unavailable. The application still supports
                // the legacy bluetoothctl listing in that case.
                (void)start(SettingsCommand::bluetooth_devices,
                            {model_.environment().bluetoothctl, "devices"}, "Loading Devices...");
                break;
            }
            if (!result.success)
                status_override_ = "Bluetooth Search Failed";
            else if (!has_output(result.output) && result.command == SettingsCommand::bluetooth_dbus_devices)
                model_.clear_bluetooth_devices();
            else if (has_output(result.output) && !model_.update_bluetooth_devices(result.output)) {
                status_override_ = "Bluetooth Search Failed";
            } else if (result.command == SettingsCommand::bluetooth_devices) {
                if (continuous_scan_ && start_paired_device_listing()) return;
                if (!has_output(result.output)) model_.clear_bluetooth_devices();
            }
            if (result.command == SettingsCommand::bluetooth_devices && !result.success) {
                request_next_scan();
                publish();
                break;
            }
            finish_device_listing();
            break;
        case SettingsCommand::bluetooth_paired_devices:
            if (result.success && has_output(result.output)) {
                if (!model_.update_bluetooth_paired_devices(result.output))
                    status_override_ = "Bluetooth Search Failed";
            } else if (!result.success && model_.bluetooth_devices().empty()) {
                status_override_ = "Bluetooth Search Failed";
            }
            finish_device_listing();
            break;
        case SettingsCommand::bluetooth_info:
            if (pending_info_index_ < pending_info_addresses_.size()) {
                const auto address = pending_info_addresses_[pending_info_index_];
                (void)model_.update_bluetooth_device_name(address, result.output);
                ++pending_info_index_;
            }
            if (start_next_device_info()) return;
            clear_pending_device_info();
            status_override_ = "Devices Updated";
            publish();
            request_next_scan();
            break;
        case SettingsCommand::bluetooth_sync: {
            const bool scan_after_sync = scan_pending_ || continuous_scan_;
            scan_pending_              = scan_after_sync;
            status_override_.clear();
            if (result.success) {
                if (!model_.update_bluetooth_controller(result.output)) {
                    model_.mark_bluetooth_service_refresh_failed();
                    status_override_ = "Bluetooth Refresh Failed";
                }
            } else if (result.output.find("No default controller available") != std::string::npos) {
                model_.mark_bluetooth_controller_unavailable();
            } else if (reports_unavailable_service(result.output)) {
                model_.mark_bluetooth_service_unavailable();
            } else {
                model_.mark_bluetooth_service_refresh_failed();
                status_override_ = "Bluetooth Refresh Failed";
            }
            model_.refresh_connectivity();
            publish();
            break;
        }
        case SettingsCommand::bluetooth_connect:
            status_override_ = result.success ? "Connected" : "Connection Failed";
            publish();
            break;
        case SettingsCommand::bluetooth_pair:
            if (result.success && !pending_pair_address_.empty()) {
                (void)model_.mark_bluetooth_device_paired(pending_pair_address_);
                status_override_ = "Paired";
            } else {
                status_override_ = "Pairing Failed";
            }
            pending_pair_address_.clear();
            publish();
            break;
        case SettingsCommand::bluetooth_power:
            model_.complete_bluetooth_power_change(result.success);
            model_.refresh_connectivity();
            status_override_ = result.success ? "Bluetooth Updated" : "Cannot Change Bluetooth";
            publish();
            if (result.success && model_.bluetooth_on()) scan();
            break;
        default:
            break;
    }
}

const BluetoothPresentation &BluetoothViewModel::presentation() const noexcept
{
    return presentation_;
}

const std::vector<BluetoothDevice> &BluetoothViewModel::devices() const noexcept
{
    return model_.bluetooth_devices();
}

lv_subject_t *BluetoothViewModel::revision_subject() noexcept
{
    return revision_.get();
}

bool BluetoothViewModel::start(SettingsCommand command, std::vector<std::string> arguments, const char *progress,
                               std::chrono::milliseconds timeout)
{
    if (commands_.busy() && settings_command_is_background(commands_.active_command())) {
        if (commands_.active_command() == SettingsCommand::bluetooth_info) clear_pending_device_info();
        commands_.stop();
    }
    if (commands_.busy()) {
        status_override_ = "Another Operation Is In Progress";
        publish();
        return false;
    }
    if (!commands_.start(command, arguments, {}, timeout)) {
        status_override_ = "Cannot Start Operation";
        publish();
        return false;
    }
    status_override_ = progress;
    publish();
    return true;
}

bool BluetoothViewModel::start_next_device_info()
{
    if (pending_info_index_ >= pending_info_addresses_.size()) return false;
    return start(SettingsCommand::bluetooth_info,
                 {model_.environment().bluetoothctl, "info", pending_info_addresses_[pending_info_index_]},
                 "Loading Device Names...", background_command_timeout);
}

void BluetoothViewModel::finish_device_listing()
{
    pending_info_addresses_.clear();
    pending_info_index_ = 0;
    for (const auto &device : model_.bluetooth_devices()) {
        if (device.name.empty()) pending_info_addresses_.push_back(device.address);
    }
    if (!pending_info_addresses_.empty() && start_next_device_info()) return;
    if (model_.bluetooth_devices().empty())
        status_override_ = "No Devices Found";
    else if (status_override_ != "Bluetooth Search Failed")
        status_override_ = "Devices Updated";
    publish();
    request_next_scan();
}

void BluetoothViewModel::request_next_scan()
{
    if (!continuous_scan_ || !model_.bluetooth_on() || !model_.bluetooth_uses_service_backend()) return;
    scan_pending_ = true;
}

bool BluetoothViewModel::start_paired_device_listing()
{
    const auto &environment = model_.environment();
    return start(SettingsCommand::bluetooth_paired_devices,
                 {environment.bluetoothctl, "paired-devices"}, "Loading My Devices...");
}

bool BluetoothViewModel::start_device_listing()
{
    const auto &environment = model_.environment();
    if (platform::settings_command_available(environment.bluetooth_dbus_helper.c_str()))
        return start(SettingsCommand::bluetooth_dbus_devices, {environment.bluetooth_dbus_helper},
                     "Loading Devices...");
    return start(SettingsCommand::bluetooth_devices, {environment.bluetoothctl, "devices"}, "Loading Devices...");
}

void BluetoothViewModel::clear_pending_device_info() noexcept
{
    pending_info_addresses_.clear();
    pending_info_index_ = 0;
}

void BluetoothViewModel::update_list_generation() noexcept
{
    const bool available               = model_.bluetooth_available();
    const bool enabled                 = model_.bluetooth_on();
    const std::size_t model_generation = model_.bluetooth_device_generation();
    if (!observed_list_state_valid_ || observed_available_ != available || observed_enabled_ != enabled ||
        observed_model_generation_ != model_generation)
        ++list_generation_;
    observed_list_state_valid_ = true;
    observed_available_        = available;
    observed_enabled_          = enabled;
    observed_model_generation_ = model_generation;
}

std::string BluetoothViewModel::derived_status() const
{
    if (!model_.bluetooth_available()) return "Bluetooth Is Unavailable";
    if (model_.bluetooth_radio().hardware_blocked) return "Bluetooth Is Hardware Blocked";
    return model_.bluetooth_on() ? "On" : "Off";
}

}  // namespace lilygo::settings
