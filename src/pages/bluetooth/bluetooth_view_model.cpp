#include "pages/bluetooth/bluetooth_view_model.hpp"

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
    if (sync_controller()) return true;
    scan_pending_    = false;
    status_override_ = "Cannot Start Operation";
    publish();
    return false;
}

void BluetoothViewModel::cancel_pending_scan() noexcept
{
    scan_pending_ = false;
}

void BluetoothViewModel::refresh()
{
    status_override_.clear();
    publish();
}

void BluetoothViewModel::scan()
{
    if (!model_.bluetooth_on()) {
        if (commands_.busy() && settings_command_is_background(commands_.active_command()) &&
            model_.bluetoothctl_available()) {
            scan_pending_    = true;
            status_override_ = "Preparing Bluetooth...";
            publish();
            return;
        }
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
    (void)start(SettingsCommand::bluetooth_connect,
                {model_.environment().bluetoothctl, "--timeout", "15", "connect", device.address}, "Connecting...");
}

void BluetoothViewModel::command_finished(const SettingsCommandResult &result)
{
    switch (result.command) {
        case SettingsCommand::bluetooth_scan:
            if (result.success) {
                (void)start(SettingsCommand::bluetooth_devices, {model_.environment().bluetoothctl, "devices"},
                            "Loading Devices...");
            } else {
                status_override_ = "Bluetooth Search Failed";
                publish();
            }
            break;
        case SettingsCommand::bluetooth_devices:
            if (!result.success)
                status_override_ = "Bluetooth Search Failed";
            else if (!has_output(result.output)) {
                model_.clear_bluetooth_devices();
                status_override_ = "No Devices Found";
            } else if (model_.update_bluetooth_devices(result.output))
                status_override_ = "Devices Updated";
            else
                status_override_ = "Bluetooth Search Failed";
            publish();
            break;
        case SettingsCommand::bluetooth_sync: {
            const bool scan_after_sync = scan_pending_;
            scan_pending_              = false;
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
            if (scan_after_sync && model_.bluetooth_on() && model_.bluetooth_uses_service_backend()) scan();
            break;
        }
        case SettingsCommand::bluetooth_connect:
            status_override_ = result.success ? "Connected" : "Connection Failed";
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

bool BluetoothViewModel::start(SettingsCommand command, std::vector<std::string> arguments, const char *progress)
{
    if (commands_.busy() && settings_command_is_background(commands_.active_command())) commands_.stop();
    if (commands_.busy()) {
        status_override_ = "Another Operation Is In Progress";
        publish();
        return false;
    }
    if (!commands_.start(command, arguments)) {
        status_override_ = "Cannot Start Operation";
        publish();
        return false;
    }
    status_override_ = progress;
    publish();
    return true;
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
