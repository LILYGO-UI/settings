#include "domain/settings_model.hpp"
#include "pages/keyboard/keyboard_model.hpp"
#include "pages/sharing/sharing_model.hpp"
#include "pages/sharing/serial_console_model.hpp"
#include "pages/wifi/wifi_model.hpp"
#include "settings_fixture.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <string>
#include <utility>

namespace {

using lilygo::settings::KeyboardAvailability;
using lilygo::settings::KeyboardEnvironment;
using lilygo::settings::KeyboardModel;
using lilygo::settings::SettingsEnvironment;
using lilygo::settings::SettingsModel;
using lilygo::settings::WifiNetwork;
using lilygo::settings::test_support::SettingsFixture;

std::string read_file(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void test_refreshes_domain_state()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());

    model.refresh_all();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(
        model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                          "\tPowered: yes\n"));

    assert(model.wifi_available());
    assert(model.bluetooth_available());
    assert(model.wifi_on());
    assert(model.bluetooth_on());
    assert(model.wifi_control_enabled());
    assert(model.bluetooth_control_enabled());
    assert(model.nmcli_available());
    assert(model.bluetoothctl_available());

    assert(model.wifi_interfaces().size() == 1);
    assert(model.wifi_interfaces().front().name == "wlan0");
    assert(model.wifi_interfaces().front().connected);
    assert(model.ethernet_interfaces().size() == 2);
    assert(model.ethernet_interfaces().front().name == "eth0");
    assert(model.ethernet_interfaces().front().connected);
    assert(model.ethernet_interfaces().front().speed_mbps == 1000);
    assert(model.ethernet_interfaces().front().duplex == "full");

    const auto &battery = model.battery();
    assert(battery.available);
    assert(battery.has_capacity && battery.capacity_percent == 82);
    assert(battery.has_cycle_count && battery.cycle_count == 42);
    assert(battery.has_temperature && battery.temperature_tenths_c == 315);
    assert(battery.charge_full_uah == 4200000);

    const auto &about = model.about();
    assert(about.model == "LILYGO CM0");
    assert(about.serial == "CM0-2026-0001");
    assert(about.os_name == "LILYGO OS 2.0");
    assert(about.memory_bytes == 2097152000LL);
    assert(about.storage_total_bytes > 0);

    assert(model.set_wifi_radio_enabled(false));
    assert(read_file(fixture.path("rfkill/rfkill0/soft")) == "1\n");
    model.refresh_connectivity();
    assert(!model.wifi_on());
    assert(model.set_wifi_radio_enabled(true));
    assert(model.set_bluetooth_radio_enabled(false));
    model.refresh_connectivity();
    assert(model.wifi_on());
    assert(!model.bluetooth_on());
}

void test_keyboard_feedback_strength_persists()
{
    SettingsFixture fixture;
    fixture.export_environment();
    KeyboardModel model(KeyboardEnvironment{fixture.path("haptics")});
    model.refresh();
    assert(model.availability() == KeyboardAvailability::ready);
    assert(model.strength() == 50);
    assert(model.set_strength(75));
    assert(model.strength() == 75);
    assert(read_file(fixture.path("haptics/1-005a/keyboard_feedback_strength")) == "75\n");
    assert(!model.set_strength(101));
}

void test_keyboard_feedback_is_safe_when_driver_is_missing()
{
    KeyboardModel model(KeyboardEnvironment{"/not/a/haptics/driver"});
    model.refresh();
    assert(model.availability() == KeyboardAvailability::unavailable);
    assert(!model.set_strength(25));
}

