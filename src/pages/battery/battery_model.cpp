#include "pages/battery/battery_model.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lilygo::settings {
namespace {

std::string read_text(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    char buffer[4096];
    input.read(buffer, sizeof(buffer));
    if (input.gcount() == static_cast<std::streamsize>(sizeof(buffer))) return {};
    std::string value(buffer, static_cast<std::size_t>(input.gcount()));
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : value.substr(first);
}

std::optional<std::int64_t> read_integer(const std::filesystem::path &path, std::int64_t minimum = 0,
                                         std::int64_t maximum = std::numeric_limits<int>::max())
{
    const auto text    = read_text(path);
    std::int64_t value = 0;
    const auto parsed  = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < minimum || value > maximum)
        return std::nullopt;
    return value;
}

bool chip_matches(std::string value, const char *chip)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    const std::string name(chip);
    if (value == name) return true;
    return value.size() > name.size() && value.compare(0, name.size(), name) == 0 &&
           (value[name.size()] == '-' || value[name.size()] == '_');
}

bool is_chip(const std::filesystem::path &path, const char *chip)
{
    return chip_matches(path.filename().string(), chip) || chip_matches(read_text(path / "model_name"), chip);
}

void read_value(const std::filesystem::path &path, std::int64_t &value, bool &present, std::int64_t minimum = 0)
{
    const auto reading = read_integer(path, minimum);
    present            = reading.has_value();
    value              = reading.value_or(0);
}

std::string text_or_unknown(const std::filesystem::path &path)
{
    auto value = read_text(path);
    return value.empty() ? "Unknown" : value;
}

