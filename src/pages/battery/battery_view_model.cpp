#include "pages/battery/battery_view_model.hpp"

#include "app_identity.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <limits>
#include <unistd.h>

namespace lilygo::settings {
namespace {

std::string milliamps(std::int64_t value)
{
    return std::to_string(value / 1000) + " mA";
}

std::string milliamp_hours(bool available, std::int64_t value)
{
    if (!available) return "--";
    char text[64];
    std::snprintf(text, sizeof(text), "%.0f mAh", static_cast<double>(value) / 1000.0);
    return text;
}

std::optional<int> capacity_number(const std::string &text, int minimum, int maximum)
{
    int value         = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value < minimum || value > maximum)
        return std::nullopt;
    return value;
}

bool valid_range(const ChargeControl &control)
{
    return control.step > 0 && control.maximum > control.minimum &&
           (control.maximum - control.minimum) / control.step <= std::numeric_limits<int>::max();
}

BatteryControlPresentation present_control(const ChargeControl &control, std::optional<std::int64_t> &draft,
                                           bool enabled, bool busy)
{
    BatteryControlPresentation result;
    result.available = control.available;
    result.enabled   = enabled;
    if (!control.available || !valid_range(control) || !control.writable ||
        (draft && (*draft < control.minimum || *draft > control.maximum || *draft == control.value)))
        draft.reset();
    const auto value = draft.value_or(control.value);
    result.pending   = draft.has_value();
    result.value     = control.available ? milliamps(value) : "--";
    if (result.pending) result.value += " (not applied)";
    if (valid_range(control)) {
        result.maximum_position = static_cast<int>((control.maximum - control.minimum) / control.step);
        result.position         = static_cast<int>(std::clamp((value - control.minimum) / control.step, std::int64_t{0},
                                                              std::int64_t{result.maximum_position}));
    }
    if (!control.writable || !valid_range(control))
        result.range = "Read-only: the driver does not support changes.";
    else if (!enabled && control.requires_authorization && !busy)
        result.range = "Read-only: permission to change this limit is unavailable.";
    else
        result.range = milliamps(control.minimum) + " to " + milliamps(control.maximum) + "; " +
                       milliamps(control.step) + " steps";
    return result;
}

std::int64_t position_value(const ChargeControl &control, int position)
{
    const auto steps = (control.maximum - control.minimum) / control.step;
    return control.minimum + std::clamp<std::int64_t>(position, 0, steps) * control.step;
}

}  // namespace

BatteryViewModel::BatteryViewModel(SettingsModel &model) noexcept : model_(model)
{
}

bool BatteryViewModel::control_enabled(const ChargeControl &control) const noexcept
{
    if (!control.available || !control.writable || !valid_range(control) || commands_.busy()) return false;
    if (!control.requires_authorization) return true;
    // Privileged writes always use the installed helper's fixed system root.
    // Test/custom roots never become inputs to a privileged filesystem write.
    return model_.environment().power_supply_root == "/sys/class/power_supply" && !model_.environment().sudo.empty() &&
           access(model_.environment().sudo.c_str(), X_OK) == 0 && access(LILYGO_UI_BATTERY_HELPER_PATH, X_OK) == 0;
}

bool BatteryViewModel::capacity_control_enabled() const noexcept
{
    const auto &configuration = model_.battery_details().capacity_configuration();
    if (!configuration.available || !configuration.writable || commands_.busy()) return false;
    if (model_.environment().power_supply_root != "/sys/class/power_supply")
        return !configuration.requires_authorization;
    return access(LILYGO_UI_BATTERY_HELPER_PATH, X_OK) == 0 &&
           (!configuration.requires_authorization ||
            (!model_.environment().sudo.empty() && access(model_.environment().sudo.c_str(), X_OK) == 0));
}

