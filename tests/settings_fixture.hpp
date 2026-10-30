#ifndef LILYGO_UI_SETTINGS_TESTS_SETTINGS_FIXTURE_HPP
#define LILYGO_UI_SETTINGS_TESTS_SETTINGS_FIXTURE_HPP

#include "domain/settings_model.hpp"
#include "cellular_pty_fixture.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
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
        cellular_.reset();
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
            path("sudo"),    path("missing-bluetooth-dbus"),
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
        set_environment("LILYGO_UI_CELLULAR_DEVICE", cellular_device_path());
        set_environment("LILYGO_UI_SYSTEMCTL", path("systemctl"));
        set_environment("LILYGO_UI_SUDO", path("sudo"));
        set_environment("LILYGO_UI_BLUETOOTH_DBUS_HELPER", path("missing-bluetooth-dbus"));
        set_environment("LILYGO_UI_RASPI_CONFIG", path("raspi-config"));
        set_environment("LILYGO_UI_RUNNING_CMDLINE", path("running-cmdline"));
        set_environment("CM0_BLUETOOTHCTL", values.bluetoothctl);
        set_environment("LILYGO_UI_HAPTICS_DRIVER_DIR", path("haptics"));
        set_environment("CM0_TEST_COMMAND_LOG", command_log_path());
        set_environment("CM0_TEST_WIFI_OUTPUT", wifi_output_path());
        set_environment("CM0_TEST_WIFI_RADIO", wifi_radio_path());
        set_environment("CM0_TEST_PASSWORD_CAPTURE", password_capture_path());
        set_environment("CM0_TEST_BLUETOOTH_POWER", bluetooth_power_path());
        set_environment("CM0_TEST_BLUETOOTH_DEVICES_OUTPUT", bluetooth_devices_output_path());
        set_environment("CM0_TEST_ETHERNET_PROFILE", ethernet_profile_path());
        set_environment("CM0_TEST_ETHERNET_MODIFY", ethernet_modify_path());
        set_environment("CM0_TEST_ETHERNET_UP", ethernet_up_path());
    }

    [[nodiscard]] std::string command_log_path() const
    {
        return path("commands.log");
    }

    [[nodiscard]] std::string cellular_device_path() const
    {
        if (std::filesystem::exists(path("cellular-unavailable"))) return path("missing-ttyACM0");
        if (!cellular_) {
            cellular_ = std::make_unique<::test_support::CellularPtyFixture>([this](const std::string &command) {
                return cellular_response(command);
            });
        }
        return cellular_->device_path();
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

    [[nodiscard]] std::string ethernet_profile_path() const
    {
        return path("ethernet-profile");
    }

    [[nodiscard]] std::string ethernet_modify_path() const
    {
        return path("ethernet-modify");
    }

    [[nodiscard]] std::string ethernet_up_path() const
    {
        return path("ethernet-up");
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
    [[nodiscard]] std::string cellular_response(const std::string &command) const
    {
        {
            std::ofstream output(command_log_path(), std::ios::app | std::ios::binary);
            assert(output);
            output << command << '\n';
        }
        const auto present = [this](const char *name) { return std::filesystem::exists(path(name)); };
        const auto read = [this](const char *name) {
            std::ifstream input(path(name), std::ios::binary);
            return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        };
        const auto ok = [](const std::string &value) { return "\r\n" + value + "\r\nOK\r\n"; };
        const bool enabled = read("cellular-radio") == "enabled\n";
        const bool has_sim = !present("cellular-no-sim");

        if (command == "AT") return ok("");
        if (command == "AT+CFUN=?") return ok("+CFUN: (0,1,4,20,21,30,31,40,41,44)");
        if (command == "AT+CFUN?") return ok(enabled ? "+CFUN: 1" : "+CFUN: 4");
        if (command == "AT%XSYSTEMMODE?") return ok("%XSYSTEMMODE: 1,0,1,0");
        if (command == "AT+CFUN=20" || command == "AT+CFUN=21") {
            if (present("cellular-denied")) return "\r\n+CME ERROR: 3\r\n";
            if (!present("cellular-noop"))
                write("cellular-radio", command == "AT+CFUN=21" ? "enabled\n" : "disabled\n");
            return ok("");
        }
        if (command == "AT+CPIN?") return has_sim ? ok("+CPIN: READY") : "\r\n+CME ERROR: 10\r\n";
        if (command == "AT%XICCID") return has_sim ? ok("%XICCID: 89860012345678901234") : "\r\nERROR\r\n";
        if (command == "AT+CIMI") return has_sim ? ok("460001234567890") : "\r\nERROR\r\n";
        if (command == "AT+CEREG?") return ok(enabled && has_sim ? "+CEREG: 0,1" : "+CEREG: 0,0");
        if (command == "AT+CESQ") return ok("+CESQ: 99,99,255,255,15,57");
        if (command == "AT+COPS?") return ok("+COPS: 0,0,\"" + read("cellular-operator") + "\",7");
        if (command == "AT%XCONNSTAT?") {
            if (enabled && has_sim) return ok("%XCONNSTAT: 0,0,524,1048,708,650");
            return "\r\nERROR\r\n";
        }
        return "\r\nERROR\r\n";
    }

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
        write("serial-console", "1\n");
        write("serial-requires-authorization", "");
        write("reboot-requires-authorization", "");
        write("running-cmdline", "console=tty1 root=PARTUUID=test quiet\n");
        write("raspi-config", R"SH(#!/bin/sh
cd "$(dirname "$0")" || exit 1
printf '%s\n' "$*" >> commands.log
case "$*" in
  'nonint get_serial_cons')
    [ ! -f serial-query-fail ] || exit 1
    cat serial-console
    ;;
  'nonint do_serial_cons 0'|'nonint do_serial_cons 1')
    [ ! -f serial-requires-authorization ] || [ "$LILYGO_TEST_AUTHORIZED_SERIAL" = 1 ] || { printf 'Permission denied\n' >&2; exit 1; }
    [ ! -f serial-denied ] || { printf 'Permission denied\n' >&2; exit 1; }
    [ ! -f serial-noop ] || exit 0
    printf '%s\n' "$3" > serial-console
    [ ! -f serial-partial-fail ] || exit 1
    ;;
  *) exit 2 ;;