ChargeControl read_control(const std::filesystem::path &path, std::int64_t minimum, std::int64_t maximum,
                           std::int64_t step, bool allow_write)
{
    ChargeControl control;
    const auto value = read_integer(path);
    if (!value) return control;
    control.available = true;
    control.value     = *value;
    control.minimum   = minimum;
    control.maximum   = maximum;
    control.step      = step;
    struct stat attributes{};
    control.writable = allow_write && maximum >= minimum && step > 0 && stat(path.c_str(), &attributes) == 0 &&
                       (attributes.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0;
    control.requires_authorization = control.writable && access(path.c_str(), W_OK) != 0;
    return control;
}

BatteryCapacityConfiguration read_capacity_configuration(const std::filesystem::path &path)
{
    BatteryCapacityConfiguration state;
    std::istringstream input(read_text(path));
    int design = 0;
    int full   = 0;
    // Reads expose raw unsigned 16-bit runtime values. Keep a usable control
    // when a gauge needs a zero/invalid seed corrected; new writes still obey
    // the signed CEDV parameter bounds declared in the state.
    if (!(input >> design >> full) || design < 0 || design > 65535 || full < 0 || full > 65535)
        return state;
    input >> std::ws;
    if (!input.eof()) return state;
    state.available  = true;
    state.design_mah = design;
    state.full_mah   = full;
    struct stat attributes{};
    state.writable = stat(path.c_str(), &attributes) == 0 && (attributes.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0;
    state.requires_authorization = state.writable && access(path.c_str(), W_OK) != 0;
    return state;
}

std::string device_token(const struct stat &attributes)
{
    return std::to_string(static_cast<std::uintmax_t>(attributes.st_dev)) + ":" +
           std::to_string(static_cast<std::uintmax_t>(attributes.st_ino));
}

std::string device_identity(const std::string &path, std::string &control_token)
{
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    struct stat attributes{};
    const auto location = error ? path : canonical.string();
    if (stat(path.c_str(), &attributes) != 0) {
        control_token.clear();
        return {};
    }
    control_token = device_token(attributes);
    return location + "\n" + control_token + "\n" + read_text(std::filesystem::path(path) / "serial_number");
}

int open_control(const std::string &device_path, const char *attribute, const std::string &expected_token)
{
    // The class entry is legitimately a symlink. Anchor the attribute write to
    // its verified device directory so a remove/reprobe between discovery and
    // open cannot redirect this operation to a new gauge or charger.
    const int directory = open(device_path.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (directory < 0) return -1;
    struct stat attributes{};
    if (fstat(directory, &attributes) != 0 || expected_token.empty() || device_token(attributes) != expected_token) {
        close(directory);
        errno = ENODEV;
        return -1;
    }
    const int descriptor = openat(directory, attribute, O_WRONLY | O_CLOEXEC | O_TRUNC | O_NOFOLLOW);
    const int failure    = errno;
    close(directory);
    if (descriptor < 0) errno = failure;
    return descriptor;
}

bool valid_cycle_status(const std::string &status)
{
    return status == "Charging" || status == "Discharging" || status == "Not charging" || status == "Full";
}

bool health_fault(const std::string &health)
{
    return !health.empty() && health != "Unknown" && health != "Good";
}

}  // namespace

BatteryModel::BatteryModel(std::string power_supply_root) : power_supply_root_(std::move(power_supply_root))
{
}

void BatteryModel::refresh()
{
    battery_                = {};
    charger_                = {};
    capacity_configuration_ = {};
    battery_path_.clear();
    charger_path_.clear();
    std::error_code error;
    std::vector<std::filesystem::path> paths;
    for (std::filesystem::directory_iterator it(power_supply_root_, error), end; !error && it != end;
         it.increment(error)) {
        std::error_code entry_error;
        if (it->is_directory(entry_error)) paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto &path : paths) {
        const auto type = read_text(path / "type");
        if (type == "Battery") {
            const auto present = read_integer(path / "present", 0, 1);
            if (present && *present == 0) continue;
            // Prefer the on-board gauge over a peripheral battery. A generic
            // battery remains useful on simulators and other host systems.
            if (read_text(path / "scope") == "Device") continue;
            if (battery_path_.empty() || (is_chip(path, "bq27220") && !is_chip(battery_path_, "bq27220")))
                battery_path_ = path.string();
        } else if (charger_path_.empty() && is_chip(path, "bq25890")) {
            charger_path_ = path.string();
        }
    }

    if (!battery_path_.empty()) {
        const std::filesystem::path path(battery_path_);
        battery_.available   = true;
        battery_.supply_name = path.filename().string();
        battery_.identity    = device_identity(battery_path_, battery_.control_token);
        battery_.model_name  = read_text(path / "model_name");
        if (battery_.model_name.empty() && is_chip(path, "bq27220")) battery_.model_name = "BQ27220";
        battery_.status           = text_or_unknown(path / "status");
        battery_.health           = text_or_unknown(path / "health");
        battery_.technology       = text_or_unknown(path / "technology");
        const auto capacity       = read_integer(path / "capacity", 0, 100);
        battery_.has_capacity     = capacity.has_value();
        battery_.capacity_percent = static_cast<int>(capacity.value_or(0));
        const auto cycles         = read_integer(path / "cycle_count");
        battery_.has_cycle_count  = cycles.has_value();
        battery_.cycle_count      = static_cast<int>(cycles.value_or(0));
        read_value(path / "voltage_now", battery_.voltage_uv, battery_.has_voltage);
        read_value(path / "current_now", battery_.current_ua, battery_.has_current, std::numeric_limits<int>::min());
        read_value(path / "temp", battery_.temperature_tenths_c, battery_.has_temperature,
                   std::numeric_limits<int>::min());
        read_value(path / "charge_full", battery_.charge_full_uah, battery_.has_charge_full);
        read_value(path / "charge_full_design", battery_.charge_full_design_uah, battery_.has_charge_full_design);
        read_value(path / "charge_now", battery_.charge_now_uah, battery_.has_charge_now);
        read_value(path / "time_to_empty_now", battery_.time_to_empty_seconds, battery_.has_time_to_empty);
        read_value(path / "time_to_full_now", battery_.time_to_full_seconds, battery_.has_time_to_full);
        if (is_chip(path, "bq27220"))
            capacity_configuration_ = read_capacity_configuration(path / "calibration_capacity");
    }
    if (!charger_path_.empty()) {
        const std::filesystem::path path(charger_path_);
        charger_.available   = true;
        charger_.supply_name = path.filename().string();
        charger_.identity    = device_identity(charger_path_, charger_.control_token);
        charger_.model_name  = read_text(path / "model_name");
        if (charger_.model_name.empty()) charger_.model_name = "BQ25890";
        charger_.status      = text_or_unknown(path / "status");
        charger_.health      = text_or_unknown(path / "health");
        charger_.charge_type = text_or_unknown(path / "charge_type");
        const auto online    = read_integer(path / "online", 0, 1);
        charger_.has_online  = online.has_value();
        charger_.online      = online.value_or(0) != 0;

        // These ranges and the constant-current setpoint ABI are provided by
        // the target BQ25890 driver. The maximum charge current is its battery
        // limit from device tree, never the chip's 5 A capability.
        const auto current_max       = read_integer(path / "constant_charge_current_max", 0, 5056000);
        charger_.charge_current      = read_control(path / "constant_charge_current", 0, current_max.value_or(0), 64000,
                                                    current_max.has_value() && *current_max > 0);
        charger_.input_current_limit = read_control(path / "input_current_limit", 100000, 3250000, 50000, true);
        // Read voltage for diagnosis. Editing it requires a battery profile
        // workflow, which this user-facing page deliberately does not provide.
        const auto voltage_max = read_integer(path / "constant_charge_voltage_max", 3840000, 4608000);
        charger_.charge_voltage =
            read_control(path / "constant_charge_voltage", 3840000, voltage_max.value_or(0), 16000, false);
    }
    update_calibration();
}

const BatteryState &BatteryModel::battery() const noexcept
{
    return battery_;
}

const ChargerState &BatteryModel::charger() const noexcept
{
    return charger_;
}

const BatteryCapacityConfiguration &BatteryModel::capacity_configuration() const noexcept
{
    return capacity_configuration_;
}

BatteryOperationResult BatteryModel::set_capacity_configuration(int design_mah, int full_mah,
                                                                const std::string &expected_token)
{
    const auto expected_identity = battery_.identity;
    const auto requested_token   = expected_token;
    refresh();
    if ((!expected_identity.empty() && battery_.identity != expected_identity) ||
        (!requested_token.empty() && battery_.control_token != requested_token))
        return {false, "The fuel gauge changed. Review its capacities before applying a calibration."};
    if (!capacity_configuration_.available) return {false, "Capacity calibration requires the updated BQ27220 driver."};
    if (!capacity_configuration_.writable) return {false, "The fuel gauge driver does not allow capacity changes."};
    if (design_mah < capacity_configuration_.minimum || design_mah > capacity_configuration_.maximum ||
        full_mah < capacity_configuration_.minimum || full_mah > capacity_configuration_.maximum)
        return {false, "Design and full-charge capacity must each be 1 to 32767 mAh."};

    const auto identity = battery_.identity;
    const int fd        = open_control(battery_path_, "calibration_capacity", battery_.control_token);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) return {false, "Administrator authorization is required."};
        return {false, "Could not open the capacity calibration control. Refresh and try again."};
    }
    const auto value = std::to_string(design_mah) + " " + std::to_string(full_mah) + "\n";
    ssize_t written;
    do {
        written = write(fd, value.data(), value.size());
    } while (written < 0 && errno == EINTR);
    const int closed = close(fd);
    refresh();
    if (written != static_cast<ssize_t>(value.size()) || closed != 0)
        return {false, "The fuel gauge rejected the capacity update. Review the current values."};
    if (!capacity_configuration_.available || battery_.identity != identity ||
        capacity_configuration_.design_mah != design_mah || capacity_configuration_.full_mah != full_mah)
        return {false, "The fuel gauge did not confirm both capacity values. Refresh before trying again."};
    if (calibration_.active) {
        calibration_.active  = false;
        calibration_.phase   = BatteryCalibrationPhase::interrupted;
        calibration_.message = "Guided cycle stopped because the gauge capacity configuration changed.";
    }
    return {true, "Design and full-charge capacities applied and verified. The gauge is restarting estimation."};
}

