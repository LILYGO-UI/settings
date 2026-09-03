#ifndef LILYGO_UI_SETTINGS_DOMAIN_SETTINGS_MODEL_HPP
#define LILYGO_UI_SETTINGS_DOMAIN_SETTINGS_MODEL_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lilygo::settings {

struct SettingsEnvironment {
    std::string network_root;
    std::string rfkill_root;
    std::string power_supply_root;
    std::string os_release;
    std::string model_file;
    std::string serial_file;
    std::string meminfo_file;
    std::string storage_path;
    std::string nmcli;
    std::string bluetoothctl;

    static SettingsEnvironment from_process_environment();
};

struct RadioState {
    bool available        = false;
    bool enabled          = false;
    bool hardware_blocked = false;
};

struct NetworkInterface {
    std::string name;
    std::string state;
    std::string address;
    std::string ipv4;
    std::string duplex;
    int speed_mbps = 0;
    int mtu        = 0;
    bool connected = false;
};

struct WifiNetwork {
    std::string ssid;
    std::string security;
    int signal     = 0;
    bool connected = false;
};

struct BluetoothDevice {
    std::string address;
    std::string name;
};

struct BatteryState {
    bool available = false;
    std::string status;
    std::string health;
    std::string technology;
    int capacity_percent                = 0;
    int cycle_count                     = 0;
    std::int64_t voltage_uv             = 0;
    std::int64_t current_ua             = 0;
    std::int64_t temperature_tenths_c   = 0;
    std::int64_t charge_full_uah        = 0;
    std::int64_t charge_full_design_uah = 0;
    bool has_capacity                   = false;
    bool has_cycle_count                = false;
    bool has_voltage                    = false;
    bool has_current                    = false;
    bool has_temperature                = false;
    bool has_charge_full                = false;
    bool has_charge_full_design         = false;
};

struct AboutState {
    std::string device_name;
    std::string model;
    std::string serial;
    std::string os_name;
    std::string kernel;
    std::string architecture;
    std::int64_t memory_bytes            = 0;
    std::int64_t storage_total_bytes     = 0;
    std::int64_t storage_available_bytes = 0;
};

class SettingsModel {
public:
    explicit SettingsModel(SettingsEnvironment environment = SettingsEnvironment::from_process_environment());

    void refresh_all();
    void refresh_connectivity();
    void refresh_battery();
    void refresh_about();

    [[nodiscard]] bool set_wifi_radio_enabled(bool enabled) const;
    [[nodiscard]] bool set_bluetooth_radio_enabled(bool enabled) const;
    void begin_wifi_power_change(bool enabled) noexcept;
    void complete_wifi_power_change(bool success) noexcept;
    void begin_bluetooth_power_change(bool enabled) noexcept;
    void complete_bluetooth_power_change(bool success) noexcept;

    [[nodiscard]] bool update_network_manager_wifi_radio(std::string_view output);
    void mark_network_manager_wifi_radio_refresh_failed() noexcept;
    [[nodiscard]] bool update_wifi_networks(std::string_view output);
    [[nodiscard]] bool update_bluetooth_devices(std::string_view output);
    [[nodiscard]] bool update_bluetooth_controller(std::string_view output);
    void mark_bluetooth_controller_unavailable() noexcept;
    void mark_bluetooth_service_unavailable() noexcept;
    void mark_bluetooth_service_refresh_failed() noexcept;
    void clear_wifi_connection() noexcept;
    void clear_wifi_networks() noexcept;
    void clear_bluetooth_devices() noexcept;

    [[nodiscard]] const SettingsEnvironment &environment() const noexcept;
    [[nodiscard]] const RadioState &wifi_radio() const noexcept;
    [[nodiscard]] const RadioState &bluetooth_radio() const noexcept;
    [[nodiscard]] bool wifi_available() const noexcept;
    [[nodiscard]] bool bluetooth_available() const noexcept;
    [[nodiscard]] bool wifi_on() const noexcept;
    [[nodiscard]] bool bluetooth_on() const noexcept;
    [[nodiscard]] bool wifi_control_enabled() const noexcept;
    [[nodiscard]] bool bluetooth_control_enabled() const noexcept;
    [[nodiscard]] bool nmcli_available() const noexcept;
    [[nodiscard]] bool bluetoothctl_available() const noexcept;
    [[nodiscard]] bool bluetooth_uses_service_backend() const noexcept;
    [[nodiscard]] const std::vector<NetworkInterface> &wifi_interfaces() const noexcept;
    [[nodiscard]] const std::vector<NetworkInterface> &ethernet_interfaces() const noexcept;
    [[nodiscard]] const std::vector<WifiNetwork> &wifi_networks() const noexcept;
    [[nodiscard]] const std::vector<BluetoothDevice> &bluetooth_devices() const noexcept;
    [[nodiscard]] std::size_t wifi_network_generation() const noexcept;
    [[nodiscard]] std::size_t bluetooth_device_generation() const noexcept;
    [[nodiscard]] const BatteryState &battery() const noexcept;
    [[nodiscard]] const AboutState &about() const noexcept;
    [[nodiscard]] const WifiNetwork *connected_wifi() const noexcept;

private:
    void recompute_connectivity() noexcept;
    void clear_stale_wifi_connection() noexcept;

    SettingsEnvironment environment_;
    RadioState wifi_radio_;
    RadioState bluetooth_radio_;
    std::vector<NetworkInterface> wifi_interfaces_;
    std::vector<NetworkInterface> ethernet_interfaces_;
    std::vector<WifiNetwork> wifi_networks_;
    std::vector<BluetoothDevice> bluetooth_devices_;
    BatteryState battery_;
    AboutState about_;
    bool wifi_available_                      = false;
    bool bluetooth_available_                 = false;
    bool wifi_on_                             = false;
    bool bluetooth_on_                        = false;
    bool wifi_control_enabled_                = false;
    bool bluetooth_control_enabled_           = false;
    bool nmcli_available_                     = false;
    bool network_manager_wifi_known_          = false;
    bool network_manager_wifi_enabled_        = false;
    bool network_manager_wifi_refresh_failed_ = false;
    bool bluetoothctl_available_              = false;
    bool bluetooth_controller_known_          = false;
    bool bluetooth_controller_available_      = false;
    bool bluetooth_controller_powered_        = false;
    bool bluetooth_service_checked_           = false;
    bool bluetooth_service_available_         = false;
    bool bluetooth_service_reachable_         = false;
    bool wifi_override_valid_                 = false;
    bool wifi_override_                       = false;
    bool wifi_previous_power_                 = false;
    bool wifi_requested_power_                = false;
    bool bluetooth_override_valid_            = false;
    bool bluetooth_override_                  = false;
    bool bluetooth_previous_power_            = false;
    bool bluetooth_requested_power_           = false;
    std::size_t wifi_network_generation_      = 0;
    std::size_t bluetooth_device_generation_  = 0;
};

}  // namespace lilygo::settings

#endif
