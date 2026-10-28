#include "pages/cellular/cellular_model.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <vector>

namespace lilygo::settings {
namespace {

std::string trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == std::string_view::npos ? std::string{} :
        std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

std::vector<std::string> lines(std::string_view output)
{
    std::vector<std::string> result;
    std::istringstream input{std::string(output)};
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.empty()) result.push_back(std::move(line));
    }
    return result;
}

bool payload(std::string_view output, std::string_view prefix, std::string &value)
{
    bool found = false;
    for (const auto &line : lines(output)) {
        if (line.compare(0, prefix.size(), prefix) != 0) continue;
        // Reject ambiguous duplicate query replies rather than using a stale one.
        if (found) return false;
        value = trim(std::string_view(line).substr(prefix.size()));
        found = true;
    }
    return found;
}

template <typename Integer>
bool number(std::string_view text, Integer &value)
{
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool csv(std::string_view text, std::vector<std::string> &fields)
{
    bool quoted = false;
    std::string part;
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 32 || c == 127) return false;
        if (c == '"') quoted = !quoted;
        if (c == ',' && !quoted) {
            fields.push_back(trim(part));
            part.clear();
        } else {
            part += c;
        }
    }
    if (quoted) return false;
    fields.push_back(trim(part));
    return true;
}

std::string unquote(std::string value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return value.substr(1, value.size() - 2);
    return value;
}

bool digits(std::string_view value, std::size_t minimum, std::size_t maximum)
{
    return value.size() >= minimum && value.size() <= maximum &&
        std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string technology(const std::string &value)
{
    // nRF9151 supports LTE-M and NB-IoT; do not invent additional radio modes.
    return value == "7" ? "LTE-M" : value == "9" ? "NB-IoT" : std::string{};
}

bool supported_functions(const std::string &value, std::vector<int> &functions)
{
    if (value.empty() || value.front() != '(') return false;
    const auto closing = value.find(')');
    if (closing == std::string::npos) return false;
    const auto trailing = trim(std::string_view(value).substr(closing + 1));
    // Standard CFUN test replies may include a second list of reset options.
    if (!trailing.empty() && (trailing.front() != ',' || trailing.size() < 4 ||
        trailing.find('(', 1) == std::string::npos || trailing.back() != ')')) return false;
    std::vector<std::string> ranges;
    if (!csv(std::string_view(value).substr(1, closing - 1), ranges)) return false;
    for (const auto &range : ranges) {
        const auto dash = range.find('-');
        int first = 0, last = 0;
        if (dash == std::string::npos) {
            if (!number(range, first)) return false;
            last = first;
        } else if (!number(trim(std::string_view(range).substr(0, dash)), first) ||
                   !number(trim(std::string_view(range).substr(dash + 1)), last)) {
            return false;
        }
        if (first < 0 || last > 255 || last < first) return false;
        for (int function = first; function <= last; ++function) functions.push_back(function);
    }
    return !functions.empty();
}

}  // namespace

CellularEnvironment CellularEnvironment::from_process_environment()
{
    const char *device = std::getenv("LILYGO_UI_CELLULAR_DEVICE");
    return {device && *device ? device : "/dev/ttyACM0"};
}

CellularModel::CellularModel(CellularEnvironment)
{
}

const CellularState &CellularModel::state() const noexcept { return state_; }

void CellularModel::resolve_radio() noexcept
{
    state_.radio_known = true;
    switch (state_.function_mode) {
    case 21: state_.enabled = true; break;
    case 1:
        state_.radio_known = state_.system_mode_known;
        state_.enabled = state_.system_mode_known && state_.lte_supported;
        break;
    case 0: case 4: case 20: case 31: case 41: case 44:
        state_.enabled = false;
        break;
    default:
        state_.radio_known = state_.enabled = false;
    }
}

bool CellularModel::update_radio(std::string_view output)
{
    radio_failed();
    std::string value;
    if (!payload(output, "+CFUN:", value) || !number(value, state_.function_mode)) return false;
    resolve_radio();
    return state_.radio_known;
}

bool CellularModel::update_capabilities(std::string_view output)
{
    capabilities_failed();
    std::string value;
    std::vector<int> functions;
    if (!payload(output, "+CFUN:", value) || !supported_functions(value, functions)) return false;
    state_.capabilities_known = true;
    const auto supports = [&](int function) {
        return std::find(functions.begin(), functions.end(), function) != functions.end();
    };
    // Dedicated LTE control preserves GNSS. Never use CFUN=0: it writes NVM.
    if (supports(20) && supports(21)) {
        state_.enable_function = 21;
        state_.disable_function = 20;
    }
    return true;
}

