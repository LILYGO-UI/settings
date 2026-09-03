#include "domain/settings_platform.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace lilygo::settings::platform {

static bool make_path(char *path, size_t path_size, const char *root, const char *entry, const char *attribute)
{
    int length = attribute ? snprintf(path, path_size, "%s/%s/%s", root, entry, attribute)
                           : snprintf(path, path_size, "%s/%s", root, entry);
    return length >= 0 && (size_t)length < path_size;
}

static void trim(char *text)
{
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' ' ||
                          text[length - 1] == '\t'))
        text[--length] = '\0';
    size_t first = 0;
    while (text[first] == ' ' || text[first] == '\t') ++first;
    if (first) memmove(text, text + first, strlen(text + first) + 1U);
}

static bool read_text_file(const char *path, char *text, size_t text_size)
{
    if (!path || !text || text_size == 0) return false;
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool success = fgets(text, (int)text_size, file) != NULL;
    fclose(file);
    if (!success) return false;
    text[text_size - 1] = '\0';
    trim(text);
    return text[0] != '\0';
}

static bool read_attribute(const char *root, const char *entry, const char *attribute, char *text, size_t text_size)
{
    char path[PATH_MAX];
    return make_path(path, sizeof(path), root, entry, attribute) && read_text_file(path, text, text_size);
}

static bool parse_number(const char *text, int64_t *value)
{
    errno            = 0;
    char *end        = NULL;
    long long parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    *value = (int64_t)parsed;
    return true;
}

static bool read_number(const char *root, const char *entry, const char *attribute, int64_t *value)
{
    char text[64];
    return read_attribute(root, entry, attribute, text, sizeof(text)) && parse_number(text, value);
}

static bool directory_exists(const char *root, const char *entry, const char *child)
{
    char path[PATH_MAX];
    struct stat status;
    return make_path(path, sizeof(path), root, entry, child) && stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static bool select_entry_by_attribute(const char *root, const char *attribute, const char *value, char *selected,
                                      size_t selected_size)
{
    DIR *directory = opendir(root);
    if (!directory) return false;
    selected[0] = '\0';
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        char actual[SETTINGS_VALUE_MAX];
        if (entry->d_name[0] == '.' || !read_attribute(root, entry->d_name, attribute, actual, sizeof(actual)) ||
            strcmp(actual, value) != 0 || strlen(entry->d_name) >= selected_size)
            continue;
        if (!selected[0] || strcmp(entry->d_name, selected) < 0) snprintf(selected, selected_size, "%s", entry->d_name);
    }
    closedir(directory);
    return selected[0] != '\0';
}

int settings_radio_read(const char *rfkill_root, const char *type, settings_radio_t *radio)
{
    if (!rfkill_root || !type || !radio) return SETTINGS_BACKEND_ERROR;
    memset(radio, 0, sizeof(*radio));
    char selected[SETTINGS_NAME_MAX];
    if (!select_entry_by_attribute(rfkill_root, "type", type, selected, sizeof(selected)))
        return SETTINGS_BACKEND_NOT_FOUND;
    int64_t soft = 0;
    int64_t hard = 0;
    if (!read_number(rfkill_root, selected, "soft", &soft)) return SETTINGS_BACKEND_ERROR;
    read_number(rfkill_root, selected, "hard", &hard);
    snprintf(radio->name, sizeof(radio->name), "%s", selected);
    radio->available        = true;
    radio->hardware_blocked = hard != 0;
    radio->enabled          = soft == 0 && hard == 0;
    return SETTINGS_BACKEND_OK;
}

int settings_radio_set_enabled(const char *rfkill_root, const char *type, bool enabled)
{
    if (!rfkill_root || !type) return SETTINGS_BACKEND_ERROR;
    DIR *directory = opendir(rfkill_root);
    if (!directory) return SETTINGS_BACKEND_NOT_FOUND;
    bool found   = false;
    bool success = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        char actual[SETTINGS_VALUE_MAX];
        if (entry->d_name[0] == '.' || !read_attribute(rfkill_root, entry->d_name, "type", actual, sizeof(actual)) ||
            strcmp(actual, type) != 0)
            continue;
        found = true;
        char path[PATH_MAX];
        if (!make_path(path, sizeof(path), rfkill_root, entry->d_name, "soft")) {
            success = false;
            continue;
        }
        FILE *file = fopen(path, "w");
        if (!file) {
            success = false;
            continue;
        }
        bool wrote = fprintf(file, "%d\n", enabled ? 0 : 1) > 0;
        if (fclose(file) != 0) wrote = false;
        if (!wrote) success = false;
    }
    closedir(directory);
    if (!found) return SETTINGS_BACKEND_NOT_FOUND;
    return success ? SETTINGS_BACKEND_OK : SETTINGS_BACKEND_ERROR;
}

