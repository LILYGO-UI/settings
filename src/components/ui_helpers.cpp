#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

namespace lilygo::settings::components {

lv_obj_t *find(lv_obj_t *root, const char *name) noexcept
{
    return root ? lv_obj_find_by_name(root, name) : nullptr;
}

void set_text(lv_obj_t *label, const char *text) noexcept
{
    if (label) lv_label_set_text(label, text && text[0] ? text : "--");
}

void set_switch(lv_obj_t *control, bool checked, bool enabled) noexcept
{
    if (!control) return;
    if (checked)
        lv_obj_add_state(control, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(control, LV_STATE_CHECKED);
    if (enabled)
        lv_obj_remove_state(control, LV_STATE_DISABLED);
    else
        lv_obj_add_state(control, LV_STATE_DISABLED);
}

}  // namespace lilygo::settings::components