bool CellularModel::update_system_mode(std::string_view output)
{
    state_.system_mode_known = state_.lte_supported = false;
    std::string value;
    std::vector<std::string> fields;
    if (!payload(output, "%XSYSTEMMODE:", value) || !csv(value, fields) || fields.size() != 4) {
        resolve_radio();
        return false;
    }
    int ltem = 0, nbiot = 0, gnss = 0, preference = 0;
    if (!number(fields[0], ltem) || !number(fields[1], nbiot) || !number(fields[2], gnss) ||
        !number(fields[3], preference) || ltem < 0 || ltem > 1 || nbiot < 0 || nbiot > 1 ||
        gnss < 0 || gnss > 1 || preference < 0 || preference > 4) {
        resolve_radio();
        return false;
    }
    state_.system_mode_known = true;
    state_.lte_supported = ltem != 0 || nbiot != 0;
    resolve_radio();
    return true;
}

bool CellularModel::update_sim_status(std::string_view output)
{
    sim_failed();
    std::string value;
    if (payload(output, "+CPIN:", value)) {
        static constexpr const char *statuses[] = {"READY", "SIM PIN", "SIM PUK", "SIM PIN2", "SIM PUK2",
            "PH-SIM PIN", "PH-FSIM PIN", "PH-FSIM PUK", "PH-NET PIN", "PH-NET PUK", "PH-NETSUB PIN",
            "PH-NETSUB PUK", "PH-SP PIN", "PH-SP PUK", "PH-CORP PIN", "PH-CORP PUK"};
        value = unquote(value);
        if (std::find(std::begin(statuses), std::end(statuses), value) == std::end(statuses)) return false;
    } else if (payload(output, "+CME ERROR:", value)) {
        if (value == "10" || value == "SIM not inserted") value = "No SIM Detected";
        else if (value == "11" || value == "SIM PIN required") value = "SIM PIN";
        else if (value == "12" || value == "SIM PUK required") value = "SIM PUK";
        else if (value == "13" || value == "SIM failure") value = "SIM Error";
        else if (value == "14" || value == "SIM busy") value = "SIM Busy";
        else if (value == "15" || value == "SIM wrong") value = "SIM Error";
        else return false;
    } else return false;
    state_.sim_known = true;
    state_.sim_ready = value == "READY";
    if (value == "READY") value = "Ready";
    else if (value == "SIM PIN") value = "SIM PIN Required";
    else if (value == "SIM PUK") value = "SIM PUK Required";
    state_.sim_status = value;
    return true;
}

bool CellularModel::update_iccid(std::string_view output)
{
    state_.iccid.clear();
    std::string value;
    if (!payload(output, "%XICCID:", value)) return false;
    value = unquote(value);
    if (!digits(value, 19, 20)) return false;
    state_.iccid = value;
    return true;
}

bool CellularModel::update_imsi(std::string_view output)
{
    state_.imsi.clear();
    for (const auto &line : lines(output)) {
        if (!digits(line, 14, 15)) continue;
        if (!state_.imsi.empty()) { state_.imsi.clear(); return false; }
        state_.imsi = line;
    }
    return !state_.imsi.empty();
}

bool CellularModel::update_operator(std::string_view output)
{
    operator_failed();
    std::string value;
    std::vector<std::string> fields;
    int mode = -1, format = -1;
    if (!payload(output, "+COPS:", value) || !csv(value, fields) || fields.empty() ||
        fields.size() > 4 || !number(fields[0], mode) || mode < 0 || mode > 4) return false;
    if (fields.size() == 1) return true; // No currently selected operator.
    if (fields.size() < 3 || !number(fields[1], format) || format < 0 || format > 2) return false;
    value = unquote(fields[2]);
    if (value.size() > 128 || (format == 2 && !value.empty() && !digits(value, 5, 6))) return false;
    state_.operator_name = value;
    if (fields.size() == 4) state_.access_technology = technology(fields[3]);
    return true;
}

