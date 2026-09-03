#ifndef LILYGO_UI_SETTINGS_PAGES_ABOUT_ABOUT_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_ABOUT_ABOUT_VIEW_HPP

#include "pages/about/about_view_model.hpp"

#include <lvgl.h>

namespace lilygo::settings {

class AboutView {
public:
    explicit AboutView(AboutViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    void render() noexcept;

    AboutViewModel &view_model_;
    lv_obj_t *root_         = nullptr;
    lv_obj_t *device_       = nullptr;
    lv_obj_t *model_        = nullptr;
    lv_obj_t *serial_       = nullptr;
    lv_obj_t *os_           = nullptr;
    lv_obj_t *kernel_       = nullptr;
    lv_obj_t *architecture_ = nullptr;
};

}  // namespace lilygo::settings

#endif
