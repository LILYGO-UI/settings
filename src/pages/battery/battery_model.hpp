#ifndef LILYGO_UI_SETTINGS_PAGES_BATTERY_BATTERY_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_BATTERY_BATTERY_MODEL_HPP

#include <cstdint>
#include <string>

namespace lilygo::settings {

struct BatteryState {
    bool available = false;
    std::string status;
    std::string health;
    std::string technology;
    int capacity_percent                = 0;
    int cycle_count                     = 0;
    std::int64_t voltage_uv             = 0;
    std::int64_t current_ua             = 0;
    std::int64_t temperature_tenths_c   = 0;
    std::int64_t charge_full_uah        = 0;
    std::int64_t charge_full_design_uah = 0;
    bool has_capacity                   = false;
    bool has_cycle_count                = false;
    bool has_voltage                    = false;
    bool has_current                    = false;
    bool has_temperature                = false;
    bool has_charge_full                = false;
    bool has_charge_full_design         = false;
    std::string supply_name;
    std::string model_name;
    std::string identity;
    std::string control_token;
    std::int64_t charge_now_uah        = 0;
    std::int64_t time_to_empty_seconds = 0;
    std::int64_t time_to_full_seconds  = 0;
    bool has_charge_now                = false;
    bool has_time_to_empty             = false;
    bool has_time_to_full              = false;
};

// Values use the Linux power_supply ABI: microamps / microvolts.
// A readable control may still be unsupported for writes by the driver.
struct ChargeControl {
    bool available = false;
    // Driver write support is distinct from the application's permissions.
    bool writable               = false;
    bool requires_authorization = false;
    std::int64_t value          = 0;
    std::int64_t minimum        = 0;
    std::int64_t maximum        = 0;
    std::int64_t step           = 0;
};

struct ChargerState {
    bool available = false;
    std::string supply_name;
    std::string model_name;
    std::string identity;
    std::string control_token;
    std::string status;
    std::string health;
    std::string charge_type;
    bool has_online = false;
    bool online     = false;
    ChargeControl input_current_limit;
    ChargeControl charge_current;
    ChargeControl charge_voltage;
};

struct BatteryOperationResult {
    bool success = false;
    std::string message;
};

// Board-driver extension. Both visible capacities are written together in
// mAh; the driver handles profile conversion and one config-update transaction.
struct BatteryCapacityConfiguration {
    bool available              = false;
    bool writable               = false;
    bool requires_authorization = false;
    int design_mah              = 0;
    int full_mah                = 0;
    int minimum                 = 1;
    int maximum                 = 32767;
};

enum class BatteryCalibrationPhase { idle, charge, discharge, recharge, complete, interrupted };

struct BatteryCalibrationState {
    bool available                = false;
    bool active                   = false;
    BatteryCalibrationPhase phase = BatteryCalibrationPhase::idle;
    std::string message;
};

class BatteryModel {
public:
    explicit BatteryModel(std::string power_supply_root);
    void refresh();
    [[nodiscard]] const BatteryState &battery() const noexcept;
    [[nodiscard]] const ChargerState &charger() const noexcept;
    [[nodiscard]] const BatteryCapacityConfiguration &capacity_configuration() const noexcept;
    [[nodiscard]] BatteryOperationResult set_capacity_configuration(int design_mah, int full_mah,
                                                                    const std::string &expected_token = {});
    [[nodiscard]] const BatteryCalibrationState &calibration() const noexcept;
    [[nodiscard]] BatteryOperationResult start_calibration();
    void cancel_calibration();
    [[nodiscard]] BatteryOperationResult set_input_current_limit(std::int64_t microamps,
                                                                 const std::string &expected_token = {});
    [[nodiscard]] BatteryOperationResult set_charge_current(std::int64_t microamps,
                                                            const std::string &expected_token = {});

private:
    void update_calibration();
    [[nodiscard]] BatteryOperationResult write_control(const std::string &attribute, const ChargeControl &control,
                                                       std::int64_t value);
    std::string power_supply_root_;
    std::string battery_path_;
    std::string charger_path_;
    std::string calibration_battery_identity_;
    BatteryState battery_;
    ChargerState charger_;
    BatteryCapacityConfiguration capacity_configuration_;
    BatteryCalibrationState calibration_;
    bool discharge_observed_ = false;
};

}  // namespace lilygo::settings

#endif
