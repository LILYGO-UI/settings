#ifndef LILYGO_UI_SETTINGS_PAGES_SHARING_SHARING_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_SHARING_SHARING_VIEW_HPP

#include "pages/sharing/sharing_view_model.hpp"

#include <lvgl.h>

namespace lilygo::settings {

class SharingView {
public:
    explicit SharingView(SharingViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent, lv_obj_t *modal_parent);
    void destroy() noexcept;

private:
    static void enabled_changed(lv_event_t *event);
    static void serial_enabled_changed(lv_event_t *event);
    static void reboot_now_clicked(lv_event_t *event);
    static void reboot_later_clicked(lv_event_t *event);
    void create_reboot_dialog(lv_obj_t *parent);
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    void render() noexcept;
    SharingViewModel &view_model_;
    lv_obj_t *root_           = nullptr;
    lv_obj_t *control_        = nullptr;
    lv_obj_t *status_         = nullptr;
    lv_obj_t *serial_control_ = nullptr;
    lv_obj_t *serial_status_  = nullptr;
    lv_obj_t *reboot_overlay_ = nullptr;
    lv_obj_t *reboot_now_     = nullptr;
    lv_obj_t *reboot_later_   = nullptr;
    lv_obj_t *reboot_error_   = nullptr;
};

}  // namespace lilygo::settings

#endif
