#include "pages/battery/battery_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

namespace lilygo::settings {

BatteryView::BatteryView(BatteryViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *BatteryView::create(lv_obj_t *parent)
{
    destroy();
    root_ = components::create_page(parent, "settings_battery");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);

    auto constrain = [](lv_obj_t *object) {
        lv_obj_set_width(object, LV_PCT(100));
        lv_obj_set_style_max_width(object, 680, 0);
    };

    auto header = components::create_detail_header(root_, "Battery", "settings_battery_back");
    constrain(header.root);

    lv_obj_t *summary = components::create_surface(root_);
    constrain(summary);
    lv_obj_set_height(summary, 164);
    lv_obj_set_layout(summary, LV_LAYOUT_GRID);
    static const std::int32_t summary_columns[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const std::int32_t summary_rows[]    = {LV_GRID_FR(1), LV_GRID_CONTENT, 16, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(summary, summary_columns, summary_rows);
    lv_obj_set_style_pad_all(summary, 18, 0);

    percent_ = components::create_label(summary, "82%", 48, components::theme::text, "settings_battery_percent");
    lv_obj_set_grid_cell(percent_, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 2);
    status_ = components::create_label(summary, "Discharging", 22, components::theme::text, "settings_battery_status");
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_grid_cell(status_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_END, 0, 1);
    technology_ =
        components::create_label(summary, "Li-ion", 16, components::theme::muted_text, "settings_battery_technology");
    lv_obj_set_style_text_align(technology_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_grid_cell(technology_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 1, 1);
    bar_ = lv_bar_create(summary);
    lv_obj_set_name_static(bar_, "settings_battery_bar");
    lv_obj_set_width(bar_, LV_PCT(100));
    lv_obj_set_height(bar_, 16);
    lv_obj_set_grid_cell(bar_, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_CENTER, 2, 1);
    lv_bar_set_range(bar_, 0, 100);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(components::theme::control_track.rgb), 0);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(components::theme::battery_healthy.rgb), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_, 8, 0);
    lv_obj_set_style_radius(bar_, 8, LV_PART_INDICATOR);

    constrain(components::create_section_label(root_, "BATTERY HEALTH"));
    lv_obj_t *health_group = components::create_group(root_);
    constrain(health_group);
    health_ = components::create_value_row(health_group, "Health", "Good", components::theme::success, true,
                                           "settings_battery_health_row")
                  .value;
    capacity_ = components::create_value_row(health_group, "Maximum Capacity", "84%", components::theme::muted_text,
                                             true, "settings_battery_capacity_row")
                    .value;
    cycles_ = components::create_value_row(health_group, "Cycle Count", "42", components::theme::muted_text, false,
                                           "settings_battery_cycles_row")
                  .value;

    constrain(components::create_section_label(root_, "POWER DETAILS"));
    lv_obj_t *power_group = components::create_group(root_);
    constrain(power_group);
    voltage_ = components::create_value_row(power_group, "Voltage", "4.01 V", components::theme::muted_text, true,
                                            "settings_battery_voltage_row")
                   .value;
    current_ = components::create_value_row(power_group, "Current", "-225 mA", components::theme::muted_text, true,
                                            "settings_battery_current_row")
                   .value;
    temperature_ = components::create_value_row(power_group, "Temperature", "31.5 C", components::theme::muted_text,
                                                false, "settings_battery_temperature_row")
                       .value;

    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void BatteryView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = percent_ = status_ = technology_ = bar_ = health_ = capacity_ = cycles_ = voltage_ = current_ =
        temperature_                                                                           = nullptr;
}

void BatteryView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<BatteryView *>(lv_observer_get_user_data(observer))->render();
}

void BatteryView::render() noexcept
{
    const auto &state = view_model_.presentation();
    components::set_text(percent_, state.percent.c_str());
    components::set_text(status_, state.status.c_str());
    components::set_text(technology_, state.technology.c_str());
    components::set_text(health_, state.health.c_str());
    components::set_text(capacity_, state.capacity.c_str());
    components::set_text(cycles_, state.cycles.c_str());
    components::set_text(voltage_, state.voltage.c_str());
    components::set_text(current_, state.current.c_str());
    components::set_text(temperature_, state.temperature.c_str());
    if (bar_) {
        lv_bar_set_value(bar_, state.bar_value, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar_, lv_color_hex(state.bar_color), LV_PART_INDICATOR);
    }
}

}  // namespace lilygo::settings
