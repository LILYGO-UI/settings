#ifndef LILYGO_UI_SETTINGS_PAGES_BATTERY_BATTERY_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_BATTERY_BATTERY_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "components/settings_theme.hpp"
#include "domain/settings_model.hpp"
#include "domain/settings_command_runner.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace lilygo::settings {

struct BatteryControlPresentation {
    bool available       = false;
    bool enabled         = false;
    bool pending         = false;
    int position         = 0;
    int maximum_position = 1;
    std::string value;
    std::string range;
};

struct BatteryCapacityPresentation {
    std::string design;
    std::string full;
    std::string message;
    std::string confirmation;
    bool enabled           = false;
    bool pending           = false;
    bool can_apply         = false;
    bool show_confirmation = false;
};

struct BatteryPresentation {
    std::string percent;
    std::string status;
    std::string technology;
    std::string health;
    std::string capacity;
    std::string cycles;
    std::string voltage;
    std::string current;
    std::string temperature;
    int bar_value           = 0;
    std::uint32_t bar_color = components::theme::battery_healthy.rgb;
    std::string remaining_charge;
    std::string full_charge;
    std::string design_charge;
    std::string gauge;
    std::string charger;
    std::string charger_status;
    std::string charge_voltage;
    std::string charging_message;
    BatteryCapacityPresentation capacity_configuration;
    BatteryControlPresentation input_current;
    BatteryControlPresentation charge_current;
    std::string calibration_description;
    std::string calibration_message;
    bool calibration_enabled      = false;
    bool calibration_confirmation = false;
    bool calibration_active       = false;
};

class BatteryViewModel {
public:
    explicit BatteryViewModel(SettingsModel &model) noexcept;
    void publish();
    void poll_command();
    void stop() noexcept;
    [[nodiscard]] bool busy() const noexcept;
    void stage_capacity(std::string design, std::string full);
    void request_capacity_apply();
    void confirm_capacity_apply();
    void cancel_capacity_apply();
    void stage_input_current(int position);
    void stage_charge_current(int position);
    void apply_input_current();
    void apply_charge_current();
    void request_calibration();
    void cancel_calibration();
    void confirm_calibration();
    [[nodiscard]] const BatteryPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    [[nodiscard]] bool capacity_control_enabled() const noexcept;
    [[nodiscard]] bool control_enabled(const ChargeControl &control) const noexcept;
    void apply_control(bool input);
    SettingsModel &model_;
    BatteryPresentation presentation_;
    std::optional<std::int64_t> input_current_draft_;
    std::optional<std::int64_t> charge_current_draft_;
    std::optional<std::string> design_capacity_draft_;
    std::optional<std::string> full_capacity_draft_;
    std::string capacity_draft_identity_;
    std::string capacity_confirmation_identity_;
    std::string current_draft_identity_;
    std::string pending_device_identity_;
    std::string capacity_message_;
    bool capacity_confirmation_ = false;
    std::optional<std::pair<int, int>> pending_capacity_;
    std::string charging_message_;
    std::string calibration_message_;
    bool calibration_confirmation_ = false;
    SettingsCommandRunner commands_;
    std::optional<std::int64_t> pending_value_;
    bool pending_input_ = false;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