void BatteryViewModel::publish()
{
    const auto &battery = model_.battery();
    if (!capacity_draft_identity_.empty() && capacity_draft_identity_ != battery.identity) {
        design_capacity_draft_.reset();
        full_capacity_draft_.reset();
        capacity_draft_identity_.clear();
        capacity_confirmation_identity_.clear();
        capacity_confirmation_ = false;
        capacity_message_      = "The battery changed. Review its capacities before calibrating.";
    }
    char text[96];
    presentation_.percent   = "--";
    presentation_.bar_value = 0;
    if (battery.available && battery.has_capacity) {
        std::snprintf(text, sizeof(text), "%d%%", battery.capacity_percent);
        presentation_.percent   = text;
        presentation_.bar_value = std::clamp(battery.capacity_percent, 0, 100);
    }
    presentation_.bar_color =
        battery.has_capacity && battery.capacity_percent < 20   ? components::theme::battery_critical.rgb
        : battery.has_capacity && battery.capacity_percent < 50 ? components::theme::battery_low.rgb
                                                                : components::theme::battery_healthy.rgb;
    presentation_.status =
        battery.available ? (battery.status.empty() ? "Status Unavailable" : battery.status) : "No Battery Detected";
    presentation_.technology = battery.technology.empty() ? "--" : battery.technology;
    presentation_.health     = battery.health.empty() ? "--" : battery.health;
    presentation_.gauge      = battery.model_name.empty() ? battery.supply_name : battery.model_name;
    if (presentation_.gauge.empty()) presentation_.gauge = "--";

    if (battery.has_charge_full && battery.has_charge_full_design && battery.charge_full_design_uah > 0) {
        const auto health = std::clamp(
            100.0 * static_cast<double>(battery.charge_full_uah) / static_cast<double>(battery.charge_full_design_uah),
            0.0, 100.0);
        std::snprintf(text, sizeof(text), "%.0f%%", health);
        presentation_.capacity = text;
    } else
        presentation_.capacity = "--";
    presentation_.remaining_charge = milliamp_hours(battery.has_charge_now, battery.charge_now_uah);
    presentation_.full_charge      = milliamp_hours(battery.has_charge_full, battery.charge_full_uah);
    presentation_.design_charge    = milliamp_hours(battery.has_charge_full_design, battery.charge_full_design_uah);
    presentation_.cycles           = battery.has_cycle_count ? std::to_string(battery.cycle_count) : "--";
    if (battery.has_voltage) {
        std::snprintf(text, sizeof(text), "%.3f V", static_cast<double>(battery.voltage_uv) / 1000000.0);
        presentation_.voltage = text;
    } else
        presentation_.voltage = "--";
    if (battery.has_current) {
        std::snprintf(text, sizeof(text), "%+.3f A", static_cast<double>(battery.current_ua) / 1000000.0);
        presentation_.current = text;
    } else
        presentation_.current = "--";
    if (battery.has_temperature) {
        std::snprintf(text, sizeof(text), "%.1f C", static_cast<double>(battery.temperature_tenths_c) / 10.0);
        presentation_.temperature = text;
    } else
        presentation_.temperature = "--";

    const auto &charger = model_.battery_details().charger();
    if (!current_draft_identity_.empty() && current_draft_identity_ != charger.identity) {
        input_current_draft_.reset();
        charge_current_draft_.reset();
        current_draft_identity_.clear();
        charging_message_ = "The charger changed. Review its current limits before applying changes.";
    }
    presentation_.charger = !charger.available           ? "Charger Unavailable"
                            : charger.model_name.empty() ? charger.supply_name
                                                         : charger.model_name;
    if (charger.available) {
        presentation_.charger_status = charger.status.empty() ? "Status unavailable" : charger.status;
        if (charger.has_online)
            presentation_.charger_status +=
                charger.online ? " · External power connected" : " · External power disconnected";
    } else {
        presentation_.charger_status = "Charging controls are unavailable on this device.";
    }
    presentation_.charge_voltage.clear();
    if (charger.charge_voltage.available) {
        std::snprintf(text, sizeof(text), "Charge voltage limit: %.3f V",
                      static_cast<double>(charger.charge_voltage.value) / 1000000.0);
        presentation_.charge_voltage = text;
    }
    presentation_.input_current    = present_control(charger.input_current_limit, input_current_draft_,
                                                     control_enabled(charger.input_current_limit), commands_.busy());
    presentation_.charge_current   = present_control(charger.charge_current, charge_current_draft_,
                                                     control_enabled(charger.charge_current), commands_.busy());
    presentation_.charging_message = charging_message_;
    if (commands_.busy() && !pending_capacity_)
        presentation_.charging_message = "Applying charging limit...";
    else if (presentation_.charging_message.empty() && charger.available && !charger.input_current_limit.available &&
             !charger.charge_current.available)
        presentation_.charging_message = "The charger driver does not expose adjustable current limits.";

    const auto &configuration = model_.battery_details().capacity_configuration();
    auto &capacity            = presentation_.capacity_configuration;
    capacity.enabled          = capacity_control_enabled();
    const auto default_design = configuration.available          ? configuration.design_mah
                                : battery.has_charge_full_design ? battery.charge_full_design_uah / 1000
                                                                 : 0;
    const auto default_full   = configuration.available   ? configuration.full_mah
                                : battery.has_charge_full ? battery.charge_full_uah / 1000
                                                          : 0;
    capacity.design   = design_capacity_draft_.value_or(default_design > 0 ? std::to_string(default_design) : "");
    capacity.full     = full_capacity_draft_.value_or(default_full > 0 ? std::to_string(default_full) : "");
    const auto design = capacity_number(capacity.design, configuration.minimum, configuration.maximum);
    const auto full   = capacity_number(capacity.full, configuration.minimum, configuration.maximum);
    capacity.pending  = (design_capacity_draft_.has_value() || full_capacity_draft_.has_value()) &&
                       (!design || !full || *design != configuration.design_mah || *full != configuration.full_mah);
    capacity.can_apply = capacity.enabled && capacity.pending && design.has_value() && full.has_value();
    if (!capacity.can_apply) capacity_confirmation_ = false;
    capacity.show_confirmation = capacity_confirmation_;
    capacity.confirmation =
        "Save design capacity " + capacity.design + " mAh and full-charge capacity " + capacity.full +
        " mAh? This replaces the learned full-charge capacity and restarts the gauge's charge estimate.";
    if (commands_.busy() && pending_capacity_)
        capacity.message = "Saving capacity calibration...";
    else if (!battery.available)
        capacity.message = "Capacity calibration is unavailable because no battery was detected.";
    else if (!configuration.available)
        capacity.message = "Read-only: update the BQ27220 device driver to enable capacity calibration.";
    else if (!configuration.writable)
        capacity.message = "Read-only: the device driver does not allow capacity calibration.";
    else if (!capacity.enabled && !commands_.busy())
        capacity.message = "Read-only: permission to calibrate capacity is unavailable.";
    else if (capacity.pending && (!design || !full))
        capacity.message = "Enter whole capacities from " + std::to_string(configuration.minimum) + " to " +
                           std::to_string(configuration.maximum) + " mAh.";
    else
        capacity.message = capacity_message_;

    const auto &calibration           = model_.battery_details().calibration();
    presentation_.calibration_enabled = calibration.available && !calibration.active && !commands_.busy();
    presentation_.calibration_active  = calibration.active;
    if (!calibration.available || calibration.active || commands_.busy()) calibration_confirmation_ = false;
    presentation_.calibration_confirmation = calibration_confirmation_;
    presentation_.calibration_description =
        "Optionally follow a charge / use / recharge cycle while the fuel gauge learns automatically.";
    presentation_.calibration_message = calibration_message_.empty() ? calibration.message : calibration_message_;
    revision_.publish();
}

