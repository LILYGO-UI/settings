#ifndef LILYGO_UI_SETTINGS_PAGES_BATTERY_BATTERY_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_BATTERY_BATTERY_VIEW_HPP

#include "pages/battery/battery_view_model.hpp"

#include <lvgl.h>

namespace lilygo::settings {

class BatteryView {
public:
    explicit BatteryView(BatteryViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    struct ControlWidgets {
        lv_obj_t *root   = nullptr;
        lv_obj_t *value  = nullptr;
        lv_obj_t *range  = nullptr;
        lv_obj_t *slider = nullptr;
        lv_obj_t *apply  = nullptr;
    };

    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    static void capacity_input_changed(lv_event_t *event);
    static void capacity_keyboard_finished(lv_event_t *event);
    static void capacity_apply_requested(lv_event_t *event);
    static void capacity_apply_confirmed(lv_event_t *event);
    static void capacity_apply_cancelled(lv_event_t *event);
    static void input_current_changed(lv_event_t *event);
    static void charge_current_changed(lv_event_t *event);
    static void input_current_applied(lv_event_t *event);
    static void charge_current_applied(lv_event_t *event);
    static void calibration_requested(lv_event_t *event);
    static void calibration_confirmed(lv_event_t *event);
    static void calibration_cancelled(lv_event_t *event);
    void render() noexcept;

    BatteryViewModel &view_model_;
    lv_obj_t *root_                       = nullptr;
    lv_obj_t *percent_                    = nullptr;
    lv_obj_t *status_                     = nullptr;
    lv_obj_t *technology_                 = nullptr;
    lv_obj_t *bar_                        = nullptr;
    lv_obj_t *health_                     = nullptr;
    lv_obj_t *capacity_                   = nullptr;
    lv_obj_t *cycles_                     = nullptr;
    lv_obj_t *voltage_                    = nullptr;
    lv_obj_t *current_                    = nullptr;
    lv_obj_t *temperature_                = nullptr;
    lv_obj_t *remaining_charge_           = nullptr;
    lv_obj_t *full_charge_                = nullptr;
    lv_obj_t *design_charge_              = nullptr;
    lv_obj_t *gauge_                      = nullptr;
    lv_obj_t *charger_                    = nullptr;
    lv_obj_t *charger_status_             = nullptr;
    lv_obj_t *charge_voltage_             = nullptr;
    lv_obj_t *charging_message_           = nullptr;
    lv_obj_t *calibration_description_    = nullptr;
    lv_obj_t *calibration_message_        = nullptr;
    lv_obj_t *calibration_request_        = nullptr;
    lv_obj_t *calibration_confirmation_   = nullptr;
    lv_obj_t *calibration_confirm_        = nullptr;
    lv_obj_t *calibration_stop_           = nullptr;
    lv_obj_t *design_capacity_input_      = nullptr;
    lv_obj_t *full_capacity_input_        = nullptr;
    lv_obj_t *capacity_apply_             = nullptr;
    lv_obj_t *capacity_message_           = nullptr;
    lv_obj_t *capacity_confirmation_      = nullptr;
    lv_obj_t *capacity_confirmation_text_ = nullptr;
    lv_obj_t *capacity_confirm_           = nullptr;
    lv_obj_t *capacity_keyboard_          = nullptr;
    bool updating_inputs_                 = false;
    ControlWidgets input_current_;
    ControlWidgets charge_current_;
};

}  // namespace lilygo::settings

#endif