void test_parses_command_results_into_model_state()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));

    assert(
        model.update_wifi_networks(":Guest:45:--\n"
                                   "*:Office\\:5G:78:WPA2\n"
                                   ":Guest:72:--\n"
                                   ":Cafe\\\\West:30:WPA1 WPA2\n"));
    assert(model.wifi_networks().size() == 3);
    assert(model.wifi_networks()[0].ssid == "Office:5G");
    assert(model.wifi_networks()[0].connected);
    assert(model.wifi_networks()[1].ssid == "Guest");
    assert(model.wifi_networks()[1].signal == 72);
    assert(model.wifi_networks()[1].security == "Open");
    assert(model.wifi_networks()[2].ssid == "Cafe\\West");
    assert(model.connected_wifi() == &model.wifi_networks()[0]);

    const auto initial_generation = model.wifi_network_generation();
    assert(
        model.update_wifi_networks("*:Connected First:35:WPA2\n"
                                   ":Connected First:99:WPA2\n"));
    assert(model.wifi_networks().size() == 1);
    assert(model.wifi_networks()[0].connected);
    assert(model.wifi_networks()[0].signal == 35);

    assert(
        model.update_wifi_networks(":Connected Last:99:WPA2\n"
                                   "*:Connected Last:42:WPA2\n"));
    assert(model.wifi_networks().size() == 1);
    assert(model.wifi_networks()[0].connected);
    assert(model.wifi_networks()[0].signal == 42);
    const auto connected_last_generation = model.wifi_network_generation();
    assert(connected_last_generation > initial_generation);
    assert(
        model.update_wifi_networks(":Connected Last:99:WPA2\n"
                                   "*:Connected Last:42:WPA2\n"));
    assert(model.wifi_network_generation() == connected_last_generation);

    assert(
        model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:01 Keyboard\n"
                                       "[NEW] Device AA:BB:CC:DD:EE:02 Living Room Speaker\n"
                                       "[CHG] Device AA:BB:CC:DD:EE:03 RSSI: -61\n"
                                       "Device AA:BB:CC:DD:EE:01 Keyboard\n"));
    assert(model.bluetooth_devices().size() == 2);
    assert(model.bluetooth_devices()[0].address == "AA:BB:CC:DD:EE:01");
    assert(model.bluetooth_devices()[1].name == "Living Room Speaker");

    assert(model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:09\n"));
    assert(model.bluetooth_devices().size() == 1);
    assert(model.bluetooth_devices()[0].name.empty());
    assert(model.update_bluetooth_device_name(
        "AA:BB:CC:DD:EE:09", "Device AA:BB:CC:DD:EE:09\n\tName: Desk Speaker\n\tAlias: Desk Speaker\n"));
    assert(model.bluetooth_devices()[0].name == "Desk Speaker");

    SettingsModel unnamed_model(fixture.environment());
    assert(unnamed_model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:04\n"
                                                  "[CHG] Device AA:BB:CC:DD:EE:04 Name: Office Headset\n"));
    assert(unnamed_model.bluetooth_devices().size() == 1);
    assert(unnamed_model.bluetooth_devices()[0].name == "Office Headset");
    assert(unnamed_model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:05\n"));
    assert(unnamed_model.bluetooth_devices().size() == 1);
    assert(unnamed_model.bluetooth_devices()[0].name.empty());
    assert(unnamed_model.update_bluetooth_device_name(
        "aa:bb:cc:dd:ee:05", "Device AA:BB:CC:DD:EE:05\n\tName: Living Room Speaker\n"));
    assert(unnamed_model.bluetooth_devices()[0].name == "Living Room Speaker");

    assert(
        model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                          "\tPowered: no\n"));
    assert(model.bluetooth_available());
    assert(!model.bluetooth_on());
    assert(read_file(fixture.path("rfkill/rfkill1/soft")) == "0\n");
    assert(
        model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                          "\tPowered: yes\n"));
    assert(model.bluetooth_on());

    model.clear_wifi_networks();
    model.clear_bluetooth_devices();
    assert(model.wifi_networks().empty());
    assert(model.bluetooth_devices().empty());
    assert(!model.update_wifi_networks(""));
    assert(!model.update_bluetooth_devices("invalid output"));
}

void test_power_change_rolls_back_without_rfkill()
{
    SettingsFixture fixture;
    auto environment        = fixture.environment();
    environment.rfkill_root = fixture.path("missing-rfkill");
    SettingsModel model(environment);

    model.refresh_connectivity();
    assert(!model.wifi_on());
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(model.wifi_on());
    assert(
        model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                          "\tPowered: yes\n"));
    assert(model.bluetooth_on());

    model.begin_wifi_power_change(false);
    model.refresh_connectivity();
    assert(!model.wifi_on());
    model.complete_wifi_power_change(false);
    model.refresh_connectivity();
    assert(model.wifi_on());

    assert(model.update_network_manager_wifi_radio("disabled\n"));
    model.refresh_connectivity();
    assert(!model.wifi_on());
    assert(!model.update_network_manager_wifi_radio("unexpected\n"));
    assert(!model.wifi_on());
    assert(model.update_network_manager_wifi_radio(" enabled \n"));
    assert(model.wifi_on());

    model.begin_bluetooth_power_change(false);
    model.complete_bluetooth_power_change(true);
    model.refresh_connectivity();
    assert(!model.bluetooth_on());
}

void test_network_manager_radio_failure_uses_observed_state()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());

    model.refresh_connectivity();
    assert(!model.wifi_on());
    model.mark_network_manager_wifi_radio_refresh_failed();
    assert(model.wifi_on());

    assert(model.update_network_manager_wifi_radio("disabled\n"));
    assert(!model.wifi_on());
    model.mark_network_manager_wifi_radio_refresh_failed();
    assert(!model.wifi_on());
}

