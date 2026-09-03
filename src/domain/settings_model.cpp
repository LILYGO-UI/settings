#include "domain/settings_model.hpp"

#include "domain/settings_platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace lilygo::settings {
namespace {

std::string environment_value(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value && value[0] ? value : fallback;
}

RadioState copy_radio(const platform::settings_radio_t &source)
{
    return {source.available, source.enabled, source.hardware_blocked};
}

std::vector<NetworkInterface> copy_interfaces(const platform::settings_interface_list_t &source)
{
    std::vector<NetworkInterface> result;
    result.reserve(source.count);
    for (std::size_t index = 0; index < source.count; ++index) {
        const auto &item = source.items[index];
        result.push_back(
            {item.name, item.state, item.address, item.ipv4, item.duplex, item.speed_mbps, item.mtu, item.connected});
    }
    return result;
}

bool same_wifi_networks(const std::vector<WifiNetwork> &left, const std::vector<WifiNetwork> &right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto &a = left[index];
        const auto &b = right[index];
        if (a.ssid != b.ssid || a.security != b.security || a.signal != b.signal || a.connected != b.connected)
            return false;
    }
    return true;
}

bool same_bluetooth_devices(const std::vector<BluetoothDevice> &left, const std::vector<BluetoothDevice> &right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].address != right[index].address || left[index].name != right[index].name) return false;
    }
    return true;
}

std::string normalized_value(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    std::string result(value.substr(first, last - first + 1U));
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

}  // namespace

SettingsEnvironment SettingsEnvironment::from_process_environment()
{
    return {
        environment_value("CM0_NETWORK_DIR", "/sys/class/net"),
        environment_value("CM0_RFKILL_DIR", "/sys/class/rfkill"),
        environment_value("CM0_POWER_SUPPLY_DIR", "/sys/class/power_supply"),
        environment_value("CM0_OS_RELEASE_FILE", "/etc/os-release"),
        environment_value("CM0_DEVICE_MODEL_FILE", "/proc/device-tree/model"),
        environment_value("CM0_DEVICE_SERIAL_FILE", "/proc/device-tree/serial-number"),
        environment_value("CM0_MEMINFO_FILE", "/proc/meminfo"),
        environment_value("CM0_STORAGE_PATH", "/"),
        environment_value("CM0_NMCLI", "nmcli"),
        environment_value("CM0_BLUETOOTHCTL", "bluetoothctl"),
    };
}

SettingsModel::SettingsModel(SettingsEnvironment environment) : environment_(std::move(environment))
{
}

void SettingsModel::refresh_all()
{
    refresh_connectivity();
    refresh_battery();
    refresh_about();
}

void SettingsModel::refresh_connectivity()
{
    platform::settings_radio_t wifi_radio{};
    platform::settings_radio_t bluetooth_radio{};
    platform::settings_interface_list_t wifi_interfaces{};
    platform::settings_interface_list_t ethernet_interfaces{};

    const bool nmcli_available = platform::settings_command_available(environment_.nmcli.c_str());
    if (nmcli_available != nmcli_available_) {
        network_manager_wifi_known_          = false;
        network_manager_wifi_refresh_failed_ = false;
    }
    nmcli_available_                  = nmcli_available;
    const bool bluetoothctl_available = platform::settings_command_available(environment_.bluetoothctl.c_str());
    if (bluetoothctl_available != bluetoothctl_available_) {
        bluetooth_service_checked_   = false;
        bluetooth_service_available_ = false;
        bluetooth_service_reachable_ = false;
        if (!bluetoothctl_available) clear_bluetooth_devices();
    }
    bluetoothctl_available_     = bluetoothctl_available;
    const int wifi_radio_result = platform::settings_radio_read(environment_.rfkill_root.c_str(), "wlan", &wifi_radio);
    platform::settings_radio_read(environment_.rfkill_root.c_str(), "bluetooth", &bluetooth_radio);
    const int wifi_interfaces_result =
        platform::settings_interfaces_read(environment_.network_root.c_str(), true, &wifi_interfaces);
    const int ethernet_result =
        platform::settings_interfaces_read(environment_.network_root.c_str(), false, &ethernet_interfaces);

    wifi_radio_          = copy_radio(wifi_radio);
    bluetooth_radio_     = copy_radio(bluetooth_radio);
    wifi_interfaces_     = wifi_interfaces_result == platform::SETTINGS_BACKEND_OK ? copy_interfaces(wifi_interfaces)
                                                                                   : std::vector<NetworkInterface>{};
    ethernet_interfaces_ = ethernet_result == platform::SETTINGS_BACKEND_OK ? copy_interfaces(ethernet_interfaces)
                                                                            : std::vector<NetworkInterface>{};

    if (wifi_override_valid_ && wifi_radio_result == platform::SETTINGS_BACKEND_OK &&
        wifi_radio_.enabled == wifi_override_)
        wifi_override_valid_ = false;
    recompute_connectivity();
}

