#include "pages/wifi/wifi_view_model.hpp"

#include <algorithm>
#include <chrono>

namespace lilygo::settings {
namespace {

constexpr auto background_command_timeout = std::chrono::seconds(4);

bool has_output(std::string_view output)
{
    return output.find_first_not_of(" \t\r\n") != std::string_view::npos;
}

}  // namespace

WifiViewModel::WifiViewModel(SettingsModel &model, SettingsCommandRunner &commands) noexcept
    : model_(model), commands_(commands)
{
}

void WifiViewModel::publish()
{
    update_list_generation();
    const bool foreground_busy    = commands_.busy() && !settings_command_is_background(commands_.active_command());
    presentation_.available       = model_.wifi_available();
    presentation_.enabled         = model_.wifi_on();
    presentation_.control_enabled = model_.wifi_control_enabled() && !foreground_busy;
    presentation_.status          = status_override_.empty() ? derived_status() : status_override_;
    presentation_.list_generation = list_generation_;
    revision_.publish();
}

bool WifiViewModel::sync_radio()
{
    if (!model_.wifi_available() || !model_.nmcli_available() || commands_.busy()) return false;
    return commands_.start(SettingsCommand::wifi_radio_sync, {model_.environment().nmcli, "radio", "wifi"}, {},
                           background_command_timeout);
}

bool WifiViewModel::sync()
{
    if (!model_.wifi_available() || !model_.wifi_on() || !model_.nmcli_available() || commands_.busy()) return false;
    return commands_.start(SettingsCommand::wifi_sync,
                           {model_.environment().nmcli, "-t", "--escape", "yes", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                            "device", "wifi", "list", "--rescan", "no"},
                           {}, background_command_timeout);
}

void WifiViewModel::refresh()
{
    status_override_.clear();
    publish();
}

void WifiViewModel::scan()
{
    if (!model_.wifi_on()) {
        status_override_ = "Wi-Fi Is Off";
        publish();
        return;
    }
    if (!model_.nmcli_available()) {
        status_override_ = "NetworkManager Is Unavailable";
        publish();
        return;
    }
    (void)start(SettingsCommand::wifi_scan,
                {model_.environment().nmcli, "-t", "--escape", "yes", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device",
                 "wifi", "list", "--rescan", "yes"},
                "Scanning...");
}

void WifiViewModel::set_enabled(bool enabled)
{
    if (commands_.busy() && !settings_command_is_background(commands_.active_command())) {
        status_override_ = "Another Operation Is In Progress";
        publish();
        return;
    }
    if (!model_.wifi_control_enabled()) {
        status_override_ = "Cannot Change Wi-Fi";
        publish();
        return;
    }

    if (model_.nmcli_available()) {
        model_.begin_wifi_power_change(enabled);
        if (!start(SettingsCommand::wifi_power, {model_.environment().nmcli, "radio", "wifi", enabled ? "on" : "off"},
                   enabled ? "Turning On..." : "Turning Off...")) {
            model_.complete_wifi_power_change(false);
            model_.refresh_connectivity();
            publish();
        }
        return;
    }

    if (commands_.busy()) commands_.stop();
    const bool rfkill_changed = model_.set_wifi_radio_enabled(enabled);
    model_.refresh_connectivity();
    status_override_ = rfkill_changed ? (enabled ? "On" : "Off") : "Cannot Change Wi-Fi";
    publish();
}

WifiActionResult WifiViewModel::activate_network(std::size_t index)
{
    if (index >= model_.wifi_networks().size() || !model_.wifi_on() || !model_.nmcli_available())
        return WifiActionResult::ignored;
    const auto &network = model_.wifi_networks()[index];
    if (network.connected) {
        const auto connected = [](const NetworkInterface &item) { return item.connected; };
        const auto count = std::count_if(model_.wifi_interfaces().begin(), model_.wifi_interfaces().end(), connected);
        const auto interface =
            std::find_if(model_.wifi_interfaces().begin(), model_.wifi_interfaces().end(), connected);
        if (count != 1 || interface == model_.wifi_interfaces().end()) {
            status_override_ = "Cannot Identify Wi-Fi Interface";
            publish();
            return WifiActionResult::ignored;
        }
        return start(SettingsCommand::wifi_disconnect,
                     {model_.environment().nmcli, "--wait", "10", "device", "disconnect", interface->name},
                     "Disconnecting...")
                   ? WifiActionResult::started
                   : WifiActionResult::ignored;
    }
    if (network.security == "Open") {
        return start(SettingsCommand::wifi_connect,
                     {model_.environment().nmcli, "--wait", "15", "device", "wifi", "connect", network.ssid},
                     "Connecting...")
                   ? WifiActionResult::started
                   : WifiActionResult::ignored;
    }
    pending_ssid_ = network.ssid;
    return WifiActionResult::password_required;
}

bool WifiViewModel::connect_pending_network(const char *password)
{
    if (pending_ssid_.empty() || !password || !password[0]) return false;
    const std::string ssid = pending_ssid_;
    std::string standard_input(password);
    standard_input.push_back('\n');
    const bool started = start(SettingsCommand::wifi_connect,
                               {model_.environment().nmcli, "--ask", "--wait", "15", "device", "wifi", "connect", ssid},
                               "Connecting...", standard_input);
    std::fill(standard_input.begin(), standard_input.end(), '\0');
    if (started) pending_ssid_.clear();
    return started;
}

void WifiViewModel::cancel_pending_network() noexcept
{
    pending_ssid_.clear();
}

void WifiViewModel::command_finished(const SettingsCommandResult &result)
{
    switch (result.command) {
        case SettingsCommand::wifi_scan:
            model_.refresh_connectivity();
            if (!result.success)
                status_override_ = "Wi-Fi Scan Failed";
            else if (!has_output(result.output)) {
                model_.clear_wifi_networks();
                status_override_ = "No Networks Found";
            } else if (model_.update_wifi_networks(result.output))
                status_override_ = "Networks Updated";
            else
                status_override_ = "Wi-Fi Scan Failed";
            publish();
            break;
        case SettingsCommand::wifi_radio_sync: {
            model_.refresh_connectivity();
            if (!result.success || !model_.update_network_manager_wifi_radio(result.output))
                model_.mark_network_manager_wifi_radio_refresh_failed();
            status_override_.clear();
            publish();
            break;
        }
        case SettingsCommand::wifi_sync:
            status_override_.clear();
            model_.refresh_connectivity();
            if (result.success) {
                if (has_output(result.output))
                    (void)model_.update_wifi_networks(result.output);
                else
                    model_.clear_wifi_networks();
            }
            publish();
            break;
        case SettingsCommand::wifi_connect:
            status_override_ = result.success ? "Connected" : "Connection Failed";
            model_.refresh_connectivity();
            publish();
            break;
        case SettingsCommand::wifi_disconnect:
            status_override_ = result.success ? "Disconnected" : "Disconnect Failed";
            model_.refresh_connectivity();
            if (result.success) model_.clear_wifi_connection();
            publish();
            break;
        case SettingsCommand::wifi_power:
            model_.complete_wifi_power_change(result.success);
            model_.refresh_connectivity();
            status_override_ = result.success ? "Wi-Fi Updated" : "Cannot Change Wi-Fi";
            publish();
            break;
        default:
            break;
    }
}

const WifiPresentation &WifiViewModel::presentation() const noexcept
{
    return presentation_;
}

const std::vector<WifiNetwork> &WifiViewModel::networks() const noexcept
{
    return model_.wifi_networks();
}

const std::string &WifiViewModel::pending_ssid() const noexcept
{
    return pending_ssid_;
}

lv_subject_t *WifiViewModel::revision_subject() noexcept
{
    return revision_.get();
}

bool WifiViewModel::start(SettingsCommand command, std::vector<std::string> arguments, const char *progress,
                          std::string_view standard_input)
{
    if (commands_.busy() && settings_command_is_background(commands_.active_command())) commands_.stop();
    if (commands_.busy()) {
        status_override_ = "Another Operation Is In Progress";
        publish();
        return false;
    }
    if (!commands_.start(command, arguments, standard_input)) {
        status_override_ = "Cannot Start Operation";
        publish();
        return false;
    }
    status_override_ = progress;
    publish();
    return true;
}

void WifiViewModel::update_list_generation() noexcept
{
    const bool available               = model_.wifi_available();
    const bool enabled                 = model_.wifi_on();
    const std::size_t model_generation = model_.wifi_network_generation();
    if (!observed_list_state_valid_ || observed_available_ != available || observed_enabled_ != enabled ||
        observed_model_generation_ != model_generation)
        ++list_generation_;
    observed_list_state_valid_ = true;
    observed_available_        = available;
    observed_enabled_          = enabled;
    observed_model_generation_ = model_generation;
}

std::string WifiViewModel::derived_status() const
{
    if (!model_.wifi_available()) return "Wi-Fi Is Unavailable";
    if (model_.wifi_radio().hardware_blocked) return "Wi-Fi Is Hardware Blocked";
    if (!model_.wifi_on()) return "Wi-Fi Is Off";
    if (const auto *connected = model_.connected_wifi()) return connected->ssid;
    return "Not Connected";
}

}  // namespace lilygo::settings
