#include "pages/settings/settings_view_model.hpp"
#include "settings_fixture.hpp"

#include <lvgl.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

namespace {

using namespace lilygo::settings;
using lilygo::settings::test_support::SettingsFixture;

struct ObserverProbe {
    int calls          = 0;
    std::int32_t value = 0;
};

void revision_changed(lv_observer_t *observer, lv_subject_t *subject)
{
    auto *probe = static_cast<ObserverProbe *>(lv_observer_get_user_data(observer));
    ++probe->calls;
    probe->value = lv_subject_get_int(subject);
}

SettingsCommandResult wait_for_result(SettingsCommandRunner &runner)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = runner.poll()) return *result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "settings command did not finish");
    return {};
}

std::string read_file(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void drain_commands(SettingsViewModel &view_model)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (view_model.busy() && std::chrono::steady_clock::now() < deadline) {
        view_model.poll_command();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(!view_model.busy());
}

void finish_wifi_command(WifiViewModel &view_model, SettingsCommandRunner &runner)
{
    auto result = wait_for_result(runner);
    view_model.command_finished(result);
    if (runner.busy()) {
        result = wait_for_result(runner);
        view_model.command_finished(result);
    }
    assert(!runner.busy());
}

std::size_t wifi_index(const WifiViewModel &view_model, const std::string &ssid)
{
    const auto &networks = view_model.networks();
    const auto item      = std::find_if(networks.begin(), networks.end(),
                                        [&ssid](const WifiNetwork &network) { return network.ssid == ssid; });
    assert(item != networks.end());
    return static_cast<std::size_t>(std::distance(networks.begin(), item));
}

void test_passive_presentations_and_subjects()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());
    model.refresh_all();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(
        model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                          "\tPowered: yes\n"));
    assert(model.update_wifi_networks("*:CM0 Lab:88:WPA2\n"));
    assert(model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:01 Keyboard\n"));

    HomeViewModel home(model);
    ObserverProbe home_probe;
    lv_observer_t *home_observer = lv_subject_add_observer(home.revision_subject(), revision_changed, &home_probe);
    assert(home_observer);
    const int home_calls     = home_probe.calls;
    const auto home_revision = lv_subject_get_int(home.revision_subject());
    home.publish();
    assert(home_probe.calls == home_calls + 1);
    assert(home_probe.value == home_revision + 1);
    assert(home.presentation().wifi == "CM0 Lab");
    assert(home.presentation().bluetooth == "On");
    assert(home.presentation().ethernet == "Connected");
    assert(home.presentation().battery == "82%");
    lv_observer_remove(home_observer);

    EthernetViewModel ethernet(model);
    const auto ethernet_revision = lv_subject_get_int(ethernet.revision_subject());
    ethernet.publish();
    assert(lv_subject_get_int(ethernet.revision_subject()) == ethernet_revision + 1);
    assert(ethernet.presentation().status == "Connected");
    assert(ethernet.presentation().interface_name == "eth0");
    assert(ethernet.presentation().speed == "1000 Mbps");
    assert(ethernet.presentation().duplex == "full");
    assert(ethernet.presentation().mtu == "1500");

    BatteryViewModel battery(model);
    battery.publish();
    assert(battery.presentation().percent == "82%");
    assert(battery.presentation().capacity == "84%");
    assert(battery.presentation().cycles == "42");
    assert(battery.presentation().voltage == "4.010 V");
    assert(battery.presentation().current == "-0.225 A");
    assert(battery.presentation().temperature == "31.5 C");
    assert(battery.presentation().bar_value == 82);

    GeneralViewModel general(model);
    general.publish();
    assert(general.about_detail().find("LILYGO OS 2.0") != std::string::npos);

    AboutViewModel about(model);
    about.publish();
    assert(about.presentation().model == "LILYGO CM0");
    assert(about.presentation().serial == "CM0-2026-0001");
    assert(about.presentation().os == "LILYGO OS 2.0");
}

void test_ethernet_manual_configuration_and_validation()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    SettingsCommandRunner runner;
    EthernetViewModel ethernet(model, runner);
    ethernet.publish();
    assert(ethernet.presentation().configurable);

    ethernet.begin_configuration();
    assert(ethernet.apply_configuration("192.168.8.50", "255.255.255.0", "192.168.8.1",
                                        "1.1.1.1, 8.8.8.8"));
    assert(ethernet.configuration_busy());

    auto result = wait_for_result(runner);
    assert(result.command == SettingsCommand::ethernet_profile_lookup);
    assert(result.success);
    ethernet.command_finished(result);

    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::ethernet_modify);
    assert(result.success);
    ethernet.command_finished(result);

    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::ethernet_up);
    assert(result.success);
    ethernet.command_finished(result);

    assert(!ethernet.configuration_busy());
    assert(ethernet.presentation().configuration_status == "Ethernet Settings Saved");
    assert(ethernet.presentation().ipv4 == "192.168.8.50");
    assert(ethernet.presentation().subnet == "255.255.255.0");
    assert(ethernet.presentation().gateway == "192.168.8.1");
    assert(ethernet.presentation().dns == "1.1.1.1,8.8.8.8");

    const auto modify = read_file(fixture.ethernet_modify_path());
    assert(modify.find("connection modify Wired connection 1") != std::string::npos);
    assert(modify.find("ipv4.method manual") != std::string::npos);
    assert(modify.find("ipv4.addresses 192.168.8.50/24") != std::string::npos);
    assert(modify.find("ipv4.gateway 192.168.8.1") != std::string::npos);
    assert(modify.find("ipv4.dns 1.1.1.1,8.8.8.8") != std::string::npos);
    assert(read_file(fixture.ethernet_up_path()).find("connection up Wired connection 1") != std::string::npos);
    const auto log = fixture.command_log();
    assert(log.find("-t -f GENERAL.CONNECTION device show eth0") != std::string::npos);
    assert(log.find("sudo -n -- " + fixture.path("nmcli") + " connection modify") != std::string::npos);
    assert(log.find("sudo -n -- " + fixture.path("nmcli") + " connection up") != std::string::npos);

    ethernet.begin_configuration();
    assert(ethernet.apply_dhcp_configuration("9.9.9.9"));
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::ethernet_profile_lookup && result.success);
    ethernet.command_finished(result);
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::ethernet_modify && result.success);
    ethernet.command_finished(result);
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::ethernet_up && result.success);
    ethernet.command_finished(result);
    assert(ethernet.presentation().ipv4_dhcp);
    const auto dhcp_modify = read_file(fixture.ethernet_modify_path());
    assert(dhcp_modify.find("ipv4.method auto") != std::string::npos);
    assert(dhcp_modify.find("ipv4.addresses ") != std::string::npos);

    // Reject malformed addresses before starting a helper process.
    fixture.write("commands.log", "");
    ethernet.begin_configuration();
    assert(!ethernet.apply_configuration("192.168.8.999", "255.255.255.0", "192.168.8.1", "8.8.8.8"));
    assert(!ethernet.configuration_busy());
    assert(ethernet.presentation().configuration_status == "Invalid Ethernet Settings");
    assert(fixture.command_log().empty());
}