void SettingsModel::recompute_connectivity() noexcept
{
    wifi_available_ = wifi_radio_.available || !wifi_interfaces_.empty();
    if (wifi_override_valid_)
        wifi_on_ = wifi_override_;
    else if (nmcli_available_ && network_manager_wifi_known_)
        wifi_on_ = network_manager_wifi_enabled_;
    else if (nmcli_available_ && !network_manager_wifi_refresh_failed_)
        wifi_on_ = false;
    else if (wifi_radio_.available)
        wifi_on_ = wifi_radio_.enabled;
    else
        wifi_on_ = !wifi_interfaces_.empty();
    wifi_on_ =
        wifi_available_ && wifi_on_ && !wifi_radio_.hardware_blocked && (!wifi_radio_.available || wifi_radio_.enabled);
    wifi_control_enabled_ =
        wifi_available_ && !wifi_radio_.hardware_blocked && (nmcli_available_ || wifi_radio_.available);

    if (bluetoothctl_available_ && bluetooth_service_checked_ && bluetooth_service_available_) {
        bluetooth_available_ = bluetooth_controller_known_ && bluetooth_controller_available_;
        bluetooth_on_        = bluetooth_override_valid_ ? bluetooth_override_ : bluetooth_controller_powered_;
        bluetooth_on_        = bluetooth_available_ && bluetooth_on_ && !bluetooth_radio_.hardware_blocked &&
                        (!bluetooth_radio_.available || bluetooth_radio_.enabled);
        bluetooth_control_enabled_ =
            bluetooth_available_ && !bluetooth_radio_.hardware_blocked && bluetooth_service_reachable_;
    } else if (!bluetoothctl_available_ || (bluetooth_service_checked_ && !bluetooth_service_available_)) {
        bluetooth_available_       = bluetooth_radio_.available;
        bluetooth_on_              = bluetooth_override_valid_ ? bluetooth_override_ : bluetooth_radio_.enabled;
        bluetooth_on_              = bluetooth_available_ && bluetooth_on_ && !bluetooth_radio_.hardware_blocked;
        bluetooth_control_enabled_ = bluetooth_available_ && !bluetooth_radio_.hardware_blocked;
    } else {
        bluetooth_available_       = bluetooth_radio_.available;
        bluetooth_on_              = false;
        bluetooth_control_enabled_ = false;
    }

    clear_stale_wifi_connection();
    if (!bluetooth_on_) clear_bluetooth_devices();
}

void SettingsModel::clear_stale_wifi_connection() noexcept
{
    const bool interface_connected = std::any_of(wifi_interfaces_.begin(), wifi_interfaces_.end(),
                                                 [](const NetworkInterface &interface) { return interface.connected; });
    if (wifi_on_ && interface_connected) return;

    bool changed = false;
    for (auto &network : wifi_networks_) {
        changed           = changed || network.connected;
        network.connected = false;
    }
    if (changed) ++wifi_network_generation_;
}

void SettingsModel::refresh_battery()
{
    platform::settings_battery_t source{};
    if (platform::settings_battery_read(environment_.power_supply_root.c_str(), &source) !=
        platform::SETTINGS_BACKEND_OK) {
        battery_ = {};
        return;
    }
    battery_ = {
        true,
        source.status,
        source.health,
        source.technology,
        source.capacity_percent,
        source.cycle_count,
        source.voltage_uv,
        source.current_ua,
        source.temperature_tenths_c,
        source.charge_full_uah,
        source.charge_full_design_uah,
        source.has_capacity,
        source.has_cycle_count,
        source.has_voltage,
        source.has_current,
        source.has_temperature,
        source.has_charge_full,
        source.has_charge_full_design,
    };
}

void SettingsModel::refresh_about()
{
    platform::settings_about_t source{};
    platform::settings_about_read(environment_.os_release.c_str(), environment_.model_file.c_str(),
                                  environment_.serial_file.c_str(), environment_.meminfo_file.c_str(),
                                  environment_.storage_path.c_str(), &source);
    about_ = {source.device_name,
              source.model,
              source.serial,
              source.os_name,
              source.kernel,
              source.architecture,
              source.memory_bytes,
              source.storage_total_bytes,
              source.storage_available_bytes};
}