void test_wifi_capacity_keeps_late_connected_network()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));

    std::string output;
    for (int index = 0; index < 12; ++index) {
        output += ":AP" + std::to_string(index) + ":" + std::to_string(100 - index) + ":WPA2\n";
    }
    output += "*:Weak Connected:1:WPA2\n";

    assert(model.update_wifi_networks(output));
    assert(model.wifi_networks().size() == 12);
    assert(model.wifi_networks().front().ssid == "Weak Connected");
    assert(model.wifi_networks().front().connected);
    assert(std::none_of(model.wifi_networks().begin(), model.wifi_networks().end(),
                        [](const WifiNetwork &network) { return network.ssid == "AP11"; }));

    const auto generation = model.wifi_network_generation();
    assert(model.update_wifi_networks(output));
    assert(model.wifi_network_generation() == generation);
}

void test_transient_bluetooth_failure_keeps_cached_state()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(
        model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                          "\tPowered: yes\n"));
    assert(model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:01 Keyboard\n"));

    model.mark_bluetooth_service_refresh_failed();
    assert(model.bluetooth_available());
    assert(model.bluetooth_on());
    assert(!model.bluetooth_control_enabled());
    assert(!model.bluetooth_uses_service_backend());
    assert(model.bluetooth_devices().size() == 1);

    assert(
        model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                          "\tPowered: yes\n"));
    assert(model.bluetooth_control_enabled());
    assert(model.bluetooth_uses_service_backend());
}

void test_tools_do_not_imply_hardware()
{
    SettingsFixture fixture;
    fixture.write("empty-net/.keep", "");
    fixture.write("empty-rfkill/.keep", "");
    auto environment         = fixture.environment();
    environment.network_root = fixture.path("empty-net");
    environment.rfkill_root  = fixture.path("empty-rfkill");
    SettingsModel model(environment);

    model.refresh_connectivity();
    assert(model.nmcli_available());
    assert(model.bluetoothctl_available());
    assert(!model.wifi_available());
    assert(!model.bluetooth_available());
    assert(!model.wifi_on());
    assert(!model.bluetooth_on());
    assert(!model.wifi_control_enabled());
    assert(!model.bluetooth_control_enabled());

    model.mark_bluetooth_controller_unavailable();
    assert(!model.bluetooth_available());
    assert(!model.bluetooth_on());
}

void test_external_wifi_disconnect_clears_cached_connection()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(model.update_wifi_networks("*:CM0 Lab:88:WPA2\n:Guest:64:--\n"));
    assert(model.connected_wifi());
    const auto generation = model.wifi_network_generation();

    fixture.write("net/wlan0/carrier", "0\n");
    fixture.write("net/wlan0/operstate", "down\n");
    model.refresh_connectivity();
    assert(!model.connected_wifi());
    assert(model.wifi_networks().size() == 2);
    assert(model.wifi_network_generation() == generation + 1);
}

void test_missing_platform_state_is_safe()
{
    SettingsEnvironment environment{
        "/not/a/network/root", "/not/a/rfkill/root", "/not/a/power/root",   "/not/an/os-release", "/not/a/model",
        "/not/a/serial",       "/not/a/meminfo",     "/not/a/storage/root", "/not/a/nmcli",       "/not/a/bluetoothctl",
    };
    SettingsModel model(environment);
    model.refresh_all();

    assert(!model.wifi_available());
    assert(!model.bluetooth_available());
    assert(!model.wifi_on());
    assert(!model.bluetooth_on());
    assert(!model.battery().available);
    assert(model.wifi_interfaces().empty());
    assert(model.ethernet_interfaces().empty());
}

void test_saved_wifi_profiles_and_connection_details()
{
    using lilygo::settings::WifiModel;
    constexpr char uuid[] = "11111111-1111-4111-8111-111111111111";
    const std::string profile = std::string("connection.uuid:") + uuid + R"(
connection.id:A friendly profile name
connection.type:802-11-wireless
802-11-wireless.ssid:Lab\:West\\AP
802-11-wireless-security.key-mgmt:wpa-psk
ipv4.method:auto
ipv6.method:auto
)";
    const auto saved = WifiModel::parse_profile(profile, uuid);
    assert(saved && saved->ssid == "Lab:West\\AP" && saved->security != "Open");
    assert(!WifiModel::parse_profile(profile, "22222222-2222-4222-8222-222222222222"));
    const auto references = WifiModel::parse_profiles(std::string(uuid) + ":802-11-wireless:yes:wlan0\n" +
        "22222222-2222-4222-8222-222222222222:802-3-ethernet:no:\n");
    assert(references && references->size() == 1 && references->front().active);
    assert(!WifiModel::parse_profiles("not-a-uuid:wifi:yes:wlan0\n"));
    assert(!WifiModel::valid_uuid(std::string(uuid) + " extra-argument"));
    const auto dashes = WifiModel::parse_profile(std::string("connection.uuid:") + uuid +
        "\nconnection.type:802-11-wireless\n802-11-wireless.ssid:--\n", uuid);
    assert(dashes && dashes->ssid == "--");

    const std::string device = std::string("GENERAL.CON-UUID:") + uuid + R"(
