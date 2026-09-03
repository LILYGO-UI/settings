#include "domain/settings_model.hpp"
#include "settings_fixture.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <string>

namespace {

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

}  // namespace

int main()
{
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