void test_ethernet_manual_configuration_reports_apply_failure()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    SettingsCommandRunner runner;
    EthernetViewModel ethernet(model, runner);
    fixture.write("ethernet-modify-fail", "");

    assert(ethernet.apply_configuration("192.168.8.60", "/24", "192.168.8.1", "8.8.8.8"));
    auto result = wait_for_result(runner);
    assert(result.command == SettingsCommand::ethernet_profile_lookup);
    ethernet.command_finished(result);
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::ethernet_modify);
    assert(!result.success);
    ethernet.command_finished(result);
    assert(!ethernet.configuration_busy());
    assert(ethernet.presentation().configuration_status == "Cannot Save Ethernet Settings");
    assert(!std::filesystem::exists(fixture.ethernet_up_path()) || read_file(fixture.ethernet_up_path()).empty());
}

void settle_wifi_background(WifiViewModel &wifi, SettingsCommandRunner &runner)
{
    for (int remaining = 24; remaining > 0; --remaining) {
        if (!runner.busy() && !wifi.resume_background()) return;
        wifi.command_finished(wait_for_result(runner));
    }
    assert(false && "Wi-Fi background work did not settle");
}

void test_known_wifi_details_and_forget_races()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(model.update_wifi_networks("*:CM0 Lab:88:WPA2\n"));
    SettingsCommandRunner runner;
    WifiViewModel wifi(model, runner);
    wifi.refresh();
    settle_wifi_background(wifi, runner);
    constexpr char current[] = "11111111-1111-4111-8111-111111111111";
    constexpr char saved[] = "22222222-2222-4222-8222-222222222222";
    assert(wifi.known_networks().size() == 2);
    assert(wifi.known_networks()[1].ssid == "Home Network" && !wifi.known_networks()[1].in_range);
    const auto generation = wifi.presentation().known_generation;
    wifi.refresh();
    settle_wifi_background(wifi, runner);
    assert(wifi.presentation().known_generation == generation);
    assert(wifi.activate_known_network(1) == WifiActionResult::started);
    settle_wifi_background(wifi, runner);
    assert(fixture.command_log().find(std::string("connection up uuid ") + saved) != std::string::npos);
    assert(wifi.pending_ssid().empty());
    wifi.open_details();
    settle_wifi_background(wifi, runner);
    assert(wifi.presentation().details_available && wifi.details().profile_uuid == current);
    assert(wifi.details().ipv4_address == "192.168.8.158");
    model.clear_wifi_connection();
    wifi.publish();
    assert(!wifi.presentation().details_available && wifi.details().ipv4_address.empty());
    wifi.close_details();
    assert(model.update_wifi_networks("*:CM0 Lab:88:WPA2\n"));

    // Rejecting a second request must not overwrite the first request's UUID.
    assert(setenv("CM0_TEST_HANG_COMMAND", "1", 1) == 0);
    assert(wifi.forget_network(saved));
    assert(!wifi.forget_network(current));
    runner.stop();
    assert(unsetenv("CM0_TEST_HANG_COMMAND") == 0);
    wifi.command_finished({SettingsCommand::wifi_forget, true, {}});
    assert(wifi.known_networks().size() == 1 && wifi.known_networks()[0].uuid == current);

    // Re-read the fixture, then interrupt a partially collected profile batch.
    wifi.refresh();
    settle_wifi_background(wifi, runner);
    assert(wifi.known_networks().size() == 2);
    wifi.request_profiles_refresh();
    assert(wifi.resume_background());
    wifi.command_finished(wait_for_result(runner));
    wifi.command_finished(wait_for_result(runner));
    assert(runner.active_command() == SettingsCommand::wifi_profile);
    assert(wifi.forget_network(current));
    settle_wifi_background(wifi, runner);
    assert(wifi.known_networks().size() == 1 && wifi.known_networks()[0].uuid == saved);
    wifi.stop();
    assert(!wifi.resume_background());
}

void test_ethernet_save_preempts_wifi_background()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    SettingsCommandRunner runner;
    EthernetViewModel ethernet(model, runner);
    assert(runner.start(SettingsCommand::wifi_profile, {"/bin/sleep", "2"}));
    ethernet.publish();
    assert(ethernet.presentation().configurable);
    assert(ethernet.apply_configuration("192.168.8.50", "255.255.255.0", "192.168.8.1", "1.1.1.1"));
    assert(runner.active_command() == SettingsCommand::ethernet_profile_lookup);
    runner.stop();
    assert(runner.start(SettingsCommand::wifi_forget, {"/bin/sleep", "2"}));
    assert(!ethernet.apply_configuration("192.168.8.50", "255.255.255.0", "192.168.8.1", "1.1.1.1"));
    assert(runner.active_command() == SettingsCommand::wifi_forget);
    runner.stop();
}

