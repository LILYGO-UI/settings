#include "pages/cellular/cellular_view_model.hpp"
#include "pages/cellular/cellular_serial.hpp"
#include "cellular_pty_fixture.hpp"

#include <lvgl.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using lilygo::settings::CellularEnvironment;
using lilygo::settings::CellularSerial;
using lilygo::settings::CellularViewModel;
using ::test_support::CellularPtyFixture;
using ::test_support::CellularPtyReply;
using namespace std::chrono_literals;

std::string ok(const std::string &text = {}) { return "\r\n" + text + "\r\nOK\r\n"; }

// Configuration changes happen only between completed transactions. The
// atomic flags coordinate the deliberately stalled background-query tests.
struct Modem {
    bool enabled = true;
    bool deny_change = false;
    bool ignore_change = false;
    bool error_after_change = false;
    std::string capabilities = "+CFUN: (0,1,4,20,21,30,31,40,41,44)";
    std::string system_mode = "%XSYSTEMMODE: 1,0,1,0";
    std::string sim_reply = ok("+CPIN: READY");
    std::string iccid = "89860012345678901234";
    std::string statistics = "%XCONNSTAT: 0,0,524,1048,708,650";
    std::map<std::string, std::string> overrides;
    std::chrono::milliseconds statistics_delay{0};
    std::chrono::milliseconds radio_delay{0};
    std::atomic_bool delayed_radio_seen{false};
    std::atomic_bool swallow_radio{false};
    std::atomic_bool radio_swallowed{false};
    CellularPtyFixture port;

    Modem()
    {
        port.set_reply_handler([this](const std::string &command) {
            if (command == "AT+CFUN?" && swallow_radio.exchange(false)) {
                radio_swallowed.store(true);
                return CellularPtyReply{};
            }
            const auto found = overrides.find(command);
            const auto text = found == overrides.end() ? reply(command) : found->second;
            if (command == "AT+CFUN?" && radio_delay != 0ms) delayed_radio_seen.store(true);
            const auto delay = command == "AT%XCONNSTAT?" ? statistics_delay :
                               command == "AT+CFUN?" ? radio_delay : 0ms;
            return CellularPtyReply{text, delay};
        });
    }

    [[nodiscard]] CellularEnvironment environment() const { return {port.device_path()}; }

    std::string reply(const std::string &command)
    {
        if (command == "AT") return ok();
        if (command == "AT+CFUN?") return ok(enabled ? "+CFUN: 1" : "+CFUN: 4");
        if (command == "AT+CFUN=?") return ok(capabilities);
        if (command == "AT%XSYSTEMMODE?") return ok(system_mode);
        if (command == "AT+CFUN=20" || command == "AT+CFUN=21") {
            if (deny_change) return "\r\n+CME ERROR: 3\r\n";
            if (!ignore_change) enabled = command == "AT+CFUN=21";
            return error_after_change ? "\r\nERROR\r\n" : ok();
        }
        if (command == "AT+CPIN?") return sim_reply;
        if (command == "AT%XICCID") return ok("%XICCID: " + iccid);
        if (command == "AT+CIMI") return ok("460001234567890");
        if (command == "AT+CEREG?") return ok(enabled ? "+CEREG: 0,1" : "+CEREG: 0,0");
        if (command == "AT+COPS?") return ok("+COPS: 0,0,\"China Mobile\",7");
        if (command == "AT+CESQ") return ok("+CESQ: 99,99,255,255,15,57");
        if (command == "AT%XCONNSTAT?") return ok(statistics);
        return "\r\nERROR\r\n";
    }
};

void drain(CellularViewModel &view_model)
{
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    while (view_model.busy() && std::chrono::steady_clock::now() < deadline) {
        view_model.poll_command();
        std::this_thread::sleep_for(2ms);
    }
    assert(!view_model.busy());
}

void refresh(CellularViewModel &view_model)
{
    view_model.refresh();
    drain(view_model);
}

void wait_for_flag(const std::atomic_bool &flag)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!flag.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(2ms);
    assert(flag.load());
}

