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
    wifi_model_.update_reachability(model_.wifi_networks(), model_.wifi_on());
    if (presentation_.details_open && (!details_ssid_.empty() || presentation_.details_available) &&
        !details_connection_valid())
        invalidate_details("Network Connection Changed");
    if (!model_.nmcli_available()) presentation_.known_status = "NetworkManager Is Unavailable";
    presentation_.known_generation = wifi_model_.generation();
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
    request_profiles_refresh();
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
    for (std::size_t i = 0; i < known_networks().size(); ++i) {
        const auto &known = known_networks()[i];
        if (known.ssid == network.ssid && ((known.security == "Open") == (network.security == "Open")))
            return activate_known_network(i);
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
        case SettingsCommand::wifi_profiles: {
            profiles_refresh_pending_ = false;
            const auto parsed         = result.success ? WifiModel::parse_profiles(result.output) : std::nullopt;
            if (!parsed) {
                presentation_.known_status = "Cannot Read Known Networks";
                publish();
                break;
            }
            profiles_ = *parsed;
            pending_known_.clear();
            profile_index_            = 0;
            profiles_partial_failure_ = false;
            profile_query_pending_    = !profiles_.empty();
            if (profiles_.empty()) {
                wifi_model_.replace_known({});
                presentation_.known_status = "No Known Networks";
            } else {
                presentation_.known_status = "Reading Known Networks...";
                (void)resume_background();
            }
            publish();
            break;
        }
        case SettingsCommand::wifi_profile: {
            if (!profile_query_pending_ || profile_index_ >= profiles_.size()) break;
            const auto &reference = profiles_[profile_index_];
            auto parsed = result.success ? WifiModel::parse_profile(result.output, reference.uuid) : std::nullopt;
            if (parsed) {
                parsed->connected = reference.active &&
                                    std::any_of(model_.wifi_interfaces().begin(), model_.wifi_interfaces().end(),
                                                [&](const auto &interface) {
                                                    return interface.connected && interface.name == reference.interface;
                                                });
                parsed->in_range =
                    model_.wifi_on() &&
                    std::any_of(model_.wifi_networks().begin(), model_.wifi_networks().end(), [&](const auto &network) {
                        return network.ssid == parsed->ssid &&
                               ((network.security == "Open") == (parsed->security == "Open"));
                    });
                pending_known_.push_back(*parsed);
            } else
                profiles_partial_failure_ = true;
            ++profile_index_;
            profile_query_pending_ = profile_index_ < profiles_.size();
            if (profile_query_pending_)
                (void)resume_background();
            else {
                wifi_model_.replace_known(std::move(pending_known_));
                presentation_.known_status = profiles_partial_failure_  ? "Some Known Networks Are Unavailable"
                                             : known_networks().empty() ? "No Known Networks"
                                                                        : "Saved on This Device";
            }
            publish();
            break;
        }
        case SettingsCommand::wifi_details: {
            if (!presentation_.details_open || !details_query_pending_) break;
            details_query_pending_ = false;
            model_.refresh_connectivity();
            auto parsed = result.success ? WifiModel::parse_device(result.output, details_interface_) : std::nullopt;
            if (!details_connection_valid() || !parsed)
                invalidate_details("Cannot Read Connection Details");
            else if (details_verifying_) {
                if (parsed->profile_uuid != pending_details_.profile_uuid)
                    invalidate_details("Network Connection Changed");
                else {
                    parsed->ssid                    = pending_details_.ssid;
                    parsed->ipv4_method             = pending_details_.ipv4_method;
                    parsed->ipv6_method             = pending_details_.ipv6_method;
                    details_                        = std::move(*parsed);
                    presentation_.details_available = true;
                    presentation_.details_loading   = false;
                    presentation_.details_status    = "Connected";
                    details_verifying_              = false;
                }
            } else {
                pending_details_         = *parsed;
                details_profile_pending_ = true;
                (void)resume_background();
            }
            publish();
            break;
        }
        case SettingsCommand::wifi_details_profile: {
            if (!presentation_.details_open || !details_profile_pending_) break;
            details_profile_pending_ = false;
            model_.refresh_connectivity();
            if (!result.success || !details_connection_valid() ||
                !WifiModel::apply_details_profile(pending_details_, result.output) ||
                pending_details_.ssid != details_ssid_) {
                invalidate_details("Network Connection Changed or Details Unavailable");
            } else {
                details_verifying_     = true;
                details_query_pending_ = true;
                (void)resume_background();
            }
            publish();
            break;
        }
        case SettingsCommand::wifi_forget:
            if (result.success) {
                wifi_model_.remove_known(forget_uuid_);
                if (details_.profile_uuid == forget_uuid_) {
                    model_.clear_wifi_connection();
                    presentation_.details_open = false;
                    invalidate_details("");
                }
                status_override_ = "Network Forgotten";
                request_profiles_refresh();
            } else {
                status_override_ = "Cannot Forget Network";
                if (presentation_.details_open) presentation_.details_status = status_override_;
            }
            forget_uuid_.clear();
            publish();
            break;
        case SettingsCommand::wifi_scan:
            request_profiles_refresh();
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
            request_profiles_refresh();
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
            request_profiles_refresh();
            status_override_ = result.success ? "Connected" : "Connection Failed";
            model_.refresh_connectivity();
            publish();
            break;
        case SettingsCommand::wifi_disconnect:
            request_profiles_refresh();
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

void WifiViewModel::request_profiles_refresh() noexcept
{
    profiles_refresh_pending_ = true;
    profile_query_pending_    = false;
    pending_known_.clear();
    profiles_.clear();
    profile_index_ = 0;
    if (presentation_.details_open) {
        details_query_pending_   = true;
        details_profile_pending_ = false;
        details_verifying_       = false;
    }
}

void WifiViewModel::open_details()
{
    presentation_.details_open = true;
    details_                   = {};
    pending_details_           = {};
    details_ssid_.clear();
    details_interface_.clear();
    const auto *connected = model_.connected_wifi();
    if (!model_.nmcli_available() || !model_.wifi_on() || !connected) {
        invalidate_details("Connection Details Are Unavailable");
        publish();
        return;
    }
    for (const auto &interface : model_.wifi_interfaces()) {
        if (!interface.connected) continue;
        if (!details_interface_.empty()) {
            invalidate_details("Cannot Identify Wi-Fi Interface");
            publish();
            return;
        }
        details_interface_ = interface.name;
    }
    if (details_interface_.empty()) {
        invalidate_details("Connection Details Are Unavailable");
        publish();
        return;
    }
    details_ssid_                   = connected->ssid;
    details_.ssid                   = details_ssid_;
    details_query_pending_          = true;
    details_profile_pending_        = false;
    presentation_.details_loading   = true;
    presentation_.details_available = false;
    presentation_.details_status    = "Reading Network Details...";
    (void)resume_background();
    publish();
}

void WifiViewModel::close_details()
{
    if (commands_.active_command() == SettingsCommand::wifi_details ||
        commands_.active_command() == SettingsCommand::wifi_details_profile)
        commands_.stop();
    presentation_.details_open = false;
    invalidate_details("");
    publish();
}

void WifiViewModel::stop() noexcept
{
    profiles_refresh_pending_ = profile_query_pending_ = false;
    details_query_pending_ = details_profile_pending_ = details_verifying_ = false;
    presentation_.details_loading                                          = false;
    pending_known_.clear();
    profiles_.clear();
}

bool WifiViewModel::details_connection_valid() const
{
    const auto *connected = model_.connected_wifi();
    if (!model_.wifi_on() || !model_.nmcli_available() || !connected || connected->ssid != details_ssid_) return false;
    std::size_t count = 0;
    for (const auto &interface : model_.wifi_interfaces()) {
        if (interface.connected) {
            if (interface.name != details_interface_) return false;
            ++count;
        }
    }
    return count == 1;
}

void WifiViewModel::invalidate_details(const char *status)
{
    details_                        = {};
    pending_details_                = {};
    presentation_.details_loading   = false;
    presentation_.details_available = false;
    presentation_.details_status    = status;
    details_query_pending_          = false;
    details_profile_pending_        = false;
    details_verifying_              = false;
}

WifiActionResult WifiViewModel::activate_known_network(std::size_t index)
{
    if (index >= known_networks().size() || !model_.wifi_on() || !model_.nmcli_available())
        return WifiActionResult::ignored;
    const auto network = known_networks()[index];
    if (network.connected) {
        open_details();
        return WifiActionResult::ignored;
    }
    return start(SettingsCommand::wifi_connect, {model_.environment().nmcli, "connection", "up", "uuid", network.uuid},
                 "Connecting...")
               ? WifiActionResult::started
               : WifiActionResult::ignored;
}

bool WifiViewModel::forget_network(std::string_view uuid)
{
    if (!model_.nmcli_available() || !WifiModel::valid_uuid(uuid)) return false;
    if (std::none_of(known_networks().begin(), known_networks().end(),
                     [&](const auto &entry) { return entry.uuid == uuid; }) &&
        (!presentation_.details_available || details_.profile_uuid != uuid))
        return false;
    const std::string requested_uuid(uuid);
    if (!start(SettingsCommand::wifi_forget,
               {model_.environment().nmcli, "connection", "delete", "uuid", requested_uuid}, "Forgetting Network..."))
        return false;
    forget_uuid_ = requested_uuid;
    if (presentation_.details_open) presentation_.details_status = "Forgetting Network...";
    publish();
    return true;
}

const std::vector<WifiKnownNetwork> &WifiViewModel::known_networks() const noexcept
{
    return wifi_model_.known_networks();
}
const WifiConnectionDetails &WifiViewModel::details() const noexcept
{
    return details_;
}

bool WifiViewModel::start_profile_query()
{
    if (profile_index_ >= profiles_.size()) return false;
    return commands_.start(SettingsCommand::wifi_profile,
                           {model_.environment().nmcli, "-t", "--escape", "yes", "-f",
                            "connection.uuid,connection.type,802-11-wireless.ssid,802-11-wireless-security.key-mgmt",
                            "connection", "show", "uuid", profiles_[profile_index_].uuid},
                           {}, background_command_timeout);
}

bool WifiViewModel::resume_background()
{
    if (commands_.busy() || !model_.nmcli_available()) return false;
    if (presentation_.details_open && (details_query_pending_ || details_profile_pending_)) {
        if (!details_connection_valid()) {
            invalidate_details("Network Connection Changed");
            publish();
        } else if (details_profile_pending_) {
            return commands_.start(SettingsCommand::wifi_details_profile,
                                   {model_.environment().nmcli, "-t", "--escape", "yes", "-f",
                                    "connection.uuid,connection.type,802-11-wireless.ssid,802-11-wireless-security.key-"
                                    "mgmt,ipv4.method,ipv6.method",
                                    "connection", "show", "uuid", pending_details_.profile_uuid},
                                   {}, background_command_timeout);
        } else {
            return commands_.start(SettingsCommand::wifi_details,
                                   {model_.environment().nmcli, "-t", "--escape", "yes", "-f",
                                    "GENERAL.DEVICE,GENERAL.STATE,GENERAL.CON-UUID,GENERAL.HWADDR,GENERAL.MTU,IP4."
                                    "ADDRESS,IP4.GATEWAY,IP4.DNS,IP4.DOMAIN,IP6.ADDRESS,IP6.GATEWAY,IP6.DNS,IP6.DOMAIN",
                                    "device", "show", details_interface_},
                                   {}, background_command_timeout);
        }
    }
    if (profile_query_pending_) return start_profile_query();
    if (profiles_refresh_pending_) {
        if (!commands_.start(SettingsCommand::wifi_profiles,
                             {model_.environment().nmcli, "-t", "--escape", "yes", "-f", "UUID,TYPE,ACTIVE,DEVICE",
                              "connection", "show"},
                             {}, background_command_timeout))
            return false;
        return true;
    }
    return false;
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
    if ((command == SettingsCommand::wifi_connect || command == SettingsCommand::wifi_forget) &&
        !model_.environment().sudo.empty())
        arguments.insert(arguments.begin(), {model_.environment().sudo, "-n", "--"});
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