void test_wifi_commands_and_publication()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(model.update_wifi_networks("*:CM0 Lab:88:WPA2\n:Guest:64:--\n:Workshop:41:WPA2\n"));

    SettingsCommandRunner runner;
    WifiViewModel wifi(model, runner);
    ObserverProbe probe;
    lv_observer_t *observer = lv_subject_add_observer(wifi.revision_subject(), revision_changed, &probe);
    assert(observer);
    wifi.publish();
    assert(wifi.presentation().enabled);
    assert(wifi.presentation().status == "CM0 Lab");
    const int published_calls = probe.calls;

    assert(wifi.activate_network(wifi.networks().size()) == WifiActionResult::ignored);
    assert(wifi.activate_network(wifi_index(wifi, "Workshop")) == WifiActionResult::password_required);
    assert(wifi.pending_ssid() == "Workshop");
    assert(!wifi.connect_pending_network(""));
    wifi.cancel_pending_network();
    assert(wifi.pending_ssid().empty());

    assert(wifi.activate_network(wifi_index(wifi, "Workshop")) == WifiActionResult::password_required);
    assert(setenv("CM0_TEST_HANG_COMMAND", "1", 1) == 0);
    assert(runner.start(SettingsCommand::wifi_scan, {model.environment().nmcli, "device", "wifi", "list"}));
    assert(!wifi.connect_pending_network("test-password"));
    assert(wifi.pending_ssid() == "Workshop");
    wifi.set_enabled(false);
    assert(read_file(fixture.path("rfkill/rfkill0/soft")) == "0\n");
    runner.stop();
    assert(unsetenv("CM0_TEST_HANG_COMMAND") == 0);

    assert(wifi.connect_pending_network("test-password"));
    assert(runner.busy());
    assert(wifi.presentation().status == "Connecting...");
    finish_wifi_command(wifi, runner);
    assert(fixture.command_log().find("--ask --wait 15 device wifi connect Workshop") != std::string::npos);
    assert(fixture.command_log().find("test-password") == std::string::npos);
    assert(fixture.command_log().find(" password ") == std::string::npos);
    assert(read_file(fixture.password_capture_path()) == "test-password");
    assert(wifi.networks().size() == 3);
    assert(probe.calls > published_calls);

    assert(wifi.activate_network(wifi_index(wifi, "Guest")) == WifiActionResult::started);
    finish_wifi_command(wifi, runner);
    assert(fixture.command_log().find("device wifi connect Guest") != std::string::npos);

    assert(wifi.activate_network(wifi_index(wifi, "CM0 Lab")) == WifiActionResult::started);
    finish_wifi_command(wifi, runner);
    assert(fixture.command_log().find("device disconnect wlan0") != std::string::npos);
    assert(model.connected_wifi() == nullptr);

    wifi.set_enabled(false);
    assert(runner.busy());
    finish_wifi_command(wifi, runner);
    assert(!wifi.presentation().enabled);
    assert(fixture.command_log().find("radio wifi off") != std::string::npos);
    lv_observer_remove(observer);
}

void test_bluetooth_commands_and_publication()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(
        model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                          "\tPowered: yes\n"));
    assert(model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:01 Keyboard\tPaired: yes\n"));

    SettingsCommandRunner runner;
    BluetoothViewModel bluetooth(model, runner);
    bluetooth.publish();
    const auto revision = lv_subject_get_int(bluetooth.revision_subject());
    bluetooth.connect(0);
    assert(runner.busy());
    auto result = wait_for_result(runner);
    assert(result.command == SettingsCommand::bluetooth_connect);
    bluetooth.command_finished(result);
    assert(bluetooth.presentation().status == "Connected");
    assert(lv_subject_get_int(bluetooth.revision_subject()) > revision);
    assert(fixture.command_log().find("connect AA:BB:CC:DD:EE:01") != std::string::npos);

    assert(model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:03 Gamepad\tPaired: no\n"));
    bluetooth.connect(0);
    assert(runner.busy());
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::bluetooth_pair);
    bluetooth.command_finished(result);
    assert(bluetooth.presentation().status == "Paired");
    assert(bluetooth.devices().size() == 1);
    assert(bluetooth.devices()[0].paired);
    assert(fixture.command_log().find("pair AA:BB:CC:DD:EE:03") != std::string::npos);

    model.clear_bluetooth_devices();
    bluetooth.scan();
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::bluetooth_scan);
    bluetooth.command_finished(result);
    assert(runner.busy());
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::bluetooth_devices);
    bluetooth.command_finished(result);
    assert(bluetooth.devices().size() == 2);
    assert(bluetooth.devices()[0].name == "Keyboard");
    assert(bluetooth.devices()[1].name == "Studio Speaker");

    bluetooth.command_finished(
        {SettingsCommand::bluetooth_scan, false, "Device AA:BB:CC:DD:EE:03 Should Not Be Published\n"});
    assert(bluetooth.devices().size() == 2);
    assert(bluetooth.presentation().status == "Bluetooth Search Failed");

    bluetooth.command_finished({SettingsCommand::bluetooth_devices, false, "Unable to list devices\n"});
    assert(bluetooth.devices().size() == 2);
    bluetooth.command_finished({SettingsCommand::bluetooth_devices, true, ""});
    assert(bluetooth.devices().empty());
    assert(bluetooth.presentation().status == "No Devices Found");

    bluetooth.set_enabled(false);
    assert(runner.busy());
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::bluetooth_power);
    bluetooth.command_finished(result);
    assert(!bluetooth.presentation().enabled);
    assert(read_file(fixture.path("rfkill/rfkill1/soft")) == "0\n");
    assert(read_file(fixture.bluetooth_power_path()) == "no\n");
}

void test_bluetooth_scan_event_name_survives_devices_refresh()
{
    SettingsFixture fixture;
    fixture.export_environment();
    fixture.write("bluetooth-devices-output", "Device AA:BB:CC:DD:EE:09\n");

    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                             "\tPowered: yes\n"));
    SettingsCommandRunner runner;
    BluetoothViewModel bluetooth(model, runner);
    bluetooth.command_finished(
        {SettingsCommand::bluetooth_scan, true, "[NEW] Device AA:BB:CC:DD:EE:09 Desk Headset\n"});
    const auto result = wait_for_result(runner);
    assert(result.command == SettingsCommand::bluetooth_devices);
    bluetooth.command_finished(result);
    assert(bluetooth.devices().size() == 1);
    assert(bluetooth.devices()[0].name == "Desk Headset");
}