std::size_t count(const Modem &modem, const std::string &command)
{
    const auto commands = modem.port.command_log();
    return static_cast<std::size_t>(std::count(commands.begin(), commands.end(), command));
}

void test_readback_rejection_unapplied_change_and_applied_error()
{
    Modem modem;
    CellularViewModel view_model(modem.environment());
    assert(!view_model.presentation().control_enabled);
    refresh(view_model);
    assert(view_model.presentation().enabled && view_model.presentation().control_enabled);
    assert(view_model.presentation().status == "Cellular: Registered");
    assert(view_model.presentation().sim_status == "Ready");
    assert(view_model.presentation().interface_name == modem.port.device_path());
    assert(view_model.presentation().signal == "-84 dBm RSRP");
    assert(view_model.presentation().access_technology == "LTE-M");
    assert(view_model.presentation().received == "1.0 MB");
    assert(view_model.presentation().transmitted == "524.0 kB");
    assert(view_model.presentation().total == "1.6 MB");
    view_model.set_enabled(false);
    assert(view_model.presentation().enabled && !view_model.presentation().control_enabled);
    view_model.set_enabled(true); // A second input cannot race the active write.
    drain(view_model);
    assert(!view_model.presentation().enabled && view_model.presentation().control_enabled);
    assert(view_model.presentation().status == "Cellular: Off");
    assert(count(modem, "AT+CFUN=20") == 1 && count(modem, "AT+CFUN=21") == 0);
    const auto commands = modem.port.command_log();
    const auto changed = std::find(commands.begin(), commands.end(), "AT+CFUN=20");
    assert(changed != commands.end());
    assert(std::find(changed + 1, commands.end(), "AT+CFUN?") != commands.end());

    modem.deny_change = true;
    view_model.set_enabled(true);
    drain(view_model);
    assert(!view_model.presentation().enabled && view_model.presentation().control_enabled);
    assert(view_model.presentation().status == "Modem Rejected Cellular Change");
    refresh(view_model);
    assert(view_model.presentation().status == "Modem Rejected Cellular Change");
    modem.deny_change = false;
    modem.ignore_change = true;
    view_model.set_enabled(true);
    drain(view_model);
    assert(!view_model.presentation().enabled);
    assert(view_model.presentation().status == "Cellular Change Was Not Applied");
    modem.ignore_change = false;
    modem.error_after_change = true;
    view_model.set_enabled(true);
    drain(view_model);
    assert(view_model.presentation().enabled && view_model.presentation().status == "Cellular: Registered");
    // LTE-only CFUN commands preserve the GNSS subsystem and do not dial a PDP context.
    for (const auto &command : modem.port.command_log()) {
        assert(command != "AT+CFUN=0" && command != "AT+CFUN=1" && command != "AT+CFUN=4");
        assert(command.find("AT+CGACT=") == std::string::npos && command.find("AT+CPIN=") == std::string::npos);
        assert(command.find("AT%XCONNSTAT=") == std::string::npos);
    }
}