static void read_ipv4_address(const char *name, char *address, size_t address_size)
{
    snprintf(address, address_size, "--");
    struct ifaddrs *addresses = NULL;
    if (getifaddrs(&addresses) != 0) return;
    for (struct ifaddrs *item = addresses; item; item = item->ifa_next) {
        if (!item->ifa_addr || strcmp(item->ifa_name, name) != 0 || item->ifa_addr->sa_family != AF_INET) continue;
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)item->ifa_addr;
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address, address_size)) break;
    }
    freeifaddrs(addresses);
}

static int compare_interfaces(const void *left, const void *right)
{
    const auto *a = static_cast<const settings_interface_t *>(left);
    const auto *b = static_cast<const settings_interface_t *>(right);
    if (a->connected != b->connected) return a->connected ? -1 : 1;
    return strcmp(a->name, b->name);
}

static bool is_ethernet_interface(const char *root, const char *name)
{
    if (strncmp(name, "eth", 3) == 0 || strncmp(name, "en", 2) == 0 || strncmp(name, "usb", 3) == 0) return true;
    int64_t link_type;
    return read_number(root, name, "type", &link_type) && link_type == 1 && directory_exists(root, name, "device");
}

int settings_interfaces_read(const char *network_root, bool wireless, settings_interface_list_t *interfaces)
{
    if (!network_root || !interfaces) return SETTINGS_BACKEND_ERROR;
    memset(interfaces, 0, sizeof(*interfaces));
    DIR *directory = opendir(network_root);
    if (!directory) return SETTINGS_BACKEND_NOT_FOUND;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && interfaces->count < SETTINGS_INTERFACE_MAX) {
        size_t name_length = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || strcmp(entry->d_name, "lo") == 0 || name_length >= SETTINGS_NAME_MAX) continue;
        bool entry_wireless = directory_exists(network_root, entry->d_name, "wireless");
        if (entry_wireless != wireless || (!wireless && !is_ethernet_interface(network_root, entry->d_name))) continue;
        settings_interface_t *item = &interfaces->items[interfaces->count];
        memcpy(item->name, entry->d_name, name_length + 1U);
        item->wireless = entry_wireless;
        snprintf(item->state, sizeof(item->state), "unknown");
        snprintf(item->address, sizeof(item->address), "--");
        snprintf(item->duplex, sizeof(item->duplex), "--");
        read_attribute(network_root, entry->d_name, "operstate", item->state, sizeof(item->state));
        read_attribute(network_root, entry->d_name, "address", item->address, sizeof(item->address));
        int64_t number;
        if (read_number(network_root, entry->d_name, "carrier", &number))
            item->connected = number != 0;
        else
            item->connected = strcmp(item->state, "up") == 0;
        if (read_number(network_root, entry->d_name, "speed", &number) && number > 0 && number <= INT_MAX)
            item->speed_mbps = (int)number;
        if (read_number(network_root, entry->d_name, "mtu", &number) && number > 0 && number <= INT_MAX)
            item->mtu = (int)number;
        read_attribute(network_root, entry->d_name, "duplex", item->duplex, sizeof(item->duplex));
        read_ipv4_address(item->name, item->ipv4, sizeof(item->ipv4));
        ++interfaces->count;
    }
    closedir(directory);
    if (!interfaces->count) return SETTINGS_BACKEND_NOT_FOUND;
    qsort(interfaces->items, interfaces->count, sizeof(interfaces->items[0]), compare_interfaces);
    return SETTINGS_BACKEND_OK;
}

static size_t split_escaped_fields(const char *line, char fields[][SETTINGS_VALUE_MAX], size_t field_count)
{
    size_t field = 0;
    size_t used  = 0;
    bool escaped = false;
    memset(fields, 0, field_count * SETTINGS_VALUE_MAX);
    for (const char *cursor = line; *cursor && *cursor != '\n' && field < field_count; ++cursor) {
        if (!escaped && *cursor == ':') {
            ++field;
            used = 0;
            continue;
        }
        if (!escaped && *cursor == '\\') {
            escaped = true;
            continue;
        }
        if (used + 1U < SETTINGS_VALUE_MAX) fields[field][used++] = *cursor;
        escaped = false;
    }
    return field + 1U;
}

