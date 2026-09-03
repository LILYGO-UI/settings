#ifndef LILYGO_UI_SETTINGS_TESTS_SETTINGS_FIXTURE_HPP
#define LILYGO_UI_SETTINGS_TESTS_SETTINGS_FIXTURE_HPP

#include "domain/settings_model.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <sys/stat.h>
#include <unistd.h>

namespace lilygo::settings::test_support {

class SettingsFixture {
public:
    SettingsFixture()
    {
        char path[]         = "/tmp/lilygo-ui-settings-test-XXXXXX";
        const char *created = mkdtemp(path);
        assert(created != nullptr);
        root_ = created;
        populate();
    }

    ~SettingsFixture()
    {
        if (root_.string().rfind("/tmp/lilygo-ui-settings-test-", 0) == 0) {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
    }

    SettingsFixture(const SettingsFixture &)            = delete;
    SettingsFixture &operator=(const SettingsFixture &) = delete;

    [[nodiscard]] std::string path(std::string_view relative = {}) const
    {
        return relative.empty() ? root_.string() : (root_ / std::filesystem::path(relative)).string();
    }

    [[nodiscard]] SettingsEnvironment environment() const
    {
        return {
            path("net"),    path("rfkill"),  path("power"), path("os-release"), path("model"),
            path("serial"), path("meminfo"), path(),        path("nmcli"),      path("bluetoothctl"),
        };
    }

    void export_environment() const
    {
        const auto values = environment();
        set_environment("CM0_NETWORK_DIR", values.network_root);
        set_environment("CM0_RFKILL_DIR", values.rfkill_root);
        set_environment("CM0_POWER_SUPPLY_DIR", values.power_supply_root);
        set_environment("CM0_OS_RELEASE_FILE", values.os_release);
        set_environment("CM0_DEVICE_MODEL_FILE", values.model_file);
        set_environment("CM0_DEVICE_SERIAL_FILE", values.serial_file);
        set_environment("CM0_MEMINFO_FILE", values.meminfo_file);
        set_environment("CM0_STORAGE_PATH", values.storage_path);
        set_environment("CM0_NMCLI", values.nmcli);
        set_environment("CM0_BLUETOOTHCTL", values.bluetoothctl);
        set_environment("CM0_TEST_COMMAND_LOG", command_log_path());
        set_environment("CM0_TEST_WIFI_OUTPUT", wifi_output_path());
        set_environment("CM0_TEST_WIFI_RADIO", wifi_radio_path());
        set_environment("CM0_TEST_PASSWORD_CAPTURE", password_capture_path());
        set_environment("CM0_TEST_BLUETOOTH_POWER", bluetooth_power_path());
        set_environment("CM0_TEST_BLUETOOTH_DEVICES_OUTPUT", bluetooth_devices_output_path());
    }

    [[nodiscard]] std::string command_log_path() const
    {
        return path("commands.log");
    }

    [[nodiscard]] std::string wifi_output_path() const
    {
        return path("wifi-output");
    }

    [[nodiscard]] std::string password_capture_path() const
    {
        return path("password-capture");
    }

    [[nodiscard]] std::string wifi_radio_path() const
    {
        return path("wifi-radio");
    }

    [[nodiscard]] std::string bluetooth_power_path() const
    {
        return path("bluetooth-powered");
    }

    [[nodiscard]] std::string bluetooth_devices_output_path() const
    {
        return path("bluetooth-devices-output");
    }

    [[nodiscard]] std::string command_log() const
    {
        std::ifstream input(command_log_path(), std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void write(std::string_view relative, std::string_view value, mode_t mode = 0600) const
    {
        const auto destination = root_ / std::filesystem::path(relative);
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        assert(!error);
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        assert(output);
        output << value;
        output.close();
        assert(output);
        assert(chmod(destination.c_str(), mode) == 0);
    }

private:
    static void set_environment(const char *name, const std::string &value)
    {
        assert(setenv(name, value.c_str(), 1) == 0);
    }

    void make_directory(std::string_view relative) const
    {
        std::error_code error;
        std::filesystem::create_directories(root_ / std::filesystem::path(relative), error);
        assert(!error);
    }

    void populate()
    {
        make_directory("net/wlan0/wireless");
        write("net/wlan0/operstate", "up\n");
        write("net/wlan0/address", "02:00:00:00:00:10\n");
        write("net/wlan0/carrier", "1\n");
        write("net/wlan0/mtu", "1500\n");

        make_directory("net/eth1");
        write("net/eth1/operstate", "down\n");
        write("net/eth1/address", "02:00:00:00:00:21\n");
        write("net/eth1/carrier", "0\n");
        write("net/eth1/speed", "100\n");
        write("net/eth1/duplex", "half\n");
        write("net/eth1/mtu", "1500\n");

        make_directory("net/eth0");
        write("net/eth0/operstate", "up\n");
        write("net/eth0/address", "02:00:00:00:00:20\n");
        write("net/eth0/carrier", "1\n");
        write("net/eth0/speed", "1000\n");
        write("net/eth0/duplex", "full\n");
        write("net/eth0/mtu", "1500\n");

        make_directory("rfkill/rfkill0");
        write("rfkill/rfkill0/type", "wlan\n");
        write("rfkill/rfkill0/soft", "0\n");
        write("rfkill/rfkill0/hard", "0\n");
        make_directory("rfkill/rfkill1");
        write("rfkill/rfkill1/type", "bluetooth\n");
        write("rfkill/rfkill1/soft", "0\n");
        write("rfkill/rfkill1/hard", "0\n");

        make_directory("power/AC");
        write("power/AC/type", "Mains\n");
        make_directory("power/BAT0");
        write("power/BAT0/type", "Battery\n");
        write("power/BAT0/present", "1\n");
        write("power/BAT0/capacity", "82\n");
        write("power/BAT0/status", "Discharging\n");
        write("power/BAT0/health", "Good\n");
        write("power/BAT0/technology", "Li-ion\n");
        write("power/BAT0/cycle_count", "42\n");
        write("power/BAT0/voltage_now", "4010000\n");
        write("power/BAT0/current_now", "-225000\n");
        write("power/BAT0/temp", "315\n");
        write("power/BAT0/charge_full", "4200000\n");
        write("power/BAT0/charge_full_design", "5000000\n");

        write("os-release", "NAME=LILYGO\nPRETTY_NAME=\"LILYGO OS 2.0\"\n");
        write("model", "LILYGO CM0\n");
        write("serial", "CM0-2026-0001\n");
        write("meminfo", "MemTotal:        2048000 kB\nMemFree: 10 kB\n");
        write("commands.log", "");
        write("password-capture", "");
        write("wifi-radio", "enabled\n");
        write("bluetooth-powered", "yes\n");
        write("bluetooth-devices-output",
              "Device AA:BB:CC:DD:EE:01 Keyboard\n"
              "Device AA:BB:CC:DD:EE:02 Studio Speaker\n");
        write("wifi-output",
              "*:CM0 Lab:88:WPA2\n"
              ":Guest:64:--\n"
              ":Workshop:41:WPA2\n"
              ":12345678901234567890123456789012:35:WPA2\n");

        write("nmcli",
              "#!/bin/sh\n"
              "if [ \"$CM0_TEST_HANG_COMMAND\" = 1 ]; then\n"
              "  trap '' TERM\n"
              "  exec sleep 30\n"
              "fi\n"
              "printf '%s\\n' \"$*\" >> \"$CM0_TEST_COMMAND_LOG\"\n"
              "if [ \"$*\" = 'radio wifi' ] && [ \"$CM0_TEST_WIFI_RADIO_FAIL\" = 1 ]; then\n"
              "  printf 'NetworkManager radio query failed\\n' >&2\n"
              "  exit 1\n"
              "fi\n"
              "case \"$*\" in\n"
              "  'radio wifi') cat \"$CM0_TEST_WIFI_RADIO\" ;;\n"
              "  *'device wifi list'*) cat \"$CM0_TEST_WIFI_OUTPUT\" ;;\n"
              "  *'--ask'*'device wifi connect'*)\n"
              "    IFS= read -r secret || exit 4\n"
              "    printf '%s' \"$secret\" > \"$CM0_TEST_PASSWORD_CAPTURE\"\n"
              "    [ -n \"$secret\" ] || exit 5\n"
              "    ;;\n"
              "  *'radio wifi off'*)\n"
              "    printf 'disabled\\n' > \"$CM0_TEST_WIFI_RADIO\"\n"
              "    [ ! -f \"$CM0_RFKILL_DIR/rfkill0/soft\" ] || printf '1\\n' > \"$CM0_RFKILL_DIR/rfkill0/soft\"\n"
              "    ;;\n"
              "  *'radio wifi on'*)\n"
              "    printf 'enabled\\n' > \"$CM0_TEST_WIFI_RADIO\"\n"
              "    [ ! -f \"$CM0_RFKILL_DIR/rfkill0/soft\" ] || printf '0\\n' > \"$CM0_RFKILL_DIR/rfkill0/soft\"\n"
              "    ;;\n"
              "esac\n",
              0700);
        write("bluetoothctl",
              "#!/bin/sh\n"
              "printf '%s\\n' \"$*\" >> \"$CM0_TEST_COMMAND_LOG\"\n"
              "if [ \"$*\" = show ] && [ \"$CM0_TEST_BLUETOOTH_SHOW_FAIL\" = 1 ]; then\n"
              "  printf 'BlueZ service unavailable\\n' >&2\n"
              "  exit 1\n"
              "fi\n"
              "if [ \"$*\" = show ] && [ \"$CM0_TEST_BLUETOOTH_SHOW_TRANSIENT_FAIL\" = 1 ]; then\n"
              "  printf 'Operation failed temporarily\\n' >&2\n"
              "  exit 1\n"
              "fi\n"
              "case \"$*\" in\n"
              "  show)\n"
              "    if [ \"$CM0_TEST_NO_BLUETOOTH_CONTROLLER\" = 1 ]; then\n"
              "      printf 'No default controller available\\n' >&2\n"
              "      exit 1\n"
              "    fi\n"
              "    powered=$(sed -n '1p' \"$CM0_TEST_BLUETOOTH_POWER\")\n"
              "    printf 'Controller AA:BB:CC:DD:EE:FF LILYGO [default]\\n\\tPowered: %s\\n' \"$powered\"\n"
              "    ;;\n"
              "  *'scan on'*) printf '[CHG] Device AA:BB:CC:DD:EE:09 RSSI: -55\\n' ;;\n"
              "  devices)\n"
              "    if [ \"$CM0_TEST_BLUETOOTH_DEVICES_FAIL\" = 1 ]; then\n"
              "      printf 'Unable to list devices\\n' >&2\n"
              "      exit 1\n"
              "    fi\n"
              "    cat \"$CM0_TEST_BLUETOOTH_DEVICES_OUTPUT\"\n"
              "    ;;\n"
              "  *'power off'*) printf 'no\\n' > \"$CM0_TEST_BLUETOOTH_POWER\" ;;\n"
              "  *'power on'*) printf 'yes\\n' > \"$CM0_TEST_BLUETOOTH_POWER\" ;;\n"
              "esac\n",
              0700);
    }

    std::filesystem::path root_;
};

}  // namespace lilygo::settings::test_support

#endif