void test_atomic_snapshot_background_cancellation_and_reopen()
{
    Modem modem;
    CellularViewModel view_model(modem.environment());
    refresh(view_model);
    const auto revision = lv_subject_get_int(view_model.revision_subject());
    const auto previous_iccid = view_model.presentation().iccid;
    modem.iccid = "89860099999999999999";
    modem.statistics = "%XCONNSTAT: 0,0,800,1200,0,0";
    modem.statistics_delay = 60ms;
    view_model.refresh();
    assert(view_model.busy() && view_model.presentation().control_enabled);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (view_model.busy() && std::chrono::steady_clock::now() < deadline) {
        view_model.poll_command();
        if (view_model.busy()) {
            assert(view_model.presentation().iccid == previous_iccid);
            assert(view_model.presentation().total == "1.6 MB");
            assert(lv_subject_get_int(view_model.revision_subject()) == revision);
        }
        std::this_thread::sleep_for(2ms);
    }
    assert(!view_model.busy());
    assert(view_model.presentation().iccid == modem.iccid && view_model.presentation().total == "2.0 MB");
    assert(lv_subject_get_int(view_model.revision_subject()) == revision + 1);
    modem.statistics_delay = 0ms;

    // Queue user input behind an outstanding response. Its late OK must not be
    // mistaken for the acknowledgement of the subsequent CFUN write.
    modem.radio_delay = 150ms;
    view_model.refresh();
    wait_for_flag(modem.delayed_radio_seen);
    const auto writes_before = count(modem, "AT+CFUN=20");
    const auto started = std::chrono::steady_clock::now();
    view_model.set_enabled(false);
    assert(std::chrono::steady_clock::now() - started < 100ms);
    assert(!view_model.presentation().control_enabled && view_model.presentation().iccid == modem.iccid);
    assert(count(modem, "AT+CFUN=20") == writes_before);
    drain(view_model);
    assert(!view_model.presentation().enabled && view_model.presentation().status == "Cellular: Off");
    assert(count(modem, "AT+CFUN=20") == writes_before + 1);
    modem.radio_delay = 0ms;

    modem.radio_swallowed.store(false);
    modem.swallow_radio.store(true);
    view_model.refresh();
    wait_for_flag(modem.radio_swallowed);
    view_model.stop();
    assert(!view_model.busy() && !view_model.presentation().control_enabled);
    assert(view_model.presentation().iccid == "Unavailable" && view_model.presentation().total == "Unavailable");
    assert(view_model.presentation().status == "Checking Cellular...");
    assert(view_model.presentation().interface_name == modem.port.device_path());
    refresh(view_model);
    assert(!view_model.presentation().enabled && view_model.presentation().control_enabled);
}

void test_missing_and_busy_port_do_not_publish_stale_data()
{
    Modem modem;
    CellularViewModel view_model(modem.environment());
    refresh(view_model);
    std::atomic_bool cancel{false};
    CellularSerial owner(modem.port.device_path());
    assert(owner.open(cancel));
    const auto command_count = modem.port.command_log().size();
    refresh(view_model);
    assert(view_model.presentation().status == "Cellular Port Is Busy in Another Application");
    assert(!view_model.presentation().control_enabled && !view_model.presentation().enabled);
    assert(view_model.presentation().iccid == "Unavailable" && view_model.presentation().signal == "Unavailable");
    assert(view_model.presentation().total == "Unavailable");
    assert(modem.port.command_log().size() == command_count);
    owner.close();
    refresh(view_model);
    assert(view_model.presentation().enabled && view_model.presentation().control_enabled);

    CellularViewModel missing({modem.port.device_path() + "-missing"});
    refresh(missing);
    assert(missing.presentation().status == "Cellular Modem Unavailable");
    assert(!missing.presentation().control_enabled && missing.presentation().iccid == "Unavailable");
}

void test_sim_state_changes_clear_identity_and_failed_replies_are_not_data()
{
    Modem modem;
    CellularViewModel view_model(modem.environment());
    refresh(view_model);
    for (const auto &value : std::vector<std::pair<std::string, std::string>>{
             {"\r\n+CME ERROR: 10\r\n", "No SIM Detected"},
             {ok("+CPIN: SIM PIN"), "SIM PIN Required"},
             {"\r\n+CME ERROR: 12\r\n", "SIM PUK Required"},
             {"\r\n+CME ERROR: 13\r\n", "SIM Error"}}) {
        modem.sim_reply = value.first;
        const auto identities_before = count(modem, "AT%XICCID");
        refresh(view_model);
        assert(view_model.presentation().sim_status == value.second);
        assert(view_model.presentation().iccid == "Unavailable" && view_model.presentation().imsi == "Unavailable");
        assert(count(modem, "AT%XICCID") == identities_before);
    }
    // CPIN's terminal CME error is authoritative even after a misleading data line.
    modem.sim_reply = "\r\n+CPIN: READY\r\n+CME ERROR: 10\r\n";
    refresh(view_model);
    assert(view_model.presentation().sim_status == "No SIM Detected");
    assert(view_model.presentation().iccid == "Unavailable");
    modem.sim_reply = ok("+CPIN: READY");
    refresh(view_model);
    assert(view_model.presentation().iccid == modem.iccid);
    for (const auto &command : {"AT%XICCID", "AT+CIMI", "AT+COPS?", "AT+CESQ", "AT%XCONNSTAT?"}) {
        auto response = modem.reply(command);
        response.replace(response.rfind("OK"), 2, "ERROR");
        modem.overrides[command] = response;
    }
    refresh(view_model);
    assert(view_model.presentation().iccid == "Unavailable" && view_model.presentation().imsi == "Unavailable");
    assert(view_model.presentation().operator_name == "Unavailable" && view_model.presentation().signal == "Unavailable");
    assert(view_model.presentation().total == "Unavailable");
    assert(view_model.presentation().enabled && view_model.presentation().control_enabled);
}