GENERAL.DEVICE:wlan0
GENERAL.STATE:100 (connected)
GENERAL.HWADDR:F0\:40\:AF\:90\:40\:36
GENERAL.MTU:1500
IP4.ADDRESS[1]:192.168.8.197/24
IP4.GATEWAY:192.168.8.1
IP4.DNS[1]:192.168.8.1
IP4.DNS[2]:1.1.1.1
IP4.DOMAIN[1]:lan
IP6.ADDRESS[1]:fe80\:\:1234/64
IP6.GATEWAY:--
)";
    auto details = WifiModel::parse_device(device, "wlan0");
    assert(details && details->ipv4_address == "192.168.8.197");
    assert(details->subnet_mask == "255.255.255.0" && details->ipv4_gateway == "192.168.8.1");
    assert(details->dns_servers == "192.168.8.1\n1.1.1.1" && details->search_domains == "lan");
    assert(details->ipv6_address == "fe80::1234/64" && details->ipv6_gateway.empty());
    assert(details->mac == "F0:40:AF:90:40:36" && details->mtu == "1500");
    assert(WifiModel::apply_details_profile(*details, profile));
    assert(details->ssid == saved->ssid && details->ipv4_method == "Using DHCP");
    assert(!WifiModel::parse_device(device, "wlan1"));
    assert(!WifiModel::parse_device("GENERAL.DEVICE:wlan0\nGENERAL.STATE:30 (disconnected)\n", "wlan0"));
    for (const auto &[prefix, mask] : std::vector<std::pair<std::string, std::string>>{
             {"0", "0.0.0.0"}, {"32", "255.255.255.255"}}) {
        auto sample = device;
        sample.replace(sample.find("/24"), 3, "/" + prefix);
        assert(WifiModel::parse_device(sample, "wlan0")->subnet_mask == mask);
    }

    WifiModel model;
    auto nearby = *saved;
    nearby.in_range = true;
    model.replace_known({nearby});
    const auto generation = model.generation();
    model.replace_known({nearby});
    model.update_reachability({{saved->ssid, "WPA2", 80, false}}, true);
    assert(model.generation() == generation);
    model.update_reachability({}, false);
    assert(model.known_networks().size() == 1 && !model.known_networks()[0].in_range);
    model.remove_known(uuid);
    assert(model.known_networks().empty());
}

}  // namespace

void test_sharing_service_state()
{
    using lilygo::settings::SharingModel;
    SharingModel model({"/test/systemctl"});
    assert(model.change_arguments(true).empty());
    const std::string missing_socket =
        "\nId=ssh.socket\nLoadState=not-found\nActiveState=inactive\nUnitFileState=\n";
    const std::string inactive_service =
        "Id=ssh.service\nLoadState=loaded\nActiveState=inactive\nUnitFileState=disabled\n";
    assert(model.update(inactive_service + missing_socket));
    assert(model.state().available && !model.state().enabled && !model.state().starts_at_boot);
    assert(model.change_arguments(true) == (std::vector<std::string>{
        "/test/systemctl", "--no-pager", "--no-ask-password", "enable", "--now", "ssh.service"}));
    assert(model.change_arguments(false).back() == "ssh.service");
    assert(model.change_arguments(false).size() == 6);

    SharingModel authorized({"/test/systemctl", "/test/sudo"});
    assert(authorized.update(inactive_service + missing_socket));
    assert(authorized.query_arguments().front() == "/test/systemctl"); // Reads need no administrator privilege.
    assert(authorized.change_arguments(true) == (std::vector<std::string>{
        "/test/sudo", "-n", "--", "/test/systemctl", "--no-pager", "--no-ask-password",
        "enable", "--now", "ssh.service"}));
    assert(authorized.change_arguments(false) == (std::vector<std::string>{
        "/test/sudo", "-n", "--", "/test/systemctl", "--no-pager", "--no-ask-password",
        "disable", "--now", "ssh.service"}));

    assert(model.update(inactive_service +
        "\nUnitFileState=enabled\nActiveState=active\nLoadState=loaded\nId=ssh.socket\n"));
    assert(model.state().enabled && model.state().starts_at_boot);
    assert(model.change_arguments(false) == (std::vector<std::string>{
        "/test/systemctl", "--no-pager", "--no-ask-password", "disable", "--now", "ssh.socket", "ssh.service"}));
    assert(!model.update("garbled output"));
    assert(!model.state().known && model.state().enabled); // Preserve last observation, disable control.
    assert(model.change_arguments(false).empty());

    assert(model.update("Id=ssh.service\nLoadState=masked\nActiveState=inactive\nUnitFileState=masked\n" + missing_socket));
    assert(model.state().masked && model.change_arguments(true).empty());
    assert(model.update("Id=ssh.service\nLoadState=not-found\nActiveState=inactive\nUnitFileState=\n" + missing_socket));
    assert(!model.state().available && model.change_arguments(true).empty());
    assert(model.update("Id=ssh.service\nLoadState=loaded\nActiveState=activating\nUnitFileState=enabled\n" + missing_socket));
    assert(model.state().transitioning && model.change_arguments(true).empty());
    assert(!model.update(inactive_service)); // Incomplete snapshot must not imply the socket is off.
}