bool CellularModel::update_signal(std::string_view output)
{
    signal_failed();
    std::string value;
    std::vector<std::string> fields;
    if (payload(output, "+CESQ:", value)) {
        if (!csv(value, fields) || fields.size() != 6) return false;
        int values[6]{};
        for (std::size_t index = 0; index < fields.size(); ++index)
            if (!number(fields[index], values[index])) return false;
        const auto ranged_or_unknown = [](int measured, int maximum, int unknown) {
            return (measured >= 0 && measured <= maximum) || measured == unknown;
        };
        if (!ranged_or_unknown(values[0], 63, 99) || !ranged_or_unknown(values[1], 7, 99) ||
            !ranged_or_unknown(values[2], 96, 255) || !ranged_or_unknown(values[3], 49, 255) ||
            !ranged_or_unknown(values[4], 34, 255)) return false;
        const int rsrp = values[5];
        if (rsrp == 255) return true;
        if (rsrp < 0 || rsrp > 97) return false;
        state_.signal_is_rsrp = true;
        state_.signal_index = rsrp;
        state_.signal_dbm = rsrp - 141;
        state_.signal = std::clamp((state_.signal_dbm + 120) * 2, 0, 100);
    } else if (payload(output, "+CSQ:", value)) {
        if (!csv(value, fields) || fields.size() != 2) return false;
        int rssi = 0, error_rate = 0;
        if (!number(fields[0], rssi) || !number(fields[1], error_rate) ||
            !((error_rate >= 0 && error_rate <= 7) || error_rate == 99)) return false;
        if (rssi == 99) return true;
        if (rssi < 0 || rssi > 31) return false;
        state_.signal_dbm = -113 + 2 * rssi;
        state_.signal_index = rssi;
        state_.signal = (rssi * 100 + 15) / 31;
    } else return false;
    state_.signal_known = true;
    return true;
}

bool CellularModel::update_registration(std::string_view output)
{
    registration_failed();
    std::string value;
    std::vector<std::string> fields;
    if (!payload(output, "+CEREG:", value) || !csv(value, fields) || fields.size() < 2) return false;
    int presentation = 0, status = 0;
    if (!number(fields[0], presentation) || presentation < 0 || presentation > 5 ||
        !number(fields[1], status) || status < 0 || status > 10) return false;
    state_.registration_known = true;
    state_.registration_status = status;
    return true;
}

bool CellularModel::update_usage(std::string_view output)
{
    clear_usage();
    std::string value;
    std::vector<std::string> fields;
    if ((!payload(output, "%XCONNSTAT:", value) && !payload(output, "%XCONNSTAT=", value)) ||
        !csv(value, fields) || fields.size() != 6) return false;
    std::uint64_t counters[6]{};
    for (std::size_t index = 0; index < fields.size(); ++index)
        if (!number(fields[index], counters[index])) return false;
    constexpr auto limit = std::numeric_limits<std::uint64_t>::max() / 1000;
    if (counters[2] > limit || counters[3] > limit) return false;
    state_.transmitted = counters[2] * 1000;
    state_.received = counters[3] * 1000;
    state_.counters_reset = last_usage_known_ &&
        (state_.received < last_received_ || state_.transmitted < last_transmitted_);
    last_received_ = state_.received;
    last_transmitted_ = state_.transmitted;
    last_usage_known_ = state_.usage_known = true;
    return true;
}

void CellularModel::radio_failed() noexcept
{
    state_.function_mode = -1;
    state_.radio_known = state_.enabled = false;
}

void CellularModel::capabilities_failed() noexcept
{
    state_.capabilities_known = false;
    state_.enable_function = state_.disable_function = -1;
}

void CellularModel::sim_failed() noexcept
{
    state_.sim_known = state_.sim_ready = false;
    state_.sim_status.clear();
    state_.iccid.clear();
    state_.imsi.clear();
}

void CellularModel::operator_failed() noexcept
{
    state_.operator_name.clear();
    state_.access_technology.clear();
}

void CellularModel::signal_failed() noexcept
{
    state_.signal_known = state_.signal_is_rsrp = false;
    state_.signal_dbm = 0;
    state_.signal_index = -1;
    state_.signal = -1;
}

void CellularModel::registration_failed() noexcept
{
    state_.registration_known = false;
    state_.registration_status = -1;
}

void CellularModel::clear_usage() noexcept
{
    state_.usage_known = state_.counters_reset = false;
    state_.received = state_.transmitted = 0;
}

void CellularModel::reset() noexcept
{
    state_ = {};
    last_usage_known_ = false;
    last_received_ = last_transmitted_ = 0;
}

}  // namespace lilygo::settings
