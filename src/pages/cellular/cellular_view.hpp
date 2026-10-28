#ifndef LILYGO_UI_SETTINGS_PAGES_CELLULAR_CELLULAR_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_CELLULAR_CELLULAR_VIEW_HPP

#include "pages/cellular/cellular_view_model.hpp"

#include <lvgl.h>

namespace lilygo::settings {

class CellularView {
public:
    explicit CellularView(CellularViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    static void enabled_changed(lv_event_t *event);
    static void layout_changed(lv_event_t *event);
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    void render() noexcept;

    CellularViewModel &view_model_;
    lv_obj_t *root_         = nullptr;
    lv_obj_t *control_      = nullptr;
    lv_obj_t *status_       = nullptr;
    lv_obj_t *sim_status_   = nullptr;
    lv_obj_t *iccid_        = nullptr;
    lv_obj_t *imsi_         = nullptr;
    lv_obj_t *operator_     = nullptr;
    lv_obj_t *technology_   = nullptr;
    lv_obj_t *signal_       = nullptr;
    lv_obj_t *interface_    = nullptr;
    lv_obj_t *received_     = nullptr;
    lv_obj_t *transmitted_  = nullptr;
    lv_obj_t *total_        = nullptr;
    lv_obj_t *usage_note_   = nullptr;
    bool updating_controls_ = false;
};

}  // namespace lilygo::settings

#endif