int main()
{
    {
        using lilygo::settings::SerialConsoleModel;
        SettingsFixture fixture;
        const lilygo::settings::SerialConsoleEnvironment environment{
            fixture.path("raspi-config"), fixture.path("systemctl"), fixture.path("running-cmdline")};
        SerialConsoleModel model(environment);
        assert(model.change_arguments(true).empty());
        assert(model.update("1\n") && !model.enabled() && !model.reboot_required());
        assert(model.update(" 0\n") && model.enabled() && model.reboot_required());
        assert(model.change_arguments(false) == (std::vector<std::string>{
            fixture.path("raspi-config"), "nonint", "do_serial_cons", "1"}));
        assert(model.change_arguments(true).back() == "0");
        SerialConsoleModel authorized({fixture.path("raspi-config"), fixture.path("systemctl"),
                                       fixture.path("running-cmdline"), fixture.path("sudo")});
        assert(authorized.query_arguments() == (std::vector<std::string>{
            fixture.path("raspi-config"), "nonint", "get_serial_cons"}));
        assert(authorized.update("1\n"));
        for (bool enabled : {false, true}) {
            assert(authorized.change_arguments(enabled) == (std::vector<std::string>{
                fixture.path("sudo"), "-n", "--", fixture.path("raspi-config"),
                "nonint", "do_serial_cons", enabled ? "0" : "1"}));
        }
        assert(authorized.reboot_arguments() == (std::vector<std::string>{
            fixture.path("sudo"), "-n", "--", fixture.path("systemctl"), "--no-ask-password", "reboot"}));
        SerialConsoleModel reopened(environment);
        assert(reopened.update("0\n") && reopened.reboot_required());
        fixture.write("running-cmdline", "console=tty1 console=serial0,115200 root=PARTUUID=test\n");
        assert(reopened.update("0\n") && !reopened.reboot_required());
        assert(reopened.update("1\n") && reopened.reboot_required());
        for (const char *invalid : {"", "2", "0\n1\n", "Permission denied\n1\n"}) {
            assert(!model.update(invalid) && !model.known());
            assert(model.change_arguments(true).empty());
        }
        assert(SerialConsoleModel::has_serial_console("console=ttyAMA10,115200 root=test"));
        assert(SerialConsoleModel::has_serial_console("console=ttyS0,115200 console=tty1"));
        assert(!SerialConsoleModel::has_serial_console("console=tty1 earlycon=serial0 testconsole=ttyS0"));
        assert(!SerialConsoleModel::has_serial_console("console=ttySfoobar"));
    }
    test_sharing_service_state();
    test_saved_wifi_profiles_and_connection_details();
    test_keyboard_feedback_strength_persists();
    test_keyboard_feedback_is_safe_when_driver_is_missing();
    test_refreshes_domain_state();
    test_parses_command_results_into_model_state();
    test_power_change_rolls_back_without_rfkill();
    test_network_manager_radio_failure_uses_observed_state();
    test_wifi_capacity_keeps_late_connected_network();
    test_transient_bluetooth_failure_keeps_cached_state();
    test_tools_do_not_imply_hardware();
    test_external_wifi_disconnect_clears_cached_connection();
    test_missing_platform_state_is_safe();
    return 0;
}