void BatteryViewModel::stage_capacity(std::string design, std::string full)
{
    if (!capacity_control_enabled()) return;
    capacity_draft_identity_ = model_.battery().identity;
    design_capacity_draft_   = std::move(design);
    full_capacity_draft_     = std::move(full);
    capacity_confirmation_   = false;
    capacity_message_.clear();
    publish();
}

void BatteryViewModel::request_capacity_apply()
{
    if (commands_.busy()) return;
    model_.refresh_battery();
    publish();
    if (!presentation_.capacity_configuration.can_apply) return;
    capacity_confirmation_identity_ = model_.battery().identity;
    capacity_confirmation_          = true;
    publish();
}

void BatteryViewModel::cancel_capacity_apply()
{
    capacity_confirmation_ = false;
    publish();
}

void BatteryViewModel::confirm_capacity_apply()
{
    if (!capacity_confirmation_ || commands_.busy()) return;
    capacity_confirmation_ = false;
    model_.refresh_battery();
    if (capacity_draft_identity_.empty() || capacity_draft_identity_ != model_.battery().identity ||
        capacity_confirmation_identity_ != model_.battery().identity) {
        capacity_message_ = "The battery changed. Review its capacities before calibrating.";
        publish();
        return;
    }
    const auto &configuration = model_.battery_details().capacity_configuration();
    const auto design =
        capacity_number(design_capacity_draft_.value_or(""), configuration.minimum, configuration.maximum);
    const auto full = capacity_number(full_capacity_draft_.value_or(""), configuration.minimum, configuration.maximum);
    if (!capacity_control_enabled() || !design || !full) {
        capacity_message_ = "Capacity calibration is unavailable or its permitted range has changed.";
        publish();
        return;
    }
    const std::string expected_token = model_.battery().control_token;
    if (*design == configuration.design_mah && *full == configuration.full_mah) {
        design_capacity_draft_.reset();
        full_capacity_draft_.reset();
        capacity_message_ = "Capacity calibration is unchanged.";
    } else if (model_.environment().power_supply_root == "/sys/class/power_supply") {
        std::vector<std::string> arguments;
        if (configuration.requires_authorization) arguments = {model_.environment().sudo, "-n"};
        arguments.insert(arguments.end(), {LILYGO_UI_BATTERY_HELPER_PATH, "capacity", std::to_string(*design),
                                           std::to_string(*full), expected_token});
        if (commands_.start(SettingsCommand::battery_control, arguments)) {
            pending_capacity_        = std::make_pair(*design, *full);
            pending_device_identity_ = model_.battery().identity;
        } else {
            capacity_message_ = "Unable to start the calibration helper. Try again.";
        }
    } else {
        const auto result = model_.battery_details().set_capacity_configuration(*design, *full, expected_token);
        capacity_message_ = result.message;
        if (result.success) {
            design_capacity_draft_.reset();
            full_capacity_draft_.reset();
        }
    }
    publish();
}