const BatteryCalibrationState &BatteryModel::calibration() const noexcept
{
    return calibration_;
}

BatteryOperationResult BatteryModel::write_control(const std::string &attribute, const ChargeControl &control,
                                                   std::int64_t value)
{
    if (!charger_.available || !control.available) return {false, "This charger control is not available."};
    if (!control.writable) return {false, "The charger driver does not allow this change."};
    if (value < control.minimum || value > control.maximum || control.step <= 0 ||
        (value - control.minimum) % control.step != 0)
        return {false, "Choose a value within the charger limits and supported step size."};
    const auto path     = std::filesystem::path(charger_path_) / attribute;
    const auto identity = charger_.identity;
    const int fd        = open_control(charger_path_, attribute.c_str(), charger_.control_token);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) return {false, "Administrator authorization is required."};
        return {false, "Could not open the charger control. Refresh and try again."};
    }
    const auto text = std::to_string(value) + "\n";
    ssize_t written;
    do {
        written = write(fd, text.data(), text.size());
    } while (written < 0 && errno == EINTR);
    const int closed = close(fd);
    if (written != static_cast<ssize_t>(text.size()) || closed != 0) {
        refresh();
        return {false, "The charger rejected the change. Its current setting has been refreshed."};
    }
    const auto readback = read_integer(path);
    refresh();
    if (!readback || *readback != value || charger_.identity != identity)
        return {false, "The charger did not confirm the requested value. Review the current setting."};
    return {true, "Charger setting applied and verified."};
}