void test_bluetooth_scan_uses_dbus_helper_when_available()
{
    SettingsFixture fixture;
    fixture.write("bluetooth-dbus", "#!/bin/sh\n"
                                  "printf '%s\\n' \"$*\" >> \"$CM0_TEST_COMMAND_LOG\"\n"
                                  "printf 'Device AA:BB:CC:DD:EE:09 D-Bus Headset\\n'\n",
                  0700);
    auto environment                 = fixture.environment();
    environment.bluetooth_dbus_helper = fixture.path("bluetooth-dbus");
    SettingsModel model(environment);
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(model.update_bluetooth_controller("Controller AA:BB:CC:DD:EE:FF LILYGO [default]\n"
                                             "\tPowered: yes\n"));

    SettingsCommandRunner runner;
    BluetoothViewModel bluetooth(model, runner);
    bluetooth.scan();
    auto result = wait_for_result(runner);
    assert(result.command == SettingsCommand::bluetooth_scan);
    bluetooth.command_finished(result);
    result = wait_for_result(runner);
    assert(result.command == SettingsCommand::bluetooth_dbus_devices);
    bluetooth.command_finished(result);
    assert(bluetooth.devices().size() == 1);
    assert(bluetooth.devices()[0].name == "D-Bus Headset");
}

void test_rfkill_fallback_is_a_single_power_path()
{
    SettingsFixture fixture;
    auto environment         = fixture.environment();
    environment.nmcli        = fixture.path("missing-nmcli");
    environment.bluetoothctl = fixture.path("missing-bluetoothctl");
    SettingsModel model(environment);
    model.refresh_connectivity();

    SettingsCommandRunner runner;
    WifiViewModel wifi(model, runner);
    BluetoothViewModel bluetooth(model, runner);
    wifi.set_enabled(false);
    assert(!runner.busy());
    assert(read_file(fixture.path("rfkill/rfkill0/soft")) == "1\n");
    bluetooth.set_enabled(false);
    assert(!runner.busy());
    assert(read_file(fixture.path("rfkill/rfkill1/soft")) == "1\n");
}

void test_settings_view_model_dispatches_results()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsViewModel view_model(fixture.environment());
    view_model.refresh();

    const auto before = lv_subject_get_int(view_model.wifi().revision_subject());
    view_model.wifi().scan();
    drain_commands(view_model);
    assert(view_model.wifi().networks().size() == 4);
    assert(view_model.wifi().networks().front().ssid == "CM0 Lab");
    assert(lv_subject_get_int(view_model.wifi().revision_subject()) > before);
    assert(view_model.home().presentation().battery == "82%");
    assert(view_model.home().presentation().bluetooth == "On");
}

void test_bluetooth_sync_honors_deferred_scan_and_rfkill_fallback()
{
    {
        SettingsFixture fixture;
        fixture.export_environment();
        SettingsViewModel view_model(fixture.environment());
        view_model.refresh();
        assert(view_model.busy());
        view_model.bluetooth().scan();
        drain_commands(view_model);
        assert(view_model.bluetooth().devices().size() == 2);
        assert(view_model.bluetooth().presentation().status == "Devices Updated");
        const auto log        = fixture.command_log();
        const auto first_show = log.find("\nshow\n");
        assert(first_show != std::string::npos);
        assert(log.find("\nshow\n", first_show + 1U) == std::string::npos);
        assert(log.find("scan on") != std::string::npos);
        assert(log.find("\ndevices\n") != std::string::npos);
    }

    {
        SettingsFixture fixture;
        fixture.export_environment();
        assert(setenv("CM0_TEST_BLUETOOTH_SHOW_FAIL", "1", 1) == 0);
        SettingsViewModel view_model(fixture.environment());
        view_model.refresh();
        drain_commands(view_model);
        assert(view_model.bluetooth().presentation().available);
        assert(view_model.bluetooth().presentation().enabled);
        assert(view_model.bluetooth().presentation().control_enabled);

        view_model.bluetooth().set_enabled(false);
        assert(!view_model.busy());
        assert(read_file(fixture.path("rfkill/rfkill1/soft")) == "1\n");
        assert(fixture.command_log().find("power off") == std::string::npos);
        assert(unsetenv("CM0_TEST_BLUETOOTH_SHOW_FAIL") == 0);
    }

    {
        SettingsFixture fixture;
        fixture.export_environment();
        SettingsViewModel view_model(fixture.environment());
        view_model.refresh();
        drain_commands(view_model);
        view_model.bluetooth().scan();
        drain_commands(view_model);
        assert(view_model.bluetooth().devices().size() == 2);

        assert(setenv("CM0_TEST_BLUETOOTH_SHOW_TRANSIENT_FAIL", "1", 1) == 0);
        view_model.refresh();
        drain_commands(view_model);
        assert(view_model.bluetooth().presentation().available);
        assert(view_model.bluetooth().presentation().enabled);
        assert(!view_model.bluetooth().presentation().control_enabled);
        assert(view_model.bluetooth().devices().size() == 2);
        view_model.bluetooth().set_enabled(false);
        assert(!view_model.busy());
        assert(read_file(fixture.path("rfkill/rfkill1/soft")) == "0\n");
        assert(fixture.command_log().find("power off") == std::string::npos);

        assert(unsetenv("CM0_TEST_BLUETOOTH_SHOW_TRANSIENT_FAIL") == 0);
        view_model.refresh();
        drain_commands(view_model);
        assert(view_model.bluetooth().presentation().control_enabled);
    }
}

void test_bluetooth_scan_resolves_missing_device_names()
{
    SettingsFixture fixture;
    fixture.export_environment();
    fixture.write("bluetooth-devices-output", "Device AA:BB:CC:DD:EE:01\n"
                                          "Device AA:BB:CC:DD:EE:02\n");

    SettingsViewModel view_model(fixture.environment());
    view_model.refresh();
    drain_commands(view_model);
    view_model.bluetooth().scan();
    drain_commands(view_model);

    assert(view_model.bluetooth().devices().size() == 2);
    assert(view_model.bluetooth().devices()[0].name == "Keyboard");
    assert(view_model.bluetooth().devices()[1].name == "Studio Speaker");
    const auto log = fixture.command_log();
    assert(log.find("info AA:BB:CC:DD:EE:01") != std::string::npos);
    assert(log.find("info AA:BB:CC:DD:EE:02") != std::string::npos);
}

