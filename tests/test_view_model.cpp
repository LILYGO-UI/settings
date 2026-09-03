#include "pages/settings/settings_view_model.hpp"
#include "settings_fixture.hpp"

#include <lvgl.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
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
    assert(model.update_bluetooth_devices("Device AA:BB:CC:DD:EE:01 Keyboard\n"));

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

}  // namespace

int main()
{
    lv_init();
    {
        test_passive_presentations_and_subjects();
        test_wifi_commands_and_publication();
        test_bluetooth_commands_and_publication();
        test_rfkill_fallback_is_a_single_power_path();
        test_settings_view_model_dispatches_results();
        test_bluetooth_sync_honors_deferred_scan_and_rfkill_fallback();
        test_periodic_quiet_sync_tracks_external_changes();
        test_wifi_scan_refreshes_carrier_and_preserves_cache_on_failure();
        test_pending_refresh_does_not_starve_bluetooth_sync();
        test_network_manager_radio_sync_without_rfkill();
        test_initial_wifi_radio_failure_falls_back_and_syncs_networks();
        test_deferred_bluetooth_scan_resumes_after_wifi_sync();
        test_stop_cancels_deferred_bluetooth_scan();
        test_disconnect_requires_unique_connected_interface();
        test_command_timeout_returns_failure();
    }
    lv_deinit();
    return 0;
}