bool SettingsModel::set_wifi_radio_enabled(bool enabled) const
{
    return platform::settings_radio_set_enabled(environment_.rfkill_root.c_str(), "wlan", enabled) ==
           platform::SETTINGS_BACKEND_OK;
}

bool SettingsModel::set_bluetooth_radio_enabled(bool enabled) const
{
    return platform::settings_radio_set_enabled(environment_.rfkill_root.c_str(), "bluetooth", enabled) ==
           platform::SETTINGS_BACKEND_OK;
}

void SettingsModel::begin_wifi_power_change(bool enabled) noexcept
{
    wifi_previous_power_  = wifi_on_;
    wifi_requested_power_ = enabled;
    wifi_override_valid_  = true;
    wifi_override_        = enabled;
    recompute_connectivity();
}

void SettingsModel::complete_wifi_power_change(bool success) noexcept
{
    if (success) {
        network_manager_wifi_known_          = true;
        network_manager_wifi_enabled_        = wifi_requested_power_;
        network_manager_wifi_refresh_failed_ = false;
    }
    wifi_override_       = success ? wifi_requested_power_ : wifi_previous_power_;
    wifi_override_valid_ = false;
    recompute_connectivity();
}

void SettingsModel::begin_bluetooth_power_change(bool enabled) noexcept
{
    bluetooth_previous_power_  = bluetooth_on_;
    bluetooth_requested_power_ = enabled;
    bluetooth_override_valid_  = true;
    bluetooth_override_        = enabled;
    recompute_connectivity();
}

void SettingsModel::complete_bluetooth_power_change(bool success) noexcept
{
    if (success && bluetooth_controller_available_) bluetooth_controller_powered_ = bluetooth_requested_power_;
    bluetooth_override_       = success ? bluetooth_requested_power_ : bluetooth_previous_power_;
    bluetooth_override_valid_ = false;
    recompute_connectivity();
}

bool SettingsModel::update_network_manager_wifi_radio(std::string_view output)
{
    const std::string value = normalized_value(output);
    if (value != "enabled" && value != "disabled") return false;
    network_manager_wifi_known_          = true;
    network_manager_wifi_enabled_        = value == "enabled";
    network_manager_wifi_refresh_failed_ = false;
    wifi_override_valid_                 = false;
    recompute_connectivity();
    return true;
}

void SettingsModel::mark_network_manager_wifi_radio_refresh_failed() noexcept
{
    if (!network_manager_wifi_known_) network_manager_wifi_refresh_failed_ = true;
    recompute_connectivity();
}

bool SettingsModel::update_wifi_networks(std::string_view output)
{
    platform::settings_wifi_network_list_t parsed{};
    const std::string text(output);
    if (platform::settings_wifi_parse_nmcli(text.c_str(), &parsed) != platform::SETTINGS_BACKEND_OK) return false;
    std::vector<WifiNetwork> networks;
    networks.reserve(parsed.count);
    for (std::size_t index = 0; index < parsed.count; ++index) {
        const auto &network = parsed.items[index];
        networks.push_back({network.ssid, network.security, network.signal, network.connected});
    }
    if (!same_wifi_networks(wifi_networks_, networks)) {
        wifi_networks_ = std::move(networks);
        ++wifi_network_generation_;
    }
    clear_stale_wifi_connection();
    return true;
}

bool SettingsModel::update_bluetooth_devices(std::string_view output)
{
    platform::settings_bluetooth_device_list_t parsed{};
    const std::string text(output);
    if (platform::settings_bluetooth_parse_devices(text.c_str(), &parsed) != platform::SETTINGS_BACKEND_OK)
        return false;
    std::vector<BluetoothDevice> devices;
    devices.reserve(parsed.count);
    for (std::size_t index = 0; index < parsed.count; ++index) {
        devices.push_back({parsed.items[index].address, parsed.items[index].name});
    }
    if (!same_bluetooth_devices(bluetooth_devices_, devices)) {
        bluetooth_devices_ = std::move(devices);
        ++bluetooth_device_generation_;
    }
    return true;
}

bool SettingsModel::update_bluetooth_controller(std::string_view output)
{
    platform::settings_bluetooth_controller_t controller{};
    const std::string text(output);
    if (platform::settings_bluetooth_parse_show(text.c_str(), &controller) != platform::SETTINGS_BACKEND_OK)
        return false;
    bluetooth_service_checked_      = true;
    bluetooth_service_available_    = true;
    bluetooth_service_reachable_    = true;
    bluetooth_controller_known_     = true;
    bluetooth_controller_available_ = controller.available;
    bluetooth_controller_powered_   = controller.powered;
    bluetooth_override_valid_       = false;
    recompute_connectivity();
    return true;
}

