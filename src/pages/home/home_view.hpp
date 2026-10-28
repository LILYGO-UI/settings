#ifndef LILYGO_UI_SETTINGS_PAGES_HOME_HOME_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_HOME_HOME_VIEW_HPP

#include "pages/home/home_view_model.hpp"

#include <lvgl.h>

namespace lilygo::settings {

class HomeView {
public:
    explicit HomeView(HomeViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void set_landscape(bool landscape) noexcept;
    void destroy() noexcept;

private:
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    void render() noexcept;

    HomeViewModel &view_model_;
    lv_obj_t *root_      = nullptr;
    lv_obj_t *wifi_      = nullptr;
    lv_obj_t *bluetooth_ = nullptr;
    lv_obj_t *ethernet_  = nullptr;
    lv_obj_t *cellular_  = nullptr;
    lv_obj_t *battery_   = nullptr;
    lv_obj_t *connectivity_ = nullptr;
    lv_obj_t *general_group_ = nullptr;
    lv_obj_t *footer_ = nullptr;
    lv_obj_t *keyboard_group_ = nullptr;
    lv_obj_t *landscape_welcome_ = nullptr;
    lv_obj_t *landscape_status_ = nullptr;
    bool landscape_ = false;
};

}  // namespace lilygo::settings

#endif