void test_periodic_quiet_sync_tracks_external_changes()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsViewModel view_model(fixture.environment());
    view_model.refresh();
    drain_commands(view_model);
    assert(view_model.home().presentation().wifi == "CM0 Lab");
    const auto initial_generation = view_model.wifi().presentation().list_generation;

    fixture.write("wifi-output", "*:Roamed Network:76:WPA2\n:Guest:60:--\n");
    view_model.refresh();
    drain_commands(view_model);
    assert(view_model.home().presentation().wifi == "Roamed Network");
    assert(view_model.wifi().presentation().status == "Roamed Network");
    assert(view_model.wifi().presentation().list_generation > initial_generation);

    const auto stable_generation = view_model.wifi().presentation().list_generation;
    view_model.refresh();
    drain_commands(view_model);
    assert(view_model.wifi().presentation().list_generation == stable_generation);

    fixture.write("net/wlan0/carrier", "0\n");
    fixture.write("net/wlan0/operstate", "down\n");
    fixture.write("wifi-output", ":Roamed Network:76:WPA2\n:Guest:60:--\n");
    fixture.write("bluetooth-powered", "no\n");
    view_model.refresh();
    drain_commands(view_model);
    assert(view_model.home().presentation().wifi == "Not Connected");
    assert(view_model.home().presentation().bluetooth == "Off");
    assert(view_model.wifi().presentation().status == "Not Connected");
}

void test_wifi_scan_refreshes_carrier_and_preserves_cache_on_failure()
{
    SettingsFixture fixture;
    fixture.export_environment();
    fixture.write("net/wlan0/carrier", "0\n");
    fixture.write("net/wlan0/operstate", "down\n");
    fixture.write("wifi-output", "*:Recovered Network:77:WPA2\n");

    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(model.update_wifi_networks(":Previous Network:52:WPA2\n"));
    SettingsCommandRunner runner;
    WifiViewModel wifi(model, runner);

    wifi.scan();
    fixture.write("net/wlan0/carrier", "1\n");
    fixture.write("net/wlan0/operstate", "up\n");
    auto result = wait_for_result(runner);
    wifi.command_finished(result);
    assert(wifi.networks().size() == 1);
    assert(wifi.networks().front().ssid == "Recovered Network");
    assert(wifi.networks().front().connected);

    wifi.command_finished({SettingsCommand::wifi_scan, false, "temporary scan failure\n"});
    assert(wifi.networks().size() == 1);
    assert(wifi.networks().front().ssid == "Recovered Network");
    wifi.command_finished({SettingsCommand::wifi_scan, true, "\n"});
    assert(wifi.networks().empty());
    assert(wifi.presentation().status == "No Networks Found");
}

void test_pending_refresh_does_not_starve_bluetooth_sync()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsViewModel view_model(fixture.environment());
    view_model.refresh();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::string log;
    while (std::chrono::steady_clock::now() < deadline) {
        log = fixture.command_log();
        if (view_model.busy() && log.find("device wifi list --rescan no") != std::string::npos) break;
        view_model.poll_command();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(view_model.busy());
    assert(log.find("device wifi list --rescan no") != std::string::npos);

    view_model.refresh();
    drain_commands(view_model);
    log                     = fixture.command_log();
    const auto first_radio  = log.find("radio wifi");
    const auto first_wifi   = log.find("device wifi list --rescan no", first_radio);
    const auto bluetooth    = log.find("\nshow\n", first_wifi);
    const auto second_radio = log.find("radio wifi", first_radio + 1U);
    assert(first_radio != std::string::npos);
    assert(first_wifi != std::string::npos);
    assert(bluetooth != std::string::npos);
    assert(second_radio != std::string::npos);
    assert(bluetooth < second_radio);
}

void test_network_manager_radio_sync_without_rfkill()
{
    SettingsFixture fixture;
    fixture.export_environment();
    fixture.write("wifi-radio", "disabled\n");
    auto environment        = fixture.environment();
    environment.rfkill_root = fixture.path("missing-rfkill");
    SettingsViewModel view_model(environment);

    view_model.refresh();
    drain_commands(view_model);
    assert(!view_model.wifi().presentation().enabled);
    assert(fixture.command_log().find("radio wifi") != std::string::npos);

    view_model.wifi().set_enabled(true);
    drain_commands(view_model);
    assert(view_model.wifi().presentation().enabled);
    assert(read_file(fixture.wifi_radio_path()) == "enabled\n");

    fixture.write("wifi-radio", "disabled\n");
    view_model.refresh();
    drain_commands(view_model);
    assert(!view_model.wifi().presentation().enabled);
}

void test_initial_wifi_radio_failure_falls_back_and_syncs_networks()
{
    SettingsFixture fixture;
    fixture.export_environment();
    assert(setenv("CM0_TEST_WIFI_RADIO_FAIL", "1", 1) == 0);
    SettingsViewModel view_model(fixture.environment());

    view_model.refresh();
    assert(!view_model.wifi().presentation().enabled);
    drain_commands(view_model);
    assert(view_model.wifi().presentation().available);
    assert(view_model.wifi().presentation().enabled);
    assert(view_model.wifi().presentation().status == "CM0 Lab");
    assert(view_model.home().presentation().wifi == "CM0 Lab");
    const auto log = fixture.command_log();
    assert(log.find("radio wifi") != std::string::npos);
    assert(log.find("device wifi list --rescan no") != std::string::npos);
    assert(unsetenv("CM0_TEST_WIFI_RADIO_FAIL") == 0);
}

void test_deferred_bluetooth_scan_resumes_after_wifi_sync()
{
    SettingsFixture fixture;
    fixture.export_environment();
    fixture.write("bluetooth-powered", "no\n");
    SettingsViewModel view_model(fixture.environment());
    view_model.refresh();
    drain_commands(view_model);
    assert(!view_model.bluetooth().presentation().enabled);

    fixture.write("commands.log", "");
    fixture.write("bluetooth-powered", "yes\n");
    assert(view_model.wifi().sync());
    view_model.bluetooth().scan();
    assert(view_model.bluetooth().presentation().status == "Preparing Bluetooth...");
    drain_commands(view_model);

    assert(view_model.bluetooth().presentation().enabled);
    assert(view_model.bluetooth().devices().size() == 2);
    const auto log       = fixture.command_log();
    const auto wifi_sync = log.find("device wifi list --rescan no");
    const auto bt_sync   = log.find("\nshow\n", wifi_sync);
    const auto scan      = log.find("scan on", bt_sync);
    const auto devices   = log.find("\ndevices\n", scan);
    assert(wifi_sync != std::string::npos);
    assert(bt_sync != std::string::npos);
    assert(scan != std::string::npos);
    assert(devices != std::string::npos);
    assert(wifi_sync < bt_sync && bt_sync < scan && scan < devices);
}

