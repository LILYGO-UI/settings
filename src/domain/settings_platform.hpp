#ifndef LILYGO_UI_SETTINGS_DOMAIN_SETTINGS_PLATFORM_HPP
#define LILYGO_UI_SETTINGS_DOMAIN_SETTINGS_PLATFORM_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace lilygo::settings::platform {

#define SETTINGS_NAME_MAX             64
#define SETTINGS_VALUE_MAX            128
#define SETTINGS_INTERFACE_MAX        8
#define SETTINGS_WIFI_NETWORK_MAX     12
#define SETTINGS_BLUETOOTH_DEVICE_MAX 12

enum {
    SETTINGS_BACKEND_OK        = 0,
    SETTINGS_BACKEND_NOT_FOUND = 1,
    SETTINGS_BACKEND_ERROR     = -1,
};

typedef struct settings_radio {
    char name[SETTINGS_NAME_MAX];
    bool available;
    bool enabled;
    bool hardware_blocked;
} settings_radio_t;

typedef struct settings_interface {
    char name[SETTINGS_NAME_MAX];
    char state[SETTINGS_VALUE_MAX];
    char address[SETTINGS_VALUE_MAX];
    char ipv4[SETTINGS_VALUE_MAX];
    char duplex[SETTINGS_VALUE_MAX];
    int speed_mbps;
    int mtu;
    bool wireless;
    bool connected;
} settings_interface_t;

typedef struct settings_interface_list {
    settings_interface_t items[SETTINGS_INTERFACE_MAX];
    size_t count;
} settings_interface_list_t;

typedef struct settings_wifi_network {
    char ssid[SETTINGS_VALUE_MAX];
    char security[SETTINGS_VALUE_MAX];
    int signal;
    bool connected;
} settings_wifi_network_t;

typedef struct settings_wifi_network_list {
    settings_wifi_network_t items[SETTINGS_WIFI_NETWORK_MAX];
    size_t count;
} settings_wifi_network_list_t;

typedef struct settings_bluetooth_device {
    char address[SETTINGS_NAME_MAX];
    char name[SETTINGS_VALUE_MAX];
} settings_bluetooth_device_t;

typedef struct settings_bluetooth_device_list {
    settings_bluetooth_device_t items[SETTINGS_BLUETOOTH_DEVICE_MAX];
    size_t count;
} settings_bluetooth_device_list_t;

typedef struct settings_bluetooth_controller {
    char address[SETTINGS_NAME_MAX];
    bool available;
    bool powered;
} settings_bluetooth_controller_t;

typedef struct settings_battery {
    char name[SETTINGS_NAME_MAX];
    char status[SETTINGS_VALUE_MAX];
    char health[SETTINGS_VALUE_MAX];
    char technology[SETTINGS_VALUE_MAX];
    int capacity_percent;
    int cycle_count;
    int64_t voltage_uv;
    int64_t current_ua;
    int64_t temperature_tenths_c;
    int64_t charge_full_uah;
    int64_t charge_full_design_uah;
    bool has_capacity;
    bool has_cycle_count;
    bool has_voltage;
    bool has_current;
    bool has_temperature;
    bool has_charge_full;
    bool has_charge_full_design;
} settings_battery_t;

typedef struct settings_about {
    char device_name[SETTINGS_VALUE_MAX];
    char model[SETTINGS_VALUE_MAX];
    char serial[SETTINGS_VALUE_MAX];
    char os_name[SETTINGS_VALUE_MAX];
    char kernel[SETTINGS_VALUE_MAX];
    char architecture[SETTINGS_VALUE_MAX];
    int64_t memory_bytes;
    int64_t storage_total_bytes;
    int64_t storage_available_bytes;
} settings_about_t;

int settings_radio_read(const char *rfkill_root, const char *type, settings_radio_t *radio);
int settings_radio_set_enabled(const char *rfkill_root, const char *type, bool enabled);
int settings_interfaces_read(const char *network_root, bool wireless, settings_interface_list_t *interfaces);
int settings_wifi_parse_nmcli(const char *output, settings_wifi_network_list_t *networks);
int settings_bluetooth_parse_devices(const char *output, settings_bluetooth_device_list_t *devices);
int settings_bluetooth_parse_show(const char *output, settings_bluetooth_controller_t *controller);
int settings_battery_read(const char *power_supply_root, settings_battery_t *battery);
int settings_about_read(const char *os_release_path, const char *model_path, const char *serial_path,
                        const char *meminfo_path, const char *storage_path, settings_about_t *about);
bool settings_command_available(const char *name);
void settings_format_bytes(int64_t bytes, char *text, size_t text_size);

}  // namespace lilygo::settings::platform

#endif