static int compare_wifi(const void *left, const void *right)
{
    const auto *a = static_cast<const settings_wifi_network_t *>(left);
    const auto *b = static_cast<const settings_wifi_network_t *>(right);
    if (a->connected != b->connected) return a->connected ? -1 : 1;
    if (a->signal != b->signal) return b->signal - a->signal;
    return strcmp(a->ssid, b->ssid);
}

int settings_wifi_parse_nmcli(const char *output, settings_wifi_network_list_t *networks)
{
    if (!output || !networks) return SETTINGS_BACKEND_ERROR;
    memset(networks, 0, sizeof(*networks));
    const char *cursor = output;
    while (*cursor) {
        const char *end = strchr(cursor, '\n');
        size_t length   = end ? (size_t)(end - cursor) : strlen(cursor);
        char line[512];
        if (length >= sizeof(line)) length = sizeof(line) - 1U;
        memcpy(line, cursor, length);
        line[length] = '\0';
        char fields[4][SETTINGS_VALUE_MAX];
        if (split_escaped_fields(line, fields, 4) >= 4 && fields[1][0]) {
            int signal = atoi(fields[2]);
            if (signal < 0) signal = 0;
            if (signal > 100) signal = 100;
            size_t duplicate = networks->count;
            for (size_t i = 0; i < networks->count; ++i) {
                if (strcmp(networks->items[i].ssid, fields[1]) == 0) {
                    duplicate = i;
                    break;
                }
            }
            settings_wifi_network_t candidate;
            memset(&candidate, 0, sizeof(candidate));
            snprintf(candidate.ssid, sizeof(candidate.ssid), "%s", fields[1]);
            snprintf(candidate.security, sizeof(candidate.security), "%s",
                     fields[3][0] && strcmp(fields[3], "--") != 0 ? fields[3] : "Open");
            candidate.signal    = signal;
            candidate.connected = strcmp(fields[0], "*") == 0 || strcmp(fields[0], "yes") == 0;
            if (duplicate != networks->count) {
                const settings_wifi_network_t *current = &networks->items[duplicate];
                if ((candidate.connected && !current->connected) ||
                    (candidate.connected == current->connected && candidate.signal > current->signal))
                    networks->items[duplicate] = candidate;
            } else if (networks->count < SETTINGS_WIFI_NETWORK_MAX) {
                networks->items[networks->count++] = candidate;
            } else {
                size_t worst = 0;
                for (size_t index = 1; index < networks->count; ++index) {
                    if (compare_wifi(&networks->items[index], &networks->items[worst]) > 0) worst = index;
                }
                if (compare_wifi(&candidate, &networks->items[worst]) < 0) networks->items[worst] = candidate;
            }
        }
        cursor = end ? end + 1 : cursor + strlen(cursor);
    }
    qsort(networks->items, networks->count, sizeof(networks->items[0]), compare_wifi);
    return networks->count ? SETTINGS_BACKEND_OK : SETTINGS_BACKEND_NOT_FOUND;
}

static bool is_bluetooth_address(const char *address)
{
    if (!address || strlen(address) != 17U) return false;
    for (size_t index = 0; index < 17U; ++index) {
        if ((index + 1U) % 3U == 0U) {
            if (address[index] != ':') return false;
        } else if (!((address[index] >= '0' && address[index] <= '9') ||
                     (address[index] >= 'a' && address[index] <= 'f') ||
                     (address[index] >= 'A' && address[index] <= 'F'))) {
            return false;
        }
    }
    return true;
}

int settings_bluetooth_parse_devices(const char *output, settings_bluetooth_device_list_t *devices)
{
    if (!output || !devices) return SETTINGS_BACKEND_ERROR;
    memset(devices, 0, sizeof(*devices));
    const char *cursor = output;
    while (*cursor && devices->count < SETTINGS_BLUETOOTH_DEVICE_MAX) {
        const char *end = strchr(cursor, '\n');
        size_t length   = end ? (size_t)(end - cursor) : strlen(cursor);
        char line[320];
        if (length >= sizeof(line)) length = sizeof(line) - 1U;
        memcpy(line, cursor, length);
        line[length] = '\0';
        trim(line);
        char *device = NULL;
        if (strncmp(line, "Device ", 7) == 0)
            device = line + 7;
        else if (strncmp(line, "[NEW] Device ", 13) == 0)
            device = line + 13;
        if (device) {
            char *space = strchr(device, ' ');
            if (space && space - device == 17) {
                *space           = '\0';
                const char *name = space + 1;
                bool duplicate   = false;
                for (size_t i = 0; i < devices->count; ++i) duplicate |= strcmp(devices->items[i].address, device) == 0;
                if (is_bluetooth_address(device) && !duplicate && name[0]) {
                    settings_bluetooth_device_t *item = &devices->items[devices->count++];
                    snprintf(item->address, sizeof(item->address), "%s", device);
                    snprintf(item->name, sizeof(item->name), "%s", name);
                }
            }
        }
        cursor = end ? end + 1 : cursor + strlen(cursor);
    }
    return devices->count ? SETTINGS_BACKEND_OK : SETTINGS_BACKEND_NOT_FOUND;
}