void BatteryViewModel::stage_input_current(int position)
{
    const auto &control = model_.battery_details().charger().input_current_limit;
    if (!control_enabled(control)) return;
    if (current_draft_identity_ != model_.battery_details().charger().identity) {
        input_current_draft_.reset();
        charge_current_draft_.reset();
    }
    current_draft_identity_ = model_.battery_details().charger().identity;
    input_current_draft_    = position_value(control, position);
    charging_message_.clear();
    publish();
}

void BatteryViewModel::stage_charge_current(int position)
{
    const auto &control = model_.battery_details().charger().charge_current;
    if (!control_enabled(control)) return;
    if (current_draft_identity_ != model_.battery_details().charger().identity) {
        input_current_draft_.reset();
        charge_current_draft_.reset();
    }
    current_draft_identity_ = model_.battery_details().charger().identity;
    charge_current_draft_   = position_value(control, position);
    charging_message_.clear();
    publish();
}

void BatteryViewModel::apply_control(bool input)
{
    if (commands_.busy()) return;
    auto &draft = input ? input_current_draft_ : charge_current_draft_;
    if (!draft) return;
    model_.refresh_battery();
    const auto &charger = model_.battery_details().charger();
    const auto &control = input ? charger.input_current_limit : charger.charge_current;
    if (current_draft_identity_.empty() || current_draft_identity_ != charger.identity) {
        charging_message_ = "The charger changed. Review its current limits before applying changes.";
        publish();
        return;
    }
    const auto value                 = *draft;
    const std::string expected_token = charger.control_token;
    if (!control_enabled(control) || value < control.minimum || value > control.maximum) {
        charging_message_ = "This charging control is unavailable or its permitted range has changed.";
        publish();
        return;
    }
    if (control.requires_authorization) {
        const std::vector<std::string> arguments{
            model_.environment().sudo,     "-n",
            LILYGO_UI_BATTERY_HELPER_PATH, input ? "input-current" : "charge-current",
            std::to_string(value),         expected_token};
        if (commands_.start(SettingsCommand::battery_control, arguments)) {
            pending_value_           = value;
            pending_input_           = input;
            pending_device_identity_ = charger.identity;
        } else {
            charging_message_ = "Unable to start the charging helper. Try again.";
        }
    } else {
        const auto result = input ? model_.battery_details().set_input_current_limit(value, expected_token)
                                  : model_.battery_details().set_charge_current(value, expected_token);
        charging_message_ = result.message;
        if (result.success) draft.reset();
    }
    publish();
}