esac
)SH", 0700);
        write("ssh-service", "inactive\n");
        write("ssh-boot", "disabled\n");
        write("ssh-socket", "not-found\n");
        write("sudo", R"SH(#!/bin/sh
cd "$(dirname "$0")" || exit 1
printf 'sudo %s\n' "$*" >> commands.log
[ "$1" = -n ] && [ "$2" = -- ] || exit 2
shift 2
case "$*" in
  "$PWD/raspi-config nonint do_serial_cons 0"|"$PWD/raspi-config nonint do_serial_cons 1")
    [ ! -f serial-sudo-denied ] || { printf 'sudo: a password is required\n' >&2; exit 1; }
    export LILYGO_TEST_AUTHORIZED_SERIAL=1
    ;;
  "$PWD/systemctl --no-ask-password reboot")
    [ ! -f reboot-sudo-denied ] || { printf 'sudo: a password is required\n' >&2; exit 1; }
    export LILYGO_TEST_AUTHORIZED_REBOOT=1
    ;;
  "$PWD/systemctl --no-pager --no-ask-password enable --now ssh.service"|\
  "$PWD/systemctl --no-pager --no-ask-password disable --now ssh.service"|\
  "$PWD/systemctl --no-pager --no-ask-password disable --now ssh.socket ssh.service")
    [ ! -f ssh-sudo-denied ] || { printf 'sudo: a password is required\n' >&2; exit 1; }
    [ ! -f ssh-no-new-privileges ] || { printf "sudo: The 'no new privileges' flag is set, which prevents sudo from running as root\n" >&2; exit 1; }
    export LILYGO_TEST_AUTHORIZED_SSH=1
    ;;
  "$PWD/nmcli"\ *)
    [ ! -f ethernet-sudo-denied ] || { printf 'sudo: a password is required\n' >&2; exit 1; }
    ;;
  *) exit 2 ;;
