#include "pages/cellular/cellular_view_model.hpp"
#include "pages/cellular/cellular_serial.hpp"

#include <chrono>
#include <exception>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace lilygo::settings {
namespace {

std::string available(const std::string &text) { return text.empty() ? "Unavailable" : text; }

std::string bytes(long double value)
{
    const char *units[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB"};
    unsigned unit = 0;
    while (value >= 1000 && unit < 6) { value /= 1000; ++unit; }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(unit ? 1 : 0) << value << ' ' << units[unit];
    return output.str();
}

std::string response_text(const CellularAtResponse &response)
{
    std::string text;
    for (const auto &line : response.lines) { text += line; text += '\n'; }
    if (!response.success) text += response.error;
    return text;
}

std::string serial_error(const std::string &error)
{
    if (error.find("permission denied") != std::string::npos || error.find("operation not permitted") != std::string::npos)
        return "Serial Port Permission Is Required";
    if (error.find("busy") != std::string::npos || error.find("Busy") != std::string::npos)
        return "Cellular Port Is Busy in Another Application";
    if (error.find("timed out") != std::string::npos) return "Cellular Modem Timed Out";
    return "Cellular Modem Unavailable";
}

void clear_snapshot(CellularModel &model)
{
    model.radio_failed();
    model.capabilities_failed();
    (void)model.update_system_mode("");
    model.sim_failed();
    model.operator_failed();
    model.signal_failed();
    model.registration_failed();
    model.clear_usage();
}

}  // namespace

CellularViewModel::CellularViewModel(CellularEnvironment environment)
    : environment_(std::move(environment)), model_(environment_)
{
    presentation_.interface_name = environment_.serial_device;
}

CellularViewModel::~CellularViewModel() { stop(); }

void CellularViewModel::refresh()
{
    if (!busy()) start();
}

void CellularViewModel::start(std::optional<bool> target)
{
    const int function = target ? (*target ? model_.state().enable_function : model_.state().disable_function) : -1;
    cancelled_.store(false);
    ready_.store(false);
    completed_model_.reset();
    completed_error_.clear();
    completed_change_error_.clear();
    requested_ = target;
    try {
        worker_ = std::thread([this, snapshot = model_, target, function]() mutable {
            try {
                run(std::move(snapshot), target, function);
            } catch (const std::exception &) {
                completed_model_.emplace(environment_);
                completed_error_ = "Cannot Read Cellular Modem";
            }
            ready_.store(true, std::memory_order_release);
        });
    } catch (const std::exception &) {
        model_.reset();
        requested_.reset();
        error_ = "Cannot Start Cellular Query";
        checked_ = true;
        publish();
        return;
    }
    if (target) {
        presentation_.control_enabled = false;
        presentation_.status = *target ? "Turning On Cellular..." : "Turning Off Cellular...";
        revision_.publish();
    }
}

void CellularViewModel::run(CellularModel snapshot, std::optional<bool> target, int function)
{
    clear_snapshot(snapshot);
    CellularSerial serial(environment_.serial_device);
    const auto finish = [&] { completed_model_ = std::move(snapshot); };
    if (!serial.open(cancelled_)) {
        completed_error_ = serial_error(serial.error());
        finish();
        return;
    }
    auto reply = serial.command("AT", cancelled_);
    if (!reply.success) {
        completed_error_ = serial_error(reply.error);
        finish();
        return;
    }
    if (target) {
        reply = serial.command("AT+CFUN=" + std::to_string(function), cancelled_, std::chrono::seconds(12));
        if (!reply.success) {
            completed_change_error_ = reply.timed_out ? "Cellular Change Timed Out" : "Modem Rejected Cellular Change";
            if (!serial.is_open()) {
                completed_error_ = serial_error(reply.error);
                finish();
                return;
            }
        }
    }
    const auto query = [&](const char *command, bool (CellularModel::*parse)(std::string_view)) {
        const auto response = serial.command(command, cancelled_);
        // Only the terminal CME SIM error is meaningful after a failed command;
        // partial payloads must never override an ERROR terminator.
        const auto payload = response.success ? response_text(response) :
            parse == &CellularModel::update_sim_status ? response.error : std::string{};
        (void)(snapshot.*parse)(payload);
        if (!serial.is_open()) {
            clear_snapshot(snapshot);
            completed_error_ = serial_error(response.error);
            return false;
        }
        return !cancelled_.load();
    };
    if (!query("AT+CFUN?", &CellularModel::update_radio) ||
        !query("AT%XSYSTEMMODE?", &CellularModel::update_system_mode) ||
        !query("AT+CFUN=?", &CellularModel::update_capabilities) ||
        !query("AT+CPIN?", &CellularModel::update_sim_status)) {
        finish();
        return;
    }
    if (snapshot.state().sim_ready) {
        if (!query("AT%XICCID", &CellularModel::update_iccid) || !query("AT+CIMI", &CellularModel::update_imsi)) {
            finish();
            return;
        }
    }
    if (!query("AT+CEREG?", &CellularModel::update_registration) ||
        !query("AT+CESQ", &CellularModel::update_signal) ||
        !query("AT+COPS?", &CellularModel::update_operator) ||
        !query("AT%XCONNSTAT?", &CellularModel::update_usage)) {
        finish();
        return;
    }
    finish();
}

void CellularViewModel::poll_command()
{
    if (!worker_.joinable() || !ready_.load(std::memory_order_acquire)) return;
    worker_.join();
    if (completed_model_) model_ = std::move(*completed_model_);
    error_ = std::move(completed_error_);
    if (queued_change_) {
        const bool target = *queued_change_;
        queued_change_.reset();
        completed_model_.reset();
        checked_ = true;
        const auto &state = model_.state();
        if (state.radio_known && state.system_mode_known && state.lte_supported &&
            state.enable_function >= 0 && state.disable_function >= 0) {
            if (state.enabled != target) {
                start(target);
                return;
            }
        } else if (error_.empty()) {
            change_error_ = "Cannot Verify Cellular State Before Change";
        }
        publish();
        return;
    }
    if (requested_ && !completed_change_error_.empty()) change_error_ = std::move(completed_change_error_);
    const auto target = requested_ ? requested_ : failed_request_;
    if (target) {
        if (model_.state().radio_known && model_.state().enabled == *target) {
            change_error_.clear();
            failed_request_.reset();
        } else {
            failed_request_ = target;
            if (change_error_.empty()) change_error_ = "Cellular Change Was Not Applied";
        }
    }
    requested_.reset();
    completed_model_.reset();
    checked_ = true;
    publish();
}

void CellularViewModel::set_enabled(bool enabled)
{
    if (!presentation_.control_enabled || requested_ || enabled == model_.state().enabled) {
        revision_.publish();
        return;
    }
    failed_request_.reset();
    change_error_.clear();
    if (busy()) {
        // Finish the outstanding AT transaction before opening a new session:
        // late untagged replies cannot safely be associated after cancellation.
        queued_change_ = enabled;
        presentation_.control_enabled = false;
        presentation_.status = enabled ? "Turning On Cellular..." : "Turning Off Cellular...";
        revision_.publish();
        return;
    }
    start(enabled);
}

void CellularViewModel::cancel() noexcept
{
    cancelled_.store(true);
    if (worker_.joinable()) worker_.join();
    ready_.store(false);
    completed_model_.reset();
}

void CellularViewModel::stop() noexcept
{
    cancel();
    model_.reset();
    requested_.reset();
    queued_change_.reset();
    failed_request_.reset();
    error_.clear();
    change_error_.clear();
    checked_ = false;
    presentation_ = CellularPresentation{};
    presentation_.interface_name = environment_.serial_device;
}

bool CellularViewModel::busy() const noexcept { return worker_.joinable(); }
const CellularPresentation &CellularViewModel::presentation() const noexcept { return presentation_; }
lv_subject_t *CellularViewModel::revision_subject() noexcept { return revision_.get(); }

void CellularViewModel::publish()
{
    const auto &state = model_.state();
    CellularPresentation next;
    next.interface_name = environment_.serial_device;
    next.enabled = state.enabled;
    const bool control_supported = state.capabilities_known && state.enable_function >= 0 && state.disable_function >= 0;
    next.control_enabled = state.radio_known && control_supported && state.system_mode_known &&
                           state.lte_supported && !requested_;
    next.operator_name = available(state.operator_name);
    next.sim_status = available(state.sim_status);
    next.iccid = available(state.iccid);
    next.imsi = available(state.imsi);
    next.access_technology = available(state.access_technology);
    if (state.signal_known) {
        if (state.signal_is_rsrp && state.signal_index == 0) next.signal = "< -140 dBm RSRP";
        else if (state.signal_is_rsrp && state.signal_index == 97) next.signal = ">= -44 dBm RSRP";
        else next.signal = std::to_string(state.signal_dbm) + (state.signal_is_rsrp ? " dBm RSRP" : " dBm RSSI");
    }
    next.summary = !checked_ ? "Checking..." : !state.radio_known ? "Unavailable" : state.enabled ? "On" : "Off";
    if (!error_.empty()) next.status = error_;
    else if (!change_error_.empty()) next.status = change_error_;
    else if (!checked_) next.status = "Checking Cellular...";
    else if (!state.radio_known) next.status = "Cannot Read Cellular Radio State";
    else if (!control_supported) next.status = "Cellular Control Is Not Supported by This Firmware";
    else if (!state.system_mode_known) next.status = "Cannot Read Cellular System Mode";
    else if (!state.lte_supported) next.status = "LTE Is Not Configured on This Modem";
    else if (!state.enabled) next.status = "Cellular: Off";
    else if (!state.sim_ready) next.status = next.sim_status;
    else if (!state.registration_known) next.status = "Cellular: On (Registration Unavailable)";
    else if (state.registration_status == 1 || state.registration_status == 5)
        next.status = state.registration_status == 5 ? "Cellular: Roaming" : "Cellular: Registered";
    else if (state.registration_status == 2) next.status = "Searching for Cellular Network...";
    else if (state.registration_status == 3) next.status = "Cellular Registration Denied";
    else next.status = "Cellular: On (Not Registered)";
    if (state.usage_known) {
        next.received = bytes(state.received);
        next.transmitted = bytes(state.transmitted);
        next.total = bytes(static_cast<long double>(state.received) + static_cast<long double>(state.transmitted));
        next.usage_note = "Modem-reported totals for its statistics collection period. "
                          "Collection may be stopped; not carrier billing.";
        if (state.counters_reset) next.usage_note = "Counters reset. " + next.usage_note;
    }
    presentation_ = std::move(next);
    revision_.publish();
}

}  // namespace lilygo::settings