int settings_bluetooth_parse_show(const char *output, settings_bluetooth_controller_t *controller)
{
    if (!output || !controller) return SETTINGS_BACKEND_ERROR;
    memset(controller, 0, sizeof(*controller));
    bool powered_found = false;
    const char *cursor = output;
    while (*cursor) {
        const char *end = strchr(cursor, '\n');
        size_t length   = end ? (size_t)(end - cursor) : strlen(cursor);
        char line[320];
        if (length >= sizeof(line)) length = sizeof(line) - 1U;
        memcpy(line, cursor, length);
        line[length] = '\0';
        trim(line);

        if (strncmp(line, "Controller ", 11) == 0) {
            const char *address         = line + 11;
            const char *space           = strchr(address, ' ');
            const size_t address_length = space ? (size_t)(space - address) : strlen(address);
            if (address_length == 17U) {
                memcpy(controller->address, address, address_length);
                controller->address[address_length] = '\0';
                controller->available               = is_bluetooth_address(controller->address);
            }
        } else if (strncmp(line, "Powered:", 8) == 0) {
            char *value = line + 8;
            trim(value);
            if (strcmp(value, "yes") == 0 || strcmp(value, "no") == 0) {
                controller->powered = strcmp(value, "yes") == 0;
                powered_found       = true;
            }
        }
        cursor = end ? end + 1 : cursor + strlen(cursor);
    }
    if (!controller->available) return SETTINGS_BACKEND_NOT_FOUND;
    return powered_found ? SETTINGS_BACKEND_OK : SETTINGS_BACKEND_ERROR;
}

static bool is_present_battery(const char *root, const char *entry)
{
    char type[SETTINGS_VALUE_MAX];
    if (!read_attribute(root, entry, "type", type, sizeof(type)) || strcmp(type, "Battery") != 0) return false;
    int64_t present;
    return !read_number(root, entry, "present", &present) || present != 0;
}

int settings_battery_read(const char *power_supply_root, settings_battery_t *battery)
{
    if (!power_supply_root || !battery) return SETTINGS_BACKEND_ERROR;
    memset(battery, 0, sizeof(*battery));
    battery->capacity_percent = -1;
    snprintf(battery->status, sizeof(battery->status), "Unknown");
    snprintf(battery->health, sizeof(battery->health), "Unknown");
    snprintf(battery->technology, sizeof(battery->technology), "Unknown");
    DIR *directory = opendir(power_supply_root);
    if (!directory) return SETTINGS_BACKEND_NOT_FOUND;
    char selected[SETTINGS_NAME_MAX] = "";
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        size_t name_length = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || name_length >= sizeof(selected) ||
            !is_present_battery(power_supply_root, entry->d_name))
            continue;
        if (!selected[0] || strcmp(entry->d_name, selected) < 0) memcpy(selected, entry->d_name, name_length + 1U);
    }
    closedir(directory);
    if (!selected[0]) return SETTINGS_BACKEND_NOT_FOUND;
    snprintf(battery->name, sizeof(battery->name), "%s", selected);
    read_attribute(power_supply_root, selected, "status", battery->status, sizeof(battery->status));
    read_attribute(power_supply_root, selected, "health", battery->health, sizeof(battery->health));
    read_attribute(power_supply_root, selected, "technology", battery->technology, sizeof(battery->technology));
    int64_t value;
    if (read_number(power_supply_root, selected, "capacity", &value) && value >= 0 && value <= 100) {
        battery->capacity_percent = (int)value;
        battery->has_capacity     = true;
    }
    if (read_number(power_supply_root, selected, "cycle_count", &value) && value >= 0 && value <= INT_MAX) {
        battery->cycle_count     = (int)value;
        battery->has_cycle_count = true;
    }
    battery->has_voltage     = read_number(power_supply_root, selected, "voltage_now", &battery->voltage_uv);
    battery->has_current     = read_number(power_supply_root, selected, "current_now", &battery->current_ua);
    battery->has_temperature = read_number(power_supply_root, selected, "temp", &battery->temperature_tenths_c);
    battery->has_charge_full = read_number(power_supply_root, selected, "charge_full", &battery->charge_full_uah);
    battery->has_charge_full_design =
        read_number(power_supply_root, selected, "charge_full_design", &battery->charge_full_design_uah);
    return SETTINGS_BACKEND_OK;
}