esac
exec "$@"
)SH", 0700);
        write("systemctl", R"SH(#!/bin/sh
cd "$(dirname "$0")" || exit 1
printf '%s\n' "$*" >> commands.log
case "$*" in
  '--no-ask-password reboot')
    [ ! -f reboot-requires-authorization ] || [ "$LILYGO_TEST_AUTHORIZED_REBOOT" = 1 ] || { printf 'Access denied\n' >&2; exit 1; }
    [ ! -f reboot-denied ] || { printf 'Access denied\n' >&2; exit 1; }
    touch reboot-requested
    ;;
  *' show '*)
    [ ! -f ssh-show-fail ] || exit 1
    load=loaded
    [ ! -f ssh-missing ] || load=not-found
    [ ! -f ssh-masked ] || load=masked
    printf 'ActiveState=%s\nId=ssh.service\nLoadState=%s\nUnitFileState=%s\n\n' "$(cat ssh-service)" "$load" "$(cat ssh-boot)"
    socket=$(cat ssh-socket)
    if [ "$socket" = not-found ]; then
      printf 'Id=ssh.socket\nLoadState=not-found\nActiveState=inactive\nUnitFileState=\n'
    else
      printf 'Id=ssh.socket\nLoadState=loaded\nActiveState=%s\nUnitFileState=%s\n' "$socket" "$(cat ssh-boot)"
    fi
    ;;
  *' enable --now ssh.service')
    [ ! -f ssh-requires-authorization ] || [ "$LILYGO_TEST_AUTHORIZED_SSH" = 1 ] || { printf 'Access denied\n' >&2; exit 1; }
    [ ! -f ssh-denied ] || { printf 'Access denied\n' >&2; exit 1; }
    [ ! -f ssh-noop ] || exit 0
    printf 'active\n' > ssh-service
    printf 'enabled\n' > ssh-boot
    ;;
  *' disable --now '*'ssh.service')
    [ ! -f ssh-requires-authorization ] || [ "$LILYGO_TEST_AUTHORIZED_SSH" = 1 ] || { printf 'Access denied\n' >&2; exit 1; }
    [ ! -f ssh-denied ] || { printf 'Access denied\n' >&2; exit 1; }
    printf 'disabled\n' > ssh-boot
    [ ! -f ssh-stop-fail ] || { printf 'Failed to stop ssh.service\n' >&2; exit 1; }
    printf 'inactive\n' > ssh-service
    if [ "$(cat ssh-socket)" != not-found ]; then printf 'inactive\n' > ssh-socket; fi
    [ ! -f ssh-partial-fail ] || { printf 'Command reported failure after applying state\n' >&2; exit 1; }
    ;;
  *) exit 2 ;;
