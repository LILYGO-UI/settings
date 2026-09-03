#include "pages/bluetooth/bluetooth_view.hpp"

#include "components/ui_helpers.hpp"
#include "components/settings_widgets.hpp"

#include <cm0/typography.h>

#include <cstdint>

namespace lilygo::settings {

BluetoothView::BluetoothView(BluetoothViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *BluetoothView::create(lv_obj_t *parent)
{
    destroy();
    root_ = components::create_page(parent, "settings_bluetooth");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);

    auto constrain = [](lv_obj_t *object) {
        lv_obj_set_width(object, LV_PCT(100));
        lv_obj_set_style_max_width(object, 680, 0);
    };

    auto header = components::create_detail_header(root_, "Bluetooth", "settings_bluetooth_back");
    constrain(header.root);
    auto toggle = components::create_toggle_row(root_, "Bluetooth", nullptr, "settings_bluetooth_switch", true);
    constrain(toggle.root);
    switch_ = toggle.control;
    status_ = components::create_label(root_, "Searching...", 16, components::theme::section_label,
                                       "settings_bluetooth_status");
    constrain(status_);
    constrain(components::create_section_label(root_, "DEVICES"));
    device_group_ = components::create_group(root_, "settings_bluetooth_device_group");
    constrain(device_group_);

    lv_obj_add_event_cb(switch_, switch_changed, LV_EVENT_VALUE_CHANGED, this);
    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void BluetoothView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = switch_ = status_ = device_group_ = nullptr;
    rendered_generation_                      = std::numeric_limits<std::size_t>::max();
    rendered_available_                       = false;
    rendered_enabled_                         = false;
    updating_                                 = false;
}

void BluetoothView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<BluetoothView *>(lv_observer_get_user_data(observer))->render();
}

void BluetoothView::switch_changed(lv_event_t *event)
{
    auto *self = static_cast<BluetoothView *>(lv_event_get_user_data(event));
    if (self->updating_) return;
    self->view_model_.set_enabled(lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED));
}

void BluetoothView::device_clicked(lv_event_t *event)
{
    auto *self         = static_cast<BluetoothView *>(lv_event_get_user_data(event));
    const auto encoded = reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (encoded != 0) self->view_model_.connect(encoded - 1U);
}

void BluetoothView::render() noexcept
{
    const auto &state = view_model_.presentation();
    updating_         = true;
    components::set_switch(switch_, state.enabled, state.control_enabled);
    updating_ = false;
    components::set_text(status_, state.status.c_str());
    if (rendered_generation_ != state.list_generation || rendered_available_ != state.available ||
        rendered_enabled_ != state.enabled)
        rebuild_devices();
}

void BluetoothView::rebuild_devices()
{
    lv_obj_clean(device_group_);
    const auto &state = view_model_.presentation();
    if (!state.available)
        create_empty_row("Bluetooth Unavailable");
    else if (!state.enabled)
        create_empty_row("Bluetooth Is Off");
    else if (view_model_.devices().empty())
        create_empty_row("No Devices Found");
    else {
        const auto &devices = view_model_.devices();
        for (std::size_t index = 0; index < devices.size(); ++index) {
            const auto &device = devices[index];
            lv_obj_t *row = components::create_dynamic_row(device_group_, device.name.c_str(), device.address.c_str(),
                                                           "", false, false);
            lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<std::uintptr_t>(index + 1U)));
            lv_obj_add_event_cb(row, device_clicked, LV_EVENT_CLICKED, this);
        }
        set_group_height(devices.size());
    }
    rendered_generation_ = state.list_generation;
    rendered_available_  = state.available;
    rendered_enabled_    = state.enabled;
}

void BluetoothView::create_empty_row(const char *message)
{
    lv_obj_t *row = components::create_dynamic_row(device_group_, message, "", "", false, false);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    set_group_height(1);
}

void BluetoothView::set_group_height(std::size_t rows) noexcept
{
    if (device_group_) lv_obj_set_height(device_group_, static_cast<std::int32_t>(rows * 83U));
}

}  // namespace lilygo::settings