BatteryOperationResult BatteryModel::set_input_current_limit(std::int64_t microamps, const std::string &expected_token)
{
    const auto expected_identity = charger_.identity;
    const auto requested_token   = expected_token;
    refresh();
    if ((!expected_identity.empty() && charger_.identity != expected_identity) ||
        (!requested_token.empty() && charger_.control_token != requested_token))
        return {false, "The charger changed. Review its limits before applying a change."};
    return write_control("input_current_limit", charger_.input_current_limit, microamps);
}

BatteryOperationResult BatteryModel::set_charge_current(std::int64_t microamps, const std::string &expected_token)
{
    const auto expected_identity = charger_.identity;
    const auto requested_token   = expected_token;
    refresh();
    if ((!expected_identity.empty() && charger_.identity != expected_identity) ||
        (!requested_token.empty() && charger_.control_token != requested_token))
        return {false, "The charger changed. Review its limits before applying a change."};
    return write_control("constant_charge_current", charger_.charge_current, microamps);
}

BatteryOperationResult BatteryModel::start_calibration()
{
    refresh();
    if (calibration_.active) return {false, "A guided battery cycle is already active."};
    if (!calibration_.available) return {false, "A BQ27220 fuel gauge with valid battery readings is required."};
    if (health_fault(battery_.health) || health_fault(charger_.health))
        return {false, "Resolve the battery or charger fault before starting a guided cycle."};
    calibration_.active           = true;
    calibration_.phase            = BatteryCalibrationPhase::charge;
    calibration_battery_identity_ = battery_.identity;
    discharge_observed_           = false;
    update_calibration();
    return {true, "Guided cycle started. Keep Settings running to track progress."};
}

void BatteryModel::cancel_calibration()
{
    calibration_.active  = false;
    calibration_.phase   = BatteryCalibrationPhase::idle;
    calibration_.message = "Guided cycle cancelled. Charging settings were not changed.";
    calibration_battery_identity_.clear();
    discharge_observed_ = false;
}

void BatteryModel::update_calibration()
{
    calibration_.available = battery_.available && is_chip(battery_path_, "bq27220") && battery_.has_capacity &&
                             valid_cycle_status(battery_.status);
    if (!calibration_.active) {
        if (calibration_.message.empty())
            calibration_.message =
                "Track a full charge, normal use down to 10%, and a recharge. "
                "This cannot confirm the gauge's capacity learning or replace factory calibration.";
        return;
    }
    if (!calibration_.available || battery_.identity != calibration_battery_identity_ ||
        health_fault(battery_.health) || health_fault(charger_.health)) {
        calibration_.active = false;
        calibration_.phase  = BatteryCalibrationPhase::interrupted;
        calibration_.message =
            "Cycle interrupted: battery readings changed, became unavailable, or reported a fault. "
            "No calibration result was recorded.";
        return;
    }
    switch (calibration_.phase) {
        case BatteryCalibrationPhase::charge:
            calibration_.message =
                "Connect power and wait until the fuel gauge reports Full. "
                "Keep Settings running; charging remains under hardware control.";
            if (battery_.status != "Full") break;
            calibration_.phase = BatteryCalibrationPhase::discharge;
            [[fallthrough]];
        case BatteryCalibrationPhase::discharge:
            calibration_.message =
                "Unplug external power and use the device normally. Reconnect at 10%. "
                "Do not force a shutdown or deep discharge.";
            if (battery_.status == "Discharging") discharge_observed_ = true;
            if (discharge_observed_ && battery_.capacity_percent <= 10) {
                calibration_.phase   = BatteryCalibrationPhase::recharge;
                calibration_.message = "Connect power now and charge until the fuel gauge reports Full.";
            } else if (discharge_observed_ && battery_.status == "Charging") {
                calibration_.active = false;
                calibration_.phase  = BatteryCalibrationPhase::interrupted;
                calibration_.message =
                    "Cycle interrupted by charging before the low-battery checkpoint. "
                    "No calibration result was recorded.";
            }
            break;
        case BatteryCalibrationPhase::recharge:
            calibration_.message = "Connect power and charge until the fuel gauge reports Full.";
            if (battery_.status == "Full") {
                calibration_.active = false;
                calibration_.phase  = BatteryCalibrationPhase::complete;
                calibration_.message =
                    "Charge cycle observed. The driver does not report whether capacity learning "
                    "succeeded; this is not a factory calibration result.";
            }
            break;
        default:
            break;
    }
}

}  // namespace lilygo::settings
