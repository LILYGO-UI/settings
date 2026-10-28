#include "pages/cellular/cellular_model.hpp"

#include <cassert>
#include <cstdlib>
#include <limits>
#include <string>

namespace {
using namespace lilygo::settings;

void load(CellularModel &model)
{
    assert(model.update_capabilities("AT+CFUN=?\r\n+CFUN: (0,1,4,20,21,30,31,40,41,44)\r\nOK\r\n"));
    assert(model.update_system_mode("%XSYSTEMMODE: 1,0,1,0\n"));
    assert(model.update_radio("AT+CFUN?\r\n+CFUN: 1\r\nOK\r\n"));
    assert(model.update_sim_status("+CPIN: READY\r\nOK\r\n"));
    assert(model.update_iccid("%XICCID: 89860012345678901234\r\nOK\r\n"));
    assert(model.update_imsi("AT+CIMI\r\n460001234567890\r\nOK\r\n"));
    assert(model.update_registration("+CEREG: 2,1,\"00AB\",\"00112233\",7\r\nOK\r\n"));
    assert(model.update_operator("+COPS: 0,0,\"China Mobile\",7\r\nOK\r\n"));
    assert(model.update_signal("+CESQ: 99,99,255,255,15,57\r\nOK\r\n"));
    assert(model.update_usage("%XCONNSTAT: 2,3,524,1048,708,650\r\nOK\r\n"));
}

void test_serial_defaults_and_nordic_replies()
{
    CellularModel model;
    load(model);
    const auto &state = model.state();
    assert(state.radio_known && state.enabled && state.function_mode == 1);
    assert(state.enable_function == 21 && state.disable_function == 20);
    assert(state.sim_known && state.sim_ready && state.sim_status == "Ready");
    assert(state.iccid == "89860012345678901234" && state.imsi == "460001234567890");
    assert(state.registration_known && state.registration_status == 1);
    assert(state.operator_name == "China Mobile" && state.access_technology == "LTE-M");
    assert(state.signal_known && state.signal_is_rsrp && state.signal_dbm == -84);
    assert(state.usage_known && state.received == 1048000 && state.transmitted == 524000);

    const char *previous = std::getenv("LILYGO_UI_CELLULAR_DEVICE");
    const bool present = previous != nullptr;
    const std::string saved = previous ? previous : "";
    unsetenv("LILYGO_UI_CELLULAR_DEVICE");
    assert(CellularEnvironment::from_process_environment().serial_device == "/dev/ttyACM0");
    setenv("LILYGO_UI_CELLULAR_DEVICE", "/dev/pts/7", 1);
    assert(CellularEnvironment::from_process_environment().serial_device == "/dev/pts/7");
    if (present) setenv("LILYGO_UI_CELLULAR_DEVICE", saved.c_str(), 1);
    else unsetenv("LILYGO_UI_CELLULAR_DEVICE");
}

void test_lte_switch_preserves_gnss_and_reads_actual_system_mode()
{
    CellularModel model;
    assert(!model.update_radio("+CFUN: 1\n")); // Full functionality alone may mean GNSS only.
    assert(model.update_system_mode("%XSYSTEMMODE: 0,0,1,0\n"));
    assert(model.state().radio_known && !model.state().enabled);
    assert(model.update_system_mode("%XSYSTEMMODE: 0,1,1,0\n"));
    assert(model.state().radio_known && model.state().enabled);
    assert(!model.update_system_mode("%XSYSTEMMODE: 1,garbage,1,0\n"));
    assert(!model.state().radio_known);
    assert(model.update_radio("+CFUN: 21\n"));
    assert(model.state().enabled);
    for (const int mode : {0, 4, 20, 31, 41, 44}) {
        assert(model.update_radio("+CFUN: " + std::to_string(mode) + "\n"));
        assert(!model.state().enabled);
    }
    assert(!model.update_radio("+CFUN: 2\n")); // Receive-only does not mean enabled or disabled.
    assert(!model.update_radio("+CFUN: 21garbage\n"));
    assert(!model.update_radio("+CFUN: 21\n+CFUN: 4\n"));

    assert(model.update_capabilities("+CFUN: (0,1,4,20-21,30-31,40-41,44),(0-1)\n"));
    assert(model.state().enable_function == 21 && model.state().disable_function == 20);
    assert(model.update_capabilities("+CFUN: (0,1,4)\n"));
    assert(model.state().enable_function == -1 && model.state().disable_function == -1);
    assert(model.update_capabilities("+CFUN: (20)\n"));
    assert(model.state().enable_function == -1);
    for (const auto *reply : {"+CFUN: (20-19)", "+CFUN: (0-2147483647)",
                              "+CFUN: (20,21", "+CFUN: (20garbage,21)", "OK"}) {
        assert(!model.update_capabilities(reply));
        assert(!model.state().capabilities_known && model.state().disable_function == -1);
    }
}

void test_sim_errors_and_bad_identity_clear_old_values()
{
    CellularModel model;
    load(model);
    assert(model.update_sim_status("+CPIN: SIM PIN\n"));
    assert(model.state().sim_known && !model.state().sim_ready);
    assert(model.state().sim_status == "SIM PIN Required");
    assert(model.state().iccid.empty() && model.state().imsi.empty());
    assert(model.update_sim_status("+CME ERROR: 10\n"));
    assert(model.state().sim_status == "No SIM Detected");
    assert(model.update_sim_status("+CME ERROR: SIM not inserted\n"));
    assert(model.state().sim_status == "No SIM Detected");
    assert(model.update_sim_status("+CME ERROR: 11\n"));
    assert(model.state().sim_status == "SIM PIN Required");
    assert(model.update_sim_status("+CME ERROR: SIM PUK required\n"));
    assert(model.state().sim_status == "SIM PUK Required");
    assert(model.update_sim_status("+CME ERROR: 13\n"));
    assert(model.state().sim_status == "SIM Error");
    assert(!model.update_sim_status("+CME ERROR: 999\n"));
    assert(!model.state().sim_known && model.state().sim_status.empty());
    assert(!model.update_sim_status("+CPIN: UNKNOWN\n"));
    assert(model.update_iccid("%XICCID: \"89860012345678901234\"\n"));
    assert(!model.update_iccid("%XICCID: 8986001234567890123F\n"));
    assert(model.state().iccid.empty());
    assert(!model.update_iccid("%XICCID: 12345\n"));
    assert(!model.update_imsi("+CIMI: invalid\nOK\n"));
    assert(model.state().imsi.empty());
    assert(!model.update_imsi("460001234567890\n460001234567891\n"));
    assert(model.state().imsi.empty());
    assert(!model.update_imsi("4600012345678901\n"));
}

void test_signal_and_registration_unknowns_are_not_fabricated()
{
    CellularModel model;
    load(model);
    assert(model.update_signal("+CESQ: 99,99,255,255,255,255\n"));
    assert(!model.state().signal_known && model.state().signal == -1);
    assert(model.update_signal("+CSQ: 15,99\n"));
    assert(model.state().signal_known && !model.state().signal_is_rsrp);
    assert(model.state().signal_dbm == -83 && model.state().signal == 48);
    assert(model.update_signal("+CSQ: 99,99\n"));
    assert(!model.state().signal_known);
    assert(!model.update_signal("+CSQ: 32,99\n"));
    assert(!model.update_signal("+CESQ: 99,99,255,255,15,98\n"));
    assert(!model.update_signal("+CESQ: 99,99,255,255,15,-1\n"));
    assert(!model.update_signal("+CESQ: 99,99,255,255,15\n"));
    assert(!model.update_signal("+CESQ: garbage,99,255,255,15,57\n"));
    assert(!model.update_signal("+CSQ: 15,garbage\n"));
    assert(!model.state().signal_known);
    assert(model.update_signal("+CESQ: 99,99,255,255,15,0\n"));
    assert(model.state().signal_known && model.state().signal_index == 0);
    assert(model.update_operator("+COPS: 0,0,\"China, Mobile\",9\n"));
    assert(model.state().operator_name == "China, Mobile" && model.state().access_technology == "NB-IoT");
    assert(model.update_operator("+COPS: 0,2,\"46000\",7\n"));
    assert(model.state().operator_name == "46000");
    assert(model.update_operator("+COPS: 0\n"));
    assert(model.state().operator_name.empty() && model.state().access_technology.empty());
    assert(!model.update_operator("+COPS: 0,0,\"missing quote,7\n"));
    assert(!model.update_operator("+COPS: 0,2,\"invalid\",7\n"));
    assert(model.update_registration("+CEREG: 0,5\n"));
    assert(model.state().registration_status == 5);
    assert(model.update_registration("+CEREG: 0,2\n"));
    assert(model.state().registration_status == 2);
    assert(!model.update_registration("+CEREG: 5\n")); // Unsolicited report is not a query reply.
    assert(!model.update_registration("+CEREG: 0,999\n"));
    assert(!model.state().registration_known);
}

void test_modem_usage_counter_units_reset_overflow_and_missing_data()
{
    CellularModel model;
    load(model);
    assert(model.update_usage("%XCONNSTAT=2,3,45,60,708,650\nOK\n"));
    assert(model.state().counters_reset);
    assert(model.state().transmitted == 45000 && model.state().received == 60000);
    assert(model.update_usage("%XCONNSTAT: 2,3,45,60,708,650\n"));
    assert(!model.state().counters_reset);
    const auto max_kbytes = std::numeric_limits<std::uint64_t>::max() / 1000;
    assert(model.update_usage("%XCONNSTAT: 0,0," + std::to_string(max_kbytes) + ",1,0,0\n"));
    assert(model.state().transmitted == max_kbytes * 1000);
    for (const auto &reply : {std::string("%XCONNSTAT: 0,0,") + std::to_string(max_kbytes + 1) + ",1,0,0\n",
                             std::string("%XCONNSTAT: 0,0,1,-1,0,0\n"),
                             std::string("%XCONNSTAT: 0,0,1,2,0\n"),
                             std::string("%XCONNSTAT: 0,0,1,2garbage,0,0\n"),
                             std::string("ERROR\n")}) {
        assert(!model.update_usage(reply));
        assert(!model.state().usage_known && model.state().received == 0 && model.state().transmitted == 0);
    }
    model.reset();
    assert(!model.state().radio_known && !model.state().sim_known && model.state().operator_name.empty());
    assert(model.state().iccid.empty() && !model.state().signal_known && !model.state().usage_known);
    assert(model.update_usage("%XCONNSTAT: 0,0,0,0,0,0\n"));
    assert(model.state().usage_known && !model.state().counters_reset);
}
}  // namespace

int main()
{
    test_serial_defaults_and_nordic_replies();
    test_lte_switch_preserves_gnss_and_reads_actual_system_mode();
    test_sim_errors_and_bad_identity_clear_old_values();
    test_signal_and_registration_unknowns_are_not_fabricated();
    test_modem_usage_counter_units_reset_overflow_and_missing_data();
}
