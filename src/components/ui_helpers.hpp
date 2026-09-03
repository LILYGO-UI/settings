#ifndef LILYGO_UI_SETTINGS_COMPONENTS_UI_HELPERS_HPP
#define LILYGO_UI_SETTINGS_COMPONENTS_UI_HELPERS_HPP

#include <lvgl.h>

namespace lilygo::settings::components {

[[nodiscard]] lv_obj_t *find(lv_obj_t *root, const char *name) noexcept;
void set_text(lv_obj_t *label, const char *text) noexcept;
void set_switch(lv_obj_t *control, bool checked, bool enabled) noexcept;

}  // namespace lilygo::settings::components

#endif
