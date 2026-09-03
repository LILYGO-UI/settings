#include "pages/about/about_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

namespace lilygo::settings {

AboutView::AboutView(AboutViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *AboutView::create(lv_obj_t *parent)
{
    destroy();
    root_ = components::create_page(parent, "settings_about");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);

    auto constrain = [](lv_obj_t *object) {
        lv_obj_set_width(object, LV_PCT(100));
        lv_obj_set_style_max_width(object, 680, 0);
    };

    auto header = components::create_detail_header(root_, "About", "settings_about_back");
    constrain(header.root);
    constrain(components::create_section_label(root_, "DEVICE"));

    lv_obj_t *device_group = components::create_group(root_);
    constrain(device_group);
    device_ = components::create_value_row(device_group, "Name", "LILYGO CM0", components::theme::muted_text, true,
                                           "settings_about_name_row")
                  .value;
    model_ = components::create_value_row(device_group, "Model", "CM0", components::theme::muted_text, true,
                                          "settings_about_model_row")
                 .value;
    serial_ = components::create_value_row(device_group, "Serial Number", "--", components::theme::muted_text, false,
                                           "settings_about_serial_row")
                  .value;

    constrain(components::create_section_label(root_, "SOFTWARE"));
    lv_obj_t *software_group = components::create_group(root_);
    constrain(software_group);
    os_ = components::create_value_row(software_group, "Operating System", "CM0 OS 2.0", components::theme::muted_text,
                                       true, "settings_about_os_row")
              .value;
    kernel_ = components::create_value_row(software_group, "Kernel", "Linux", components::theme::muted_text, true,
                                           "settings_about_kernel_row")
                  .value;
    architecture_ = components::create_value_row(software_group, "Architecture", "aarch64",
                                                 components::theme::muted_text, false, "settings_about_arch_row")
                        .value;

    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void AboutView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = device_ = model_ = serial_ = os_ = kernel_ = architecture_ = nullptr;
}

void AboutView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<AboutView *>(lv_observer_get_user_data(observer))->render();
}

void AboutView::render() noexcept
{
    const auto &state = view_model_.presentation();
    components::set_text(device_, state.device.c_str());
    components::set_text(model_, state.model.c_str());
    components::set_text(serial_, state.serial.c_str());
    components::set_text(os_, state.os.c_str());
    components::set_text(kernel_, state.kernel.c_str());
    components::set_text(architecture_, state.architecture.c_str());
}

}  // namespace lilygo::settings
