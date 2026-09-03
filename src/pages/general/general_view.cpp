#include "pages/general/general_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

namespace lilygo::settings {

GeneralView::GeneralView(GeneralViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *GeneralView::create(lv_obj_t *parent)
{
    destroy();
    root_ = components::create_page(parent, "settings_general");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 28, 0);

    auto header = components::create_detail_header(root_, "General", "settings_general_back");
    lv_obj_set_width(header.root, LV_PCT(100));
    lv_obj_set_style_max_width(header.root, 680, 0);

    lv_obj_t *group = components::create_group(root_);
    lv_obj_set_width(group, LV_PCT(100));
    lv_obj_set_style_max_width(group, 680, 0);
    const auto about =
        components::create_menu_row(group, LV_SYMBOL_SETTINGS, "About", "", false, true, "settings_general_about_row");
    about_detail_ = about.detail;

    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void GeneralView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = about_detail_ = nullptr;
}

void GeneralView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<GeneralView *>(lv_observer_get_user_data(observer))->render();
}

void GeneralView::render() noexcept
{
    components::set_text(about_detail_, view_model_.about_detail().c_str());
}

}  // namespace lilygo::settings