void test_capabilities_system_mode_and_optional_usage()
{
    Modem modem;
    CellularViewModel view_model(modem.environment());
    modem.capabilities = "+CFUN: (0,1,4)";
    refresh(view_model);
    assert(view_model.presentation().enabled && !view_model.presentation().control_enabled);
    assert(view_model.presentation().status == "Cellular Control Is Not Supported by This Firmware");
    view_model.set_enabled(false);
    assert(!view_model.busy() && count(modem, "AT+CFUN=20") == 0);
    modem.capabilities = "+CFUN: (0,1,4,20,21)";
    modem.overrides["AT%XSYSTEMMODE?"] = "\r\nERROR\r\n";
    refresh(view_model);
    assert(!view_model.presentation().control_enabled && !view_model.presentation().enabled);
    assert(view_model.presentation().status != "LTE Is Not Configured on This Modem");
    assert(view_model.presentation().summary == "Unavailable");
    modem.overrides.clear();
    modem.system_mode = "%XSYSTEMMODE: 0,0,1,0";
    refresh(view_model);
    assert(!view_model.presentation().enabled && !view_model.presentation().control_enabled);
    assert(view_model.presentation().status == "LTE Is Not Configured on This Modem");
    modem.system_mode = "%XSYSTEMMODE: 1,0,1,0";
    modem.overrides["AT%XCONNSTAT?"] = "\r\nERROR\r\n";
    refresh(view_model);
    assert(view_model.presentation().status == "Cellular: Registered");
    assert(view_model.presentation().control_enabled && view_model.presentation().total == "Unavailable");
    assert(view_model.presentation().usage_note == "Data usage requires modem statistics support.");
    modem.overrides.clear();
    refresh(view_model);
    modem.overrides["AT+CEREG?"] = "\r\n+CEREG: 0,1\r\nERROR\r\n";
    refresh(view_model);
    assert(view_model.presentation().status == "Cellular: On (Registration Unavailable)");
    modem.overrides.clear();
    modem.statistics = "%XCONNSTAT: 0,0,3,7,0,0";
    refresh(view_model);
    assert(view_model.presentation().total == "10.0 kB");
    assert(view_model.presentation().usage_note.find("Counters reset") != std::string::npos);
    const auto maximum_kbytes = std::numeric_limits<std::uint64_t>::max() / 1000;
    modem.statistics = "%XCONNSTAT: 0,0," + std::to_string(maximum_kbytes) + "," +
                       std::to_string(maximum_kbytes) + ",0,0";
    refresh(view_model);
    assert(view_model.presentation().total == "36.9 EB");
    modem.statistics = "%XCONNSTAT: 0,0," + std::to_string(maximum_kbytes + 1) + ",7,0,0";
    refresh(view_model);
    assert(view_model.presentation().total == "Unavailable");
}

}  // namespace

int main()
{
    lv_init();
    test_readback_rejection_unapplied_change_and_applied_error();
    test_atomic_snapshot_background_cancellation_and_reopen();
    test_missing_and_busy_port_do_not_publish_stale_data();
    test_sim_state_changes_clear_identity_and_failed_replies_are_not_data();
    test_capabilities_system_mode_and_optional_usage();
    lv_deinit();
}
