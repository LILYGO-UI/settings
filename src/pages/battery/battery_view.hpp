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
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    void render() noexcept;

    BatteryViewModel &view_model_;
    lv_obj_t *root_        = nullptr;
    lv_obj_t *percent_     = nullptr;
    lv_obj_t *status_      = nullptr;
    lv_obj_t *technology_  = nullptr;
    lv_obj_t *bar_         = nullptr;
    lv_obj_t *health_      = nullptr;
    lv_obj_t *capacity_    = nullptr;
    lv_obj_t *cycles_      = nullptr;
    lv_obj_t *voltage_     = nullptr;
    lv_obj_t *current_     = nullptr;
    lv_obj_t *temperature_ = nullptr;
};

}  // namespace lilygo::settings

#endif