void BatteryViewModel::apply_input_current()
{
    apply_control(true);
}

void BatteryViewModel::apply_charge_current()
{
    apply_control(false);
}

void BatteryViewModel::poll_command()
{
    const auto result = commands_.poll();
    if (!result) return;
    model_.refresh_battery();
    if (pending_capacity_) {
        const auto &configuration = model_.battery_details().capacity_configuration();
        if (pending_device_identity_ != model_.battery().identity) {
            capacity_message_ = "The battery changed during calibration. The result could not be verified.";
        } else if (result->success && configuration.available && configuration.design_mah == pending_capacity_->first &&
                   configuration.full_mah == pending_capacity_->second) {
            capacity_message_ = "Capacity calibration saved and verified.";
            if (model_.battery_details().calibration().active) model_.battery_details().cancel_calibration();
            design_capacity_draft_.reset();
            full_capacity_draft_.reset();
        } else if (result->success) {
            capacity_message_ = "The gauge reported different capacities. Check the readings before trying again.";
        } else {
            capacity_message_ = "Capacity calibration failed. Check device permissions and the fuel gauge.";
        }
        pending_capacity_.reset();
        pending_device_identity_.clear();
        publish();
        return;
    }
    const auto &charger = model_.battery_details().charger();
    const auto &control = pending_input_ ? charger.input_current_limit : charger.charge_current;
    if (pending_device_identity_ != charger.identity) {
        charging_message_ = "The charger changed while applying the limit. The result could not be verified.";
    } else if (result->success && pending_value_ && control.available && control.value == *pending_value_) {
        charging_message_ = "Charging limit applied and verified.";
        (pending_input_ ? input_current_draft_ : charge_current_draft_).reset();
    } else if (result->success) {
        charging_message_ = "The charger reported a different limit. Check the value and try again.";
    } else {
        std::string output = result->output;
        std::transform(output.begin(), output.end(), output.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        const bool denied =
            output.find("permission") != std::string::npos || output.find("denied") != std::string::npos ||
            output.find("password") != std::string::npos || output.find("not allowed") != std::string::npos;
        charging_message_ = denied ? "Permission denied. The device's charging policy does not allow this change."
                                   : "Unable to apply the charging limit. Check the charger and try again.";
    }
    pending_value_.reset();
    pending_device_identity_.clear();
    publish();
}

void BatteryViewModel::stop() noexcept
{
    commands_.stop();
    pending_value_.reset();
    pending_capacity_.reset();
    pending_device_identity_.clear();
}

bool BatteryViewModel::busy() const noexcept
{
    return commands_.busy();
}

void BatteryViewModel::request_calibration()
{
    if (commands_.busy()) return;
    model_.refresh_battery();
    const auto &calibration = model_.battery_details().calibration();
    if (!calibration.available || calibration.active || commands_.busy()) return;
    calibration_message_.clear();
    calibration_confirmation_ = true;
    publish();
}

void BatteryViewModel::cancel_calibration()
{
    if (commands_.busy()) return;
    if (model_.battery_details().calibration().active) model_.battery_details().cancel_calibration();
    calibration_confirmation_ = false;
    calibration_message_.clear();
    publish();
}

void BatteryViewModel::confirm_calibration()
{
    if (!calibration_confirmation_ || commands_.busy()) return;
    calibration_confirmation_ = false;
    const auto result         = model_.battery_details().start_calibration();
    calibration_message_      = result.success ? "" : result.message;
    publish();
}

const BatteryPresentation &BatteryViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *BatteryViewModel::revision_subject() noexcept
{
    return revision_.get();
}

}  // namespace lilygo::settings
