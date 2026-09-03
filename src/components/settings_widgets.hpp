#ifndef LILYGO_UI_SETTINGS_COMPONENTS_SETTINGS_WIDGETS_HPP
#define LILYGO_UI_SETTINGS_COMPONENTS_SETTINGS_WIDGETS_HPP

#include "components/settings_theme.hpp"

#include <lvgl.h>

namespace lilygo::settings::components {

// All returned pointers are non-owning. LVGL's parent object owns the created
// widgets; these factories do not register callbacks, observers, or timers.
struct DetailHeader {
    lv_obj_t *root  = nullptr;
    lv_obj_t *back  = nullptr;
    lv_obj_t *title = nullptr;
};

struct ToggleRow {
    lv_obj_t *root    = nullptr;
    lv_obj_t *title   = nullptr;
    lv_obj_t *control = nullptr;
};

struct MenuRow {
    lv_obj_t *root    = nullptr;
    lv_obj_t *icon    = nullptr;
    lv_obj_t *title   = nullptr;
    lv_obj_t *detail  = nullptr;
    lv_obj_t *chevron = nullptr;
    lv_obj_t *divider = nullptr;
};

struct ValueRow {
    lv_obj_t *root    = nullptr;
    lv_obj_t *title   = nullptr;
    lv_obj_t *value   = nullptr;
    lv_obj_t *divider = nullptr;
};

[[nodiscard]] bool fonts_available() noexcept;
[[nodiscard]] lv_obj_t *create_label(lv_obj_t *parent, const char *text, std::uint32_t size,
                                     theme::SemanticColor color = theme::text, const char *name = nullptr);
[[nodiscard]] lv_obj_t *create_page(lv_obj_t *parent, const char *name);
[[nodiscard]] lv_obj_t *create_surface(lv_obj_t *parent, const char *name = nullptr);
[[nodiscard]] lv_obj_t *create_group(lv_obj_t *parent, const char *name = nullptr);
[[nodiscard]] lv_obj_t *create_section_label(lv_obj_t *parent, const char *text, const char *name = nullptr);
[[nodiscard]] DetailHeader create_detail_header(lv_obj_t *parent, const char *title, const char *back_name = nullptr);
[[nodiscard]] ToggleRow create_toggle_row(lv_obj_t *parent, const char *title, const char *row_name = nullptr,
                                          const char *control_name = nullptr, bool checked = false);
[[nodiscard]] MenuRow create_menu_row(lv_obj_t *parent, const char *icon, const char *title, const char *detail,
                                      bool show_divider, bool show_chevron, const char *name = nullptr);
[[nodiscard]] MenuRow create_image_menu_row(lv_obj_t *parent, const lv_image_dsc_t *icon, const char *title,
                                            const char *detail, bool show_divider, bool show_chevron,
                                            const char *name = nullptr);
[[nodiscard]] ValueRow create_value_row(lv_obj_t *parent, const char *title, const char *value,
                                        theme::SemanticColor value_color = theme::text, bool show_divider = true,
                                        const char *name = nullptr);

// Direct children are stable: status, title, subtitle, lock, trailing, divider.
[[nodiscard]] lv_obj_t *create_dynamic_row(lv_obj_t *parent, const char *title, const char *subtitle,
                                           const char *trailing, bool connected, bool locked);

}  // namespace lilygo::settings::components

#endif