static void read_os_name(const char *path, char *name, size_t name_size)
{
    if (!name || name_size == 0) return;
    snprintf(name, name_size, "Unknown");
    FILE *file = fopen(path, "r");
    if (!file) return;
    char line[320];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "PRETTY_NAME=", 12) != 0) continue;
        char *value = line + 12;
        trim(value);
        size_t length = strlen(value);
        if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
            value[length - 1] = '\0';
            ++value;
        }
        length                 = strlen(value);
        const size_t copy_size = length < name_size - 1U ? length : name_size - 1U;
        memcpy(name, value, copy_size);
        name[copy_size] = '\0';
        break;
    }
    fclose(file);
}

static int64_t read_memory_total(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    char line[256];
    int64_t bytes = 0;
    while (fgets(line, sizeof(line), file)) {
        long long kilobytes;
        if (sscanf(line, "MemTotal: %lld kB", &kilobytes) == 1) {
            bytes = (int64_t)kilobytes * 1024;
            break;
        }
    }
    fclose(file);
    return bytes;
}

int settings_about_read(const char *os_release_path, const char *model_path, const char *serial_path,
                        const char *meminfo_path, const char *storage_path, settings_about_t *about)
{
    if (!os_release_path || !model_path || !serial_path || !meminfo_path || !storage_path || !about)
        return SETTINGS_BACKEND_ERROR;
    memset(about, 0, sizeof(*about));
    if (gethostname(about->device_name, sizeof(about->device_name) - 1U) != 0)
        snprintf(about->device_name, sizeof(about->device_name), "CM0");
    snprintf(about->model, sizeof(about->model), "CM0 Device");
    snprintf(about->serial, sizeof(about->serial), "Unavailable");
    read_text_file(model_path, about->model, sizeof(about->model));
    read_text_file(serial_path, about->serial, sizeof(about->serial));
    read_os_name(os_release_path, about->os_name, sizeof(about->os_name));
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        snprintf(about->kernel, sizeof(about->kernel), "%.60s %.65s", system_info.sysname, system_info.release);
        snprintf(about->architecture, sizeof(about->architecture), "%s", system_info.machine);
        if (strcmp(about->os_name, "Unknown") == 0)
            snprintf(about->os_name, sizeof(about->os_name), "%s", system_info.sysname);
    } else {
        snprintf(about->kernel, sizeof(about->kernel), "Unknown");
        snprintf(about->architecture, sizeof(about->architecture), "Unknown");
    }
    about->memory_bytes = read_memory_total(meminfo_path);
    struct statvfs storage;
    if (statvfs(storage_path, &storage) == 0) {
        about->storage_total_bytes     = (int64_t)storage.f_blocks * (int64_t)storage.f_frsize;
        about->storage_available_bytes = (int64_t)storage.f_bavail * (int64_t)storage.f_frsize;
    }
    return SETTINGS_BACKEND_OK;
}

bool settings_command_available(const char *name)
{
    if (!name || !name[0]) return false;
    if (strchr(name, '/')) return access(name, X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path) return false;
    const char *cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, ':');
        size_t length   = end ? (size_t)(end - cursor) : strlen(cursor);
        char candidate[PATH_MAX];
        int written = snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)length, cursor, name);
        if (written > 0 && (size_t)written < sizeof(candidate) && access(candidate, X_OK) == 0) return true;
        cursor = end ? end + 1 : cursor + length;
    }
    return false;
}

void settings_format_bytes(int64_t bytes, char *text, size_t text_size)
{
    if (!text || !text_size) return;
    if (bytes <= 0) {
        snprintf(text, text_size, "--");
        return;
    }
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value               = (double)bytes;
    size_t unit                = 0;
    while (value >= 1000.0 && unit + 1U < sizeof(units) / sizeof(units[0])) {
        value /= 1000.0;
        ++unit;
    }
    if (value >= 100.0 || unit == 0)
        snprintf(text, text_size, "%.0f %s", value, units[unit]);
    else
        snprintf(text, text_size, "%.1f %s", value, units[unit]);
}

}  // namespace lilygo::settings::platform