void test_stop_cancels_deferred_bluetooth_scan()
{
    SettingsFixture fixture;
    fixture.export_environment();
    fixture.write("bluetooth-powered", "no\n");
    SettingsViewModel view_model(fixture.environment());
    view_model.refresh();
    drain_commands(view_model);

    assert(view_model.wifi().sync());
    view_model.bluetooth().scan();
    assert(view_model.bluetooth().presentation().status == "Preparing Bluetooth...");
    view_model.stop();
    fixture.write("commands.log", "");
    fixture.write("bluetooth-powered", "yes\n");
    view_model.refresh();
    drain_commands(view_model);

    assert(view_model.bluetooth().presentation().enabled);
    assert(fixture.command_log().find("scan on") == std::string::npos);
}

void test_disconnect_requires_unique_connected_interface()
{
    SettingsFixture fixture;
    fixture.export_environment();
    fixture.write("net/wlan1/wireless/.keep", "");
    fixture.write("net/wlan1/operstate", "up\n");
    fixture.write("net/wlan1/carrier", "1\n");
    SettingsModel model(fixture.environment());
    model.refresh_connectivity();
    assert(model.update_network_manager_wifi_radio("enabled\n"));
    assert(model.update_wifi_networks("*:CM0 Lab:88:WPA2\n"));
    SettingsCommandRunner runner;
    WifiViewModel wifi(model, runner);

    assert(wifi.activate_network(0) == WifiActionResult::ignored);
    assert(!runner.busy());
    assert(wifi.presentation().status == "Cannot Identify Wi-Fi Interface");
}

void test_command_timeout_returns_failure()
{
    SettingsFixture fixture;
    fixture.export_environment();
    assert(setenv("CM0_TEST_HANG_COMMAND", "1", 1) == 0);
    SettingsCommandRunner runner;
    assert(runner.start(SettingsCommand::wifi_scan, {fixture.environment().nmcli, "device", "wifi", "list"}, {},
                        std::chrono::milliseconds(25)));
    const auto result = wait_for_result(runner);
    assert(result.command == SettingsCommand::wifi_scan);
    assert(!result.success);
    assert(result.output == "Command timed out");
    assert(!runner.busy());
    assert(unsetenv("CM0_TEST_HANG_COMMAND") == 0);
}