esac
)SH", 0700);
        write("haptics/1-005a/keyboard_feedback_strength", "50\n");
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

        // This bearer is separate from Wi-Fi and Ethernet.  Statistics alone
        // must not cause the generic interface scanner to classify it as LAN.
        write("net/wwan0/statistics/rx_bytes", "1048576\n");
        write("net/wwan0/statistics/tx_bytes", "524288\n");

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
        write("power/BAT0/model_name", "bq27220\n");
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
        write("power/BAT0/calibration_capacity", "5000 4200\n");
        write("power/BAT0/charge_now", "3444000\n");
        write("power/BAT0/time_to_empty_now", "54000\n");
        make_directory("power/bq25890-charger");
        write("power/bq25890-charger/type", "USB\n");
        write("power/bq25890-charger/model_name", "bq25890\n");
        write("power/bq25890-charger/status", "Discharging\n");
        write("power/bq25890-charger/health", "Good\n");
        write("power/bq25890-charger/online", "0\n");
        write("power/bq25890-charger/input_current_limit", "500000\n");
        write("power/bq25890-charger/constant_charge_current", "1024000\n");
        write("power/bq25890-charger/constant_charge_current_max", "2048000\n");
        write("power/bq25890-charger/constant_charge_voltage", "4208000\n");
        write("power/bq25890-charger/constant_charge_voltage_max", "4208000\n");

        write("os-release", "NAME=LILYGO\nPRETTY_NAME=\"LILYGO OS 2.0\"\n");
        write("model", "LILYGO CM0\n");
        write("serial", "CM0-2026-0001\n");
        write("meminfo", "MemTotal:        2048000 kB\nMemFree: 10 kB\n");
        write("commands.log", "");
        write("password-capture", "");
        write("wifi-radio", "enabled\n");
        write("cellular-radio", "enabled\n");
        write("cellular-operator", "China Mobile");
        write("bluetooth-powered", "yes\n");
        write("bluetooth-devices-output",
              "Device AA:BB:CC:DD:EE:01 Keyboard\tPaired: yes\n"
              "Device AA:BB:CC:DD:EE:02 Studio Speaker\tPaired: yes\n");
        write("ethernet-profile", "Wired connection 1\n");
        write("ethernet-modify", "");
        write("ethernet-up", "");
        write("wifi-output",
              "*:CM0 Lab:88:WPA2\n"
              ":Guest:64:--\n"
              ":Workshop:41:WPA2\n"
              ":12345678901234567890123456789012:35:WPA2\n");

        write("wifi-profiles", "11111111-1111-4111-8111-111111111111:802-11-wireless:yes:wlan0\n"
                               "22222222-2222-4222-8222-222222222222:802-11-wireless:no:--\n");
        write("wifi-profile-11111111-1111-4111-8111-111111111111",
              "connection.uuid:11111111-1111-4111-8111-111111111111\n"
              "connection.type:802-11-wireless\n802-11-wireless.ssid:CM0 Lab\n"
              "802-11-wireless-security.key-mgmt:wpa-psk\nipv4.method:auto\nipv6.method:auto\n");
        write("wifi-profile-22222222-2222-4222-8222-222222222222",
              "connection.uuid:22222222-2222-4222-8222-222222222222\n"
              "connection.type:802-11-wireless\n802-11-wireless.ssid:Home Network\n"
              "802-11-wireless-security.key-mgmt:wpa-psk\nipv4.method:auto\nipv6.method:auto\n");
        write("wifi-details", "GENERAL.DEVICE:wlan0\nGENERAL.STATE:100 (connected)\n"
                              "GENERAL.CON-UUID:11111111-1111-4111-8111-111111111111\n"
                              "GENERAL.HWADDR:84\\:2f\\:57\\:18\\:f2\\:8e\nGENERAL.MTU:1500\n"
                              "IP4.ADDRESS[1]:192.168.8.158/24\nIP4.GATEWAY:192.168.8.1\n"
                              "IP4.DNS[1]:192.168.8.1\nIP4.DOMAIN[1]:lan\n"
                              "IP6.ADDRESS[1]:fe80\\:\\:1234/64\nIP6.GATEWAY:\n");

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
              "fixture_dir=$(dirname \"$0\")\n"
              "case \"$*\" in\n"
              "  *'UUID,TYPE,ACTIVE,DEVICE connection show')\n"
              "    [ ! -f \"$fixture_dir/wifi-profiles-fail\" ] || exit 1\n"
              "    cat \"$fixture_dir/wifi-profiles\" ;;\n"
              "  *'connection show uuid '*)\n"
              "    for last do :; done\n"
              "    cat \"$fixture_dir/wifi-profile-$last\" ;;\n"
              "  *'device show wlan0')\n"
              "    [ ! -f \"$fixture_dir/wifi-details-fail\" ] || exit 1\n"
              "    cat \"$fixture_dir/wifi-details\" ;;\n"
              "  *'connection delete uuid '*)\n"
              "    [ ! -f \"$fixture_dir/wifi-forget-fail\" ] || exit 1\n"
              "    for last do :; done\n"
              "    sed \"/$last/d\" \"$fixture_dir/wifi-profiles\" > \"$fixture_dir/wifi-profiles-next\"\n"
              "    mv \"$fixture_dir/wifi-profiles-next\" \"$fixture_dir/wifi-profiles\" ;;\n"
              "  'radio wifi') cat \"$CM0_TEST_WIFI_RADIO\" ;;\n"
              "  *'device wifi list'*) cat \"$CM0_TEST_WIFI_OUTPUT\" ;;\n"
              "  *'device show eth0'*)\n"
              "    [ ! -f ethernet-profile-fail ] || exit 1\n"
              "    cat \"$CM0_TEST_ETHERNET_PROFILE\"\n"
              "    ;;\n"
              "  *'connection modify'*)\n"
              "    [ ! -f ethernet-modify-fail ] || exit 1\n"
              "    printf '%s\\n' \"$*\" > \"$CM0_TEST_ETHERNET_MODIFY\"\n"
              "    ;;\n"
              "  *'connection up'*)\n"
              "    [ ! -f ethernet-up-fail ] || exit 1\n"
              "    printf '%s\\n' \"$*\" > \"$CM0_TEST_ETHERNET_UP\"\n"
              "    ;;\n"
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
              "  *'info AA:BB:CC:DD:EE:01'*) printf 'Device AA:BB:CC:DD:EE:01\\n\\tName: Keyboard\\n\\tAlias: Keyboard\\n' ;;\n"
              "  *'info AA:BB:CC:DD:EE:02'*) printf 'Device AA:BB:CC:DD:EE:02\\n\\tName: Studio Speaker\\n\\tAlias: Studio Speaker\\n' ;;\n"
              "  devices)\n"
              "    if [ \"$CM0_TEST_BLUETOOTH_DEVICES_FAIL\" = 1 ]; then\n"
              "      printf 'Unable to list devices\\n' >&2\n"
              "      exit 1\n"
              "    fi\n"
              "    cat \"$CM0_TEST_BLUETOOTH_DEVICES_OUTPUT\"\n"
              "    ;;\n"
              "  paired-devices) cat \"$CM0_TEST_BLUETOOTH_DEVICES_OUTPUT\" ;;\n"
              "  *'pair '*) printf 'Pairing successful\\n' ;;\n"
              "  *'power off'*) printf 'no\\n' > \"$CM0_TEST_BLUETOOTH_POWER\" ;;\n"
              "  *'power on'*) printf 'yes\\n' > \"$CM0_TEST_BLUETOOTH_POWER\" ;;\n"
              "esac\n",
              0700);
    }

    std::filesystem::path root_;
    mutable std::unique_ptr<::test_support::CellularPtyFixture> cellular_;
};

}  // namespace lilygo::settings::test_support

#endif