void SettingsModel::mark_bluetooth_controller_unavailable() noexcept
{
    bluetooth_service_checked_      = true;
    bluetooth_service_available_    = true;
    bluetooth_service_reachable_    = true;
    bluetooth_controller_known_     = true;
    bluetooth_controller_available_ = false;
    bluetooth_controller_powered_   = false;
    bluetooth_override_valid_       = false;
    recompute_connectivity();
}

void SettingsModel::mark_bluetooth_service_unavailable() noexcept
{
    bluetooth_service_checked_      = true;
    bluetooth_service_available_    = false;
    bluetooth_service_reachable_    = false;
    bluetooth_controller_known_     = false;
    bluetooth_controller_available_ = false;
    bluetooth_controller_powered_   = false;
    bluetooth_override_valid_       = false;
    clear_bluetooth_devices();
    recompute_connectivity();
}

void SettingsModel::mark_bluetooth_service_refresh_failed() noexcept
{
    bluetooth_service_reachable_ = false;
    recompute_connectivity();
}

void SettingsModel::clear_wifi_connection() noexcept
{
    bool changed = false;
    for (auto &network : wifi_networks_) {
        changed           = changed || network.connected;
        network.connected = false;
    }
    if (changed) ++wifi_network_generation_;
}

void SettingsModel::clear_wifi_networks() noexcept
{
    if (wifi_networks_.empty()) return;
    wifi_networks_.clear();
    ++wifi_network_generation_;
}
void SettingsModel::clear_bluetooth_devices() noexcept
{
    if (bluetooth_devices_.empty()) return;
    bluetooth_devices_.clear();
    ++bluetooth_device_generation_;
}

const SettingsEnvironment &SettingsModel::environment() const noexcept
{
    return environment_;
}
const RadioState &SettingsModel::wifi_radio() const noexcept
{
    return wifi_radio_;
}
const RadioState &SettingsModel::bluetooth_radio() const noexcept
{
    return bluetooth_radio_;
}
bool SettingsModel::wifi_available() const noexcept
{
    return wifi_available_;
}
bool SettingsModel::bluetooth_available() const noexcept
{
    return bluetooth_available_;
}
bool SettingsModel::wifi_on() const noexcept
{
    return wifi_on_;
}
bool SettingsModel::bluetooth_on() const noexcept
{
    return bluetooth_on_;
}
bool SettingsModel::wifi_control_enabled() const noexcept
{
    return wifi_control_enabled_;
}
bool SettingsModel::bluetooth_control_enabled() const noexcept
{
    return bluetooth_control_enabled_;
}
bool SettingsModel::nmcli_available() const noexcept
{
    return nmcli_available_;
}
bool SettingsModel::bluetoothctl_available() const noexcept
{
    return bluetoothctl_available_;
}
bool SettingsModel::bluetooth_uses_service_backend() const noexcept
{
    return bluetoothctl_available_ && bluetooth_service_checked_ && bluetooth_service_available_ &&
           bluetooth_service_reachable_ && bluetooth_controller_available_;
}
const std::vector<NetworkInterface> &SettingsModel::wifi_interfaces() const noexcept
{
    return wifi_interfaces_;
}
const std::vector<NetworkInterface> &SettingsModel::ethernet_interfaces() const noexcept
{
    return ethernet_interfaces_;
}
const std::vector<WifiNetwork> &SettingsModel::wifi_networks() const noexcept
{
    return wifi_networks_;
}
const std::vector<BluetoothDevice> &SettingsModel::bluetooth_devices() const noexcept
{
    return bluetooth_devices_;
}
std::size_t SettingsModel::wifi_network_generation() const noexcept
{
    return wifi_network_generation_;
}
std::size_t SettingsModel::bluetooth_device_generation() const noexcept
{
    return bluetooth_device_generation_;
}
const BatteryState &SettingsModel::battery() const noexcept
{
    return battery_;
}
const AboutState &SettingsModel::about() const noexcept
{
    return about_;
}

const WifiNetwork *SettingsModel::connected_wifi() const noexcept
{
    const auto item = std::find_if(wifi_networks_.begin(), wifi_networks_.end(),
                                   [](const WifiNetwork &network) { return network.connected; });
    return item == wifi_networks_.end() ? nullptr : &*item;
}

}  // namespace lilygo::settings