void test_sharing_commands_and_failures()
{
    SettingsFixture fixture;
    fixture.export_environment();
    fixture.write("ssh-requires-authorization", "");
    SettingsViewModel view_model(fixture.environment());
    auto &sharing = view_model.sharing();
    ObserverProbe probe;
    auto *observer = lv_subject_add_observer(sharing.revision_subject(), revision_changed, &probe);
    view_model.refresh();
    drain_commands(view_model);
    assert(!sharing.presentation().enabled && sharing.presentation().control_enabled);
    const auto before = probe.calls;
    sharing.set_enabled(true);
    assert(sharing.busy() && !sharing.presentation().control_enabled);
    assert(!sharing.presentation().enabled); // Do not claim success before observing it.
    sharing.set_enabled(false); // Ignore a second change while the command is running.
    drain_commands(view_model);
    assert(sharing.presentation().enabled && sharing.presentation().control_enabled);
    assert(sharing.presentation().status == "Remote Login: On");
    assert(probe.calls > before);
    assert(fixture.command_log().find("enable --now ssh.service") != std::string::npos);
    assert(fixture.command_log().find("sudo -n -- " + fixture.path("systemctl")) != std::string::npos);

    // An absent package policy must fail promptly without changing state or prompting for a password.
    fixture.write("ssh-sudo-denied", "");
    sharing.set_enabled(false);
    drain_commands(view_model);
    assert(sharing.presentation().enabled && sharing.presentation().control_enabled);
    assert(sharing.presentation().status == "Administrator Permission Is Required to Change Remote Login.");
    sharing.refresh();
    drain_commands(view_model);
    assert(sharing.presentation().status == "Administrator Permission Is Required to Change Remote Login.");
    std::filesystem::remove(fixture.path("ssh-sudo-denied"));

    fixture.write("ssh-denied", "");
    sharing.set_enabled(false);
    drain_commands(view_model);
    assert(sharing.presentation().enabled && sharing.presentation().control_enabled);
    assert(sharing.presentation().status == "Administrator Permission Is Required to Change Remote Login.");
    std::filesystem::remove(fixture.path("ssh-denied"));

    fixture.write("ssh-no-new-privileges", "");
    sharing.set_enabled(false);
    drain_commands(view_model);
    assert(sharing.presentation().enabled && sharing.presentation().control_enabled);
    assert(sharing.presentation().status == "Administrator Permission Is Required to Change Remote Login.");
    std::filesystem::remove(fixture.path("ssh-no-new-privileges"));

    // Disabling startup alone must not count as closing a still-running SSH listener.
    fixture.write("ssh-stop-fail", "");
    sharing.set_enabled(false);
    drain_commands(view_model);
    assert(sharing.presentation().enabled && sharing.presentation().control_enabled);
    assert(sharing.presentation().status == "Cannot Change Remote Login. See Application Log.");
    std::filesystem::remove(fixture.path("ssh-stop-fail"));

    fixture.write("ssh-service", "inactive\n");
    fixture.write("ssh-socket", "active\n");
    sharing.refresh();
    drain_commands(view_model);
    assert(sharing.presentation().enabled); // Listening socket alone enables remote login.
    fixture.write("ssh-partial-fail", "");
    sharing.set_enabled(false);
    drain_commands(view_model);
    assert(!sharing.presentation().enabled); // A failed command can still have changed the service.
    assert(sharing.presentation().status == "Remote Login: Off");
    assert(fixture.command_log().find("disable --now ssh.socket ssh.service") != std::string::npos);
    std::filesystem::remove(fixture.path("ssh-partial-fail"));

    fixture.write("ssh-noop", "");
    sharing.set_enabled(true);
    drain_commands(view_model);
    assert(!sharing.presentation().enabled);
    assert(sharing.presentation().status == "Remote Login Change Was Not Fully Applied");
    std::filesystem::remove(fixture.path("ssh-noop"));

    fixture.write("ssh-show-fail", "");
    sharing.refresh();
    drain_commands(view_model);
    assert(!sharing.presentation().control_enabled);
    sharing.set_enabled(true);
    assert(!sharing.busy());
    std::filesystem::remove(fixture.path("ssh-show-fail"));
    fixture.write("ssh-missing", "");
    sharing.refresh();
    drain_commands(view_model);
    assert(!sharing.presentation().control_enabled);
    assert(sharing.presentation().status == "SSH Server Is Not Installed");
    std::filesystem::remove(fixture.path("ssh-missing"));
    fixture.write("ssh-masked", "");
    sharing.refresh();
    drain_commands(view_model);
    assert(!sharing.presentation().control_enabled);
    assert(sharing.presentation().status == "SSH Is Disabled by System Policy");
    std::filesystem::remove(fixture.path("ssh-masked"));
    view_model.refresh();
    drain_commands(view_model);
    assert(sharing.presentation().control_enabled);
    sharing.set_enabled(true);
    view_model.stop();
    assert(!view_model.busy());
    sharing.refresh();
    drain_commands(view_model);
    assert(sharing.presentation().control_enabled);
    lv_observer_remove(observer);

    SharingViewModel missing({fixture.path("missing-systemctl")});
    missing.refresh();
    while (missing.busy()) {
        missing.poll_command();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(!missing.presentation().control_enabled);
    assert(missing.presentation().status.find("Cannot Check") != std::string::npos);
}

void test_serial_console_save_and_reboot_choices()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SharingViewModel sharing;
    const auto drain = [](SharingViewModel &vm) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (vm.busy() && std::chrono::steady_clock::now() < deadline) {
            vm.poll_command();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(!vm.busy());
    };
    sharing.refresh();
    drain(sharing);
    assert(sharing.presentation().serial_control_enabled && !sharing.presentation().serial_enabled);
    sharing.reboot_now(); // No prompt, so no permission to reboot.
    assert(!sharing.busy());
    fixture.write("serial-sudo-denied", "");
    sharing.set_serial_enabled(true);
    drain(sharing);
    assert(!sharing.presentation().serial_enabled && sharing.presentation().serial_control_enabled);
    assert(!sharing.presentation().show_reboot_prompt);
    assert(sharing.presentation().serial_status.find("Permissions") != std::string::npos);
    assert(!std::filesystem::exists(fixture.path("reboot-requested")));
    std::filesystem::remove(fixture.path("serial-sudo-denied"));
    sharing.set_serial_enabled(true);
    assert(!sharing.presentation().serial_control_enabled && !sharing.presentation().show_reboot_prompt);
    sharing.set_serial_enabled(false); // No overlapping writes.
    drain(sharing);
    assert(sharing.presentation().serial_enabled && sharing.presentation().show_reboot_prompt);
    assert(sharing.presentation().reboot_required);
    assert(!sharing.presentation().control_enabled);
    assert(fixture.command_log().find("nonint do_serial_cons 0") != std::string::npos);
    assert(fixture.command_log().find("sudo -n -- " + fixture.path("raspi-config") +
                                      " nonint do_serial_cons 0") != std::string::npos);
    assert(fixture.command_log().find("reboot") == std::string::npos);
    sharing.refresh();
    drain(sharing);
    assert(sharing.presentation().show_reboot_prompt);
    sharing.reboot_later();
    assert(!sharing.presentation().show_reboot_prompt && sharing.presentation().reboot_required);
    assert(sharing.presentation().serial_enabled);
    assert(fixture.command_log().find("reboot") == std::string::npos);
    sharing.stop();
    sharing.refresh();
    drain(sharing);
    assert(sharing.presentation().reboot_required && !sharing.presentation().show_reboot_prompt);

    fixture.write("serial-denied", "");
    sharing.set_serial_enabled(false);
    drain(sharing);
    assert(sharing.presentation().serial_enabled && !sharing.presentation().show_reboot_prompt);
    assert(sharing.presentation().serial_status.find("Permissions") != std::string::npos);
    std::filesystem::remove(fixture.path("serial-denied"));
    fixture.write("serial-noop", "");
    sharing.set_serial_enabled(false);
    drain(sharing);
    assert(sharing.presentation().serial_status == "Serial Console Change Was Not Applied");
    assert(!sharing.presentation().show_reboot_prompt);
    std::filesystem::remove(fixture.path("serial-noop"));
    fixture.write("serial-partial-fail", "");
    sharing.set_serial_enabled(false);
    drain(sharing);
    assert(!sharing.presentation().serial_enabled && !sharing.presentation().show_reboot_prompt);
    assert(sharing.presentation().serial_status.find("Cannot Save") != std::string::npos);
    std::filesystem::remove(fixture.path("serial-partial-fail"));

    sharing.set_serial_enabled(true);
    drain(sharing);
    sharing.reboot_later();
    fixture.write("running-cmdline", "console=serial0,115200 root=test\n");
    sharing.refresh();
    drain(sharing);
    assert(!sharing.presentation().reboot_required);
    sharing.set_serial_enabled(false);
    drain(sharing);
    assert(sharing.presentation().show_reboot_prompt && !sharing.presentation().serial_enabled);
    assert(sharing.presentation().reboot_required);
    fixture.write("reboot-sudo-denied", "");
    sharing.reboot_now();
    sharing.reboot_now();
    drain(sharing);
    assert(sharing.presentation().show_reboot_prompt && !sharing.presentation().reboot_busy);
    assert(!sharing.presentation().reboot_error.empty());
    assert(!std::filesystem::exists(fixture.path("reboot-requested")));
    const auto sudo_reboot = "sudo -n -- " + fixture.path("systemctl") + " --no-ask-password reboot\n";
    const auto denied_log = fixture.command_log();
    assert(denied_log.find(sudo_reboot) != std::string::npos);
    assert(denied_log.find(sudo_reboot) == denied_log.rfind(sudo_reboot));
    std::filesystem::remove(fixture.path("reboot-sudo-denied"));
    fixture.write("reboot-denied", "");
    sharing.reboot_now();
    assert(sharing.presentation().reboot_busy);
    sharing.reboot_now(); // Repeated clicks must not send multiple requests.
    drain(sharing);
    assert(sharing.presentation().show_reboot_prompt && !sharing.presentation().reboot_busy);
    assert(!sharing.presentation().reboot_error.empty());
    assert(!std::filesystem::exists(fixture.path("reboot-requested")));
    auto log = fixture.command_log();
    const std::string reboot_command = "\n--no-ask-password reboot\n";
    assert(log.find(reboot_command) != std::string::npos);
    assert(log.find(reboot_command) == log.rfind(reboot_command));
    sharing.reboot_later();
    assert(!sharing.presentation().show_reboot_prompt && sharing.presentation().reboot_required);
    std::filesystem::remove(fixture.path("reboot-denied"));

    fixture.write("serial-query-fail", "");
    sharing.refresh();
    drain(sharing);
    assert(!sharing.presentation().serial_control_enabled);
    assert(!sharing.presentation().show_reboot_prompt);
    std::filesystem::remove(fixture.path("serial-query-fail"));
    sharing.refresh();
    drain(sharing);
    sharing.set_serial_enabled(true);
    drain(sharing);
    sharing.reboot_now();
    drain(sharing);
    assert(std::filesystem::exists(fixture.path("reboot-requested")));
    assert(!sharing.presentation().show_reboot_prompt && sharing.presentation().reboot_busy);
    assert(!sharing.presentation().serial_control_enabled && !sharing.presentation().control_enabled);
    sharing.reboot_now();
    assert(!sharing.busy());

    SharingViewModel unavailable({fixture.path("systemctl")},
        {fixture.path("missing-raspi-config"), fixture.path("systemctl"), fixture.path("running-cmdline")});
    unavailable.refresh();
    drain(unavailable);
    assert(!unavailable.presentation().serial_control_enabled);
    assert(unavailable.presentation().serial_status.find("Unavailable") != std::string::npos);
}

