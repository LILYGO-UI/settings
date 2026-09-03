#ifndef LILYGO_UI_SETTINGS_PAGES_GENERAL_GENERAL_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_GENERAL_GENERAL_VIEW_HPP

#include "pages/general/general_view_model.hpp"

#include <lvgl.h>

namespace lilygo::settings {

class GeneralView {
public:
    explicit GeneralView(GeneralViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    void render() noexcept;

    GeneralViewModel &view_model_;
    lv_obj_t *root_         = nullptr;
    lv_obj_t *about_detail_ = nullptr;
};

}  // namespace lilygo::settings

#endif