void test_sharing_refresh_preserves_controls_and_accepts_actions()
{
    SettingsFixture fixture;
    fixture.export_environment();
    SettingsViewModel settings(fixture.environment());
    auto &sharing = settings.sharing();
    settings.refresh();
    assert(!sharing.presentation().control_enabled);
    assert(!sharing.presentation().serial_control_enabled);
    drain_commands(settings);

    sharing.refresh();
    assert(sharing.busy());
    assert(sharing.presentation().control_enabled);
    assert(sharing.presentation().serial_control_enabled);
    drain_commands(settings);
    assert(sharing.presentation().control_enabled && sharing.presentation().serial_control_enabled);

    // Changes made outside Settings still appear after a quiet refresh.
    fixture.write("ssh-service", "active\n");
    fixture.write("ssh-boot", "enabled\n");
    fixture.write("serial-console", "0\n");
    sharing.refresh();
    assert(sharing.presentation().control_enabled && sharing.presentation().serial_control_enabled);
    drain_commands(settings);
    assert(sharing.presentation().enabled && sharing.presentation().serial_enabled);
    assert(!sharing.presentation().show_reboot_prompt);

    // A focused, enabled switch must accept input even while its read is pending.
    sharing.refresh();
    sharing.set_enabled(false);
    sharing.set_serial_enabled(false);
    assert(!sharing.presentation().control_enabled && !sharing.presentation().serial_control_enabled);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (settings.busy() && std::chrono::steady_clock::now() < deadline) {
        if (sharing.presentation().status == "Turning Off Remote Login...")
            assert(!sharing.presentation().control_enabled);
        if (sharing.presentation().serial_status == "Saving Serial Console...")
            assert(!sharing.presentation().serial_control_enabled);
        settings.poll_command();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(!settings.busy());
    assert(!sharing.presentation().enabled && !sharing.presentation().serial_enabled);
    assert(sharing.presentation().show_reboot_prompt);
    assert(fixture.command_log().find("disable --now ssh.service") != std::string::npos);
    assert(fixture.command_log().find("nonint do_serial_cons 1") != std::string::npos);
    sharing.reboot_later();
    assert(sharing.presentation().control_enabled && sharing.presentation().serial_control_enabled);
}

}  // namespace

int main()
{
    lv_init();
    {
        test_passive_presentations_and_subjects();
        test_ethernet_manual_configuration_and_validation();
        test_ethernet_manual_configuration_reports_apply_failure();
        test_wifi_commands_and_publication();
        test_known_wifi_details_and_forget_races();
        test_ethernet_save_preempts_wifi_background();
        test_bluetooth_commands_and_publication();
        test_bluetooth_scan_event_name_survives_devices_refresh();
        test_bluetooth_scan_uses_dbus_helper_when_available();
        test_rfkill_fallback_is_a_single_power_path();
        test_settings_view_model_dispatches_results();
        test_bluetooth_sync_honors_deferred_scan_and_rfkill_fallback();
        test_bluetooth_scan_resolves_missing_device_names();
        test_periodic_quiet_sync_tracks_external_changes();
        test_wifi_scan_refreshes_carrier_and_preserves_cache_on_failure();
        test_pending_refresh_does_not_starve_bluetooth_sync();
        test_network_manager_radio_sync_without_rfkill();
        test_initial_wifi_radio_failure_falls_back_and_syncs_networks();
        test_deferred_bluetooth_scan_resumes_after_wifi_sync();
        test_stop_cancels_deferred_bluetooth_scan();
        test_disconnect_requires_unique_connected_interface();
        test_command_timeout_returns_failure();
        test_sharing_commands_and_failures();
        test_serial_console_save_and_reboot_choices();
        test_sharing_refresh_preserves_controls_and_accepts_actions();
    }
    lv_deinit();
    return 0;
}
