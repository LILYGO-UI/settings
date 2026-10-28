#include "pages/battery/battery_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>
#include <cm0/input.h>

namespace lilygo::settings {
namespace {

void constrain(lv_obj_t *object)
{
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_style_max_width(object, 680, 0);
}

lv_obj_t *action(lv_obj_t *parent, const char *text, const char *name, bool primary)
{
    auto *button = lv_button_create(parent);
    lv_obj_set_name_static(button, name);
    lv_obj_set_size(button, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(button, 48, 0);
    lv_obj_set_style_pad_all(button, 12, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    const auto background = primary ? components::theme::primary_action : components::theme::pressed_background;
    lv_obj_set_style_bg_color(button, lv_color_hex(background.rgb), 0);
    lv_obj_set_style_bg_opa(button, background.opacity, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::theme::control_track.rgb), LV_STATE_DISABLED);
    auto *label = components::create_label(button, text, 18,
                                           primary ? components::theme::accent_content : components::theme::text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return button;
}

void set_action_enabled(lv_obj_t *button, bool enabled, bool primary = true)
{
    lv_obj_set_state(button, LV_STATE_DISABLED, !enabled);
    auto *label      = lv_obj_get_child(button, 0);
    const auto color = !enabled  ? components::theme::section_label
                       : primary ? components::theme::accent_content
                                 : components::theme::text;
    lv_obj_set_style_text_color(label, lv_color_hex(color.rgb), 0);
}

lv_obj_t *detail(lv_obj_t *parent, const char *title, const char *name, bool divider = true)
{
    const auto row = components::create_value_row(parent, title, "--", components::theme::muted_text, divider, name);
    // Both cells may wrap on a narrow parent. Their content determines the row
    // height, so translated titles and values cannot overlap the next row.
    static const std::int32_t columns[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {LV_GRID_CONTENT, 1, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(row.root, columns, rows);
    lv_obj_set_height(row.root, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row.root, 64, 0);
    lv_obj_set_style_pad_top(row.root, 14, 0);
    lv_obj_set_style_pad_bottom(row.root, 14, 0);
    for (auto *label : {row.title, row.value}) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_height(label, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(label, LV_COORD_MAX, 0);
    }
    return row.value;
}

}  // namespace

BatteryView::BatteryView(BatteryViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *BatteryView::create(lv_obj_t *parent)
{
    destroy();
    const auto *input_font    = lilygo_ui_font_get(22);
    const auto *keyboard_font = lilygo_ui_font_get(16);
    if (!input_font || !keyboard_font) return nullptr;
    root_ = components::create_page(parent, "settings_battery");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 12, 0);

    const auto header = components::create_detail_header(root_, "Battery", "settings_battery_back");
    constrain(header.root);

    auto *summary = components::create_surface(root_, "settings_battery_summary");
    constrain(summary);
    lv_obj_set_style_min_height(summary, 152, 0);
    lv_obj_set_layout(summary, LV_LAYOUT_GRID);
    static const std::int32_t summary_columns[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const std::int32_t summary_rows[]    = {LV_GRID_CONTENT, LV_GRID_CONTENT, 16, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(summary, summary_columns, summary_rows);
    lv_obj_set_style_pad_all(summary, 18, 0);

    percent_ = components::create_label(summary, "--", 48, components::theme::text, "settings_battery_percent");
    lv_obj_set_grid_cell(percent_, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 2);
    status_ = components::create_label(summary, "", 22, components::theme::text, "settings_battery_status");
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_grid_cell(status_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_END, 0, 1);
    technology_ =
        components::create_label(summary, "", 16, components::theme::muted_text, "settings_battery_technology");
    lv_obj_set_style_text_align(technology_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_grid_cell(technology_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 1, 1);
    bar_ = lv_bar_create(summary);
    lv_obj_set_name_static(bar_, "settings_battery_bar");
    lv_obj_set_size(bar_, LV_PCT(100), 16);
    lv_obj_set_grid_cell(bar_, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_CENTER, 2, 1);
    lv_bar_set_range(bar_, 0, 100);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(components::theme::control_track.rgb), 0);
    lv_obj_set_style_radius(bar_, 8, 0);
    lv_obj_set_style_radius(bar_, 8, LV_PART_INDICATOR);

    auto *calibration = components::create_surface(root_, "settings_battery_calibration");
    constrain(calibration);
    (void)components::create_label(calibration, "Battery Calibration", 22, components::theme::container_title);
    (void)components::create_label(calibration,
                                   "Set the battery's rated design capacity and measured full-charge capacity. "
                                   "Values are stored in gauge RAM and may reset when battery power is removed.",
                                   16, components::theme::section_label);
    auto capacity_input = [this, calibration, input_font](const char *title, const char *name) {
        (void)components::create_label(calibration, title, 18, components::theme::container_title);
        auto *input = lv_textarea_create(calibration);
        lv_obj_set_name_static(input, name);
        lv_obj_set_size(input, LV_PCT(100), 52);
        lv_textarea_set_one_line(input, true);
        lv_textarea_set_accepted_chars(input, "0123456789");
        lv_textarea_set_max_length(input, 5);
        lv_textarea_set_placeholder_text(input, "Unavailable");
        lv_obj_set_style_text_font(input, input_font, 0);
        lv_obj_set_style_text_color(input, lv_color_hex(components::theme::text.rgb), 0);
        lv_obj_set_style_bg_color(input, lv_color_hex(components::theme::container_background.rgb), 0);
        lv_obj_set_style_radius(input, 6, 0);
        lv_obj_set_style_border_width(input, 1, 0);
        lv_obj_set_style_border_color(input, lv_color_hex(components::theme::border.rgb), 0);
        lv_obj_set_style_pad_left(input, 12, 0);
        lv_obj_set_style_pad_right(input, 12, 0);
        lv_obj_add_event_cb(input, capacity_input_changed, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(input, capacity_input_changed, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(input, capacity_input_changed, LV_EVENT_FOCUSED, this);
        return input;
    };
    design_capacity_input_ = capacity_input("Design Capacity (mAh)", "settings_battery_design_capacity_input");
    full_capacity_input_   = capacity_input("Full-charge Capacity (mAh)", "settings_battery_full_capacity_input");
    capacity_keyboard_     = lv_keyboard_create(calibration);
    lv_obj_set_name_static(capacity_keyboard_, "settings_battery_capacity_keyboard");
    lv_obj_set_size(capacity_keyboard_, LV_PCT(100), 208);
    lv_obj_set_style_text_font(capacity_keyboard_, keyboard_font, LV_PART_ITEMS);
    lv_keyboard_set_mode(capacity_keyboard_, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(capacity_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(capacity_keyboard_, capacity_keyboard_finished, LV_EVENT_READY, this);
    lv_obj_add_event_cb(capacity_keyboard_, capacity_keyboard_finished, LV_EVENT_CANCEL, this);
    capacity_message_ = components::create_label(calibration, "", 16, components::theme::section_label,
                                                 "settings_battery_capacity_message");
    capacity_apply_   = action(calibration, "Review Changes", "settings_battery_capacity_apply", true);
    lv_obj_add_event_cb(capacity_apply_, capacity_apply_requested, LV_EVENT_CLICKED, this);
    capacity_confirmation_ = components::create_surface(calibration, "settings_battery_capacity_confirmation");
    lv_obj_set_style_pad_all(capacity_confirmation_, 0, 0);
    lv_obj_set_style_border_width(capacity_confirmation_, 0, 0);
    capacity_confirmation_text_ = components::create_label(capacity_confirmation_, "", 16, components::theme::text,
                                                           "settings_battery_capacity_confirmation_text");
    auto *capacity_cancel       = action(capacity_confirmation_, "Cancel", "settings_battery_capacity_cancel", false);
    lv_obj_add_event_cb(capacity_cancel, capacity_apply_cancelled, LV_EVENT_CLICKED, this);
    capacity_confirm_ = action(capacity_confirmation_, "Save Calibration", "settings_battery_capacity_confirm", true);
    lv_obj_add_event_cb(capacity_confirm_, capacity_apply_confirmed, LV_EVENT_CLICKED, this);

    auto *guide = components::create_surface(root_, "settings_battery_learning_guide");
    constrain(guide);
    (void)components::create_label(guide, "Guided Learning Cycle", 22, components::theme::container_title);
    calibration_description_ = components::create_label(guide, "", 16, components::theme::section_label,
                                                        "settings_battery_calibration_description");
    calibration_message_     = components::create_label(guide, "", 16, components::theme::section_label,
                                                        "settings_battery_calibration_message");
    calibration_request_     = action(guide, "Start Learning Guide", "settings_battery_calibrate", true);
    lv_obj_add_event_cb(calibration_request_, calibration_requested, LV_EVENT_CLICKED, this);
    calibration_confirmation_ = components::create_surface(guide, "settings_battery_calibration_confirmation");
    lv_obj_set_style_border_width(calibration_confirmation_, 0, 0);
    lv_obj_set_style_pad_all(calibration_confirmation_, 0, 0);
    (void)components::create_label(
        calibration_confirmation_,
        "Track a full charge, normal use down to 10%, then another full charge. "
        "Keep Settings running to track the cycle. Do not deliberately discharge to shutdown. "
        "This guide cannot verify the gauge's automatic learning or restore capacity.",
        16, components::theme::text, "settings_battery_calibration_prompt");
    auto *cancel = action(calibration_confirmation_, "Cancel", "settings_battery_calibration_cancel", false);
    lv_obj_add_event_cb(cancel, calibration_cancelled, LV_EVENT_CLICKED, this);
    calibration_confirm_ =
        action(calibration_confirmation_, "Start Guide", "settings_battery_calibration_confirm", true);
    lv_obj_add_event_cb(calibration_confirm_, calibration_confirmed, LV_EVENT_CLICKED, this);
    calibration_stop_ = action(guide, "Stop Guide", "settings_battery_calibration_stop", false);
    lv_obj_add_event_cb(calibration_stop_, calibration_cancelled, LV_EVENT_CLICKED, this);

    constrain(components::create_section_label(root_, "CHARGING"));
    auto *charging = components::create_surface(root_, "settings_battery_charging");
    constrain(charging);
    charger_ =
        components::create_label(charging, "", 22, components::theme::container_title, "settings_battery_charger");
    charger_status_ =
        components::create_label(charging, "", 16, components::theme::section_label, "settings_battery_charger_status");
    charge_voltage_ =
        components::create_label(charging, "", 16, components::theme::section_label, "settings_battery_charge_voltage");
    auto make_control = [this, charging](ControlWidgets &widgets, bool input) {
        widgets.root = components::create_surface(
            charging, input ? "settings_battery_input_current_control" : "settings_battery_charge_current_control");
        lv_obj_set_style_border_width(widgets.root, 0, 0);
        lv_obj_set_style_pad_all(widgets.root, 0, 0);
        (void)components::create_label(widgets.root, input ? "Input Current Limit" : "Charge Current", 20,
                                       components::theme::container_title);
        widgets.value = components::create_label(
            widgets.root, "", 22, components::theme::text,
            input ? "settings_battery_input_current_value" : "settings_battery_charge_current_value");
        widgets.range = components::create_label(
            widgets.root, "", 14, components::theme::section_label,
            input ? "settings_battery_input_current_range" : "settings_battery_charge_current_range");
        // Padding keeps the knob and its full hit target inside the card at
        // either end without fixing the slider to a particular screen width.
        auto *track = components::create_surface(widgets.root);
        lv_obj_set_style_border_width(track, 0, 0);
        lv_obj_set_style_pad_left(track, 18, 0);
        lv_obj_set_style_pad_right(track, 18, 0);
        lv_obj_set_style_pad_top(track, 18, 0);
        lv_obj_set_style_pad_bottom(track, 18, 0);
        widgets.slider = lv_slider_create(track);
        lv_obj_set_name_static(
            widgets.slider, input ? "settings_battery_input_current_slider" : "settings_battery_charge_current_slider");
        lv_obj_set_size(widgets.slider, LV_PCT(100), 8);
        lv_obj_set_ext_click_area(widgets.slider, 18);
        lv_obj_set_style_bg_color(widgets.slider, lv_color_hex(components::theme::control_track.rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(widgets.slider, lv_color_hex(components::theme::primary_action.rgb),
                                  LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(widgets.slider, lv_color_hex(components::theme::primary_action.rgb), LV_PART_KNOB);
        lv_obj_add_event_cb(widgets.slider, input ? input_current_changed : charge_current_changed,
                            LV_EVENT_VALUE_CHANGED, this);
        widgets.apply =
            action(widgets.root, "Apply",
                   input ? "settings_battery_input_current_apply" : "settings_battery_charge_current_apply", true);
        lv_obj_add_event_cb(widgets.apply, input ? input_current_applied : charge_current_applied, LV_EVENT_CLICKED,
                            this);
    };
    make_control(input_current_, true);
    make_control(charge_current_, false);
    charging_message_ = components::create_label(charging, "", 16, components::theme::section_label,
                                                 "settings_battery_charging_message");
    lv_obj_move_to_index(guide, static_cast<std::int32_t>(lv_obj_get_child_count(root_) - 1));

    constrain(components::create_section_label(root_, "BATTERY HEALTH"));
    auto *health_group = components::create_group(root_);
    constrain(health_group);
    gauge_            = detail(health_group, "Fuel Gauge", "settings_battery_gauge_row");
    health_           = detail(health_group, "Health", "settings_battery_health_row");
    capacity_         = detail(health_group, "Maximum Capacity", "settings_battery_capacity_row");
    remaining_charge_ = detail(health_group, "Remaining Charge", "settings_battery_remaining_charge_row");
    full_charge_      = detail(health_group, "Full Charge", "settings_battery_full_charge_row");
    design_charge_    = detail(health_group, "Design Capacity", "settings_battery_design_charge_row");
    cycles_           = detail(health_group, "Cycle Count", "settings_battery_cycles_row", false);

    constrain(components::create_section_label(root_, "POWER DETAILS"));
    auto *power_group = components::create_group(root_);
    constrain(power_group);
    voltage_     = detail(power_group, "Voltage", "settings_battery_voltage_row");
    current_     = detail(power_group, "Current", "settings_battery_current_row");
    temperature_ = detail(power_group, "Temperature", "settings_battery_temperature_row", false);

    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void BatteryView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = percent_ = status_ = technology_ = bar_ = health_ = capacity_ = cycles_ = voltage_ = current_ =
        temperature_ = remaining_charge_ = full_charge_ = design_charge_ = gauge_ = charger_ = charger_status_ =
            charge_voltage_ = charging_message_ = calibration_description_ = calibration_message_ =
                calibration_request_ = calibration_confirmation_ = calibration_confirm_ = calibration_stop_ = nullptr;
    design_capacity_input_ = full_capacity_input_ = capacity_apply_ = capacity_message_ = capacity_confirmation_ =
        capacity_confirmation_text_ = capacity_confirm_ = capacity_keyboard_ = nullptr;
    updating_inputs_                                                         = false;
    input_current_                                                           = {};
    charge_current_                                                          = {};
}

void BatteryView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<BatteryView *>(lv_observer_get_user_data(observer))->render();
}

void BatteryView::capacity_input_changed(lv_event_t *event)
{
    auto *self = static_cast<BatteryView *>(lv_event_get_user_data(event));
    if (self->updating_inputs_) return;
    auto *field = lv_event_get_target_obj(event);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        self->view_model_.stage_capacity(lv_textarea_get_text(self->design_capacity_input_),
                                         lv_textarea_get_text(self->full_capacity_input_));
        return;
    }
    if (lv_obj_has_state(field, LV_STATE_DISABLED)) return;
    lv_keyboard_set_textarea(self->capacity_keyboard_, field);
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && !cm0_input_hardware_keyboard_present()) {
        lv_obj_clear_flag(self->capacity_keyboard_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view_recursive(self->capacity_keyboard_, LV_ANIM_OFF);
    }
}

void BatteryView::capacity_keyboard_finished(lv_event_t *event)
{
    auto *self = static_cast<BatteryView *>(lv_event_get_user_data(event));
    lv_obj_add_flag(self->capacity_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(self->capacity_keyboard_, nullptr);
    lv_obj_scroll_to_view_recursive(self->capacity_apply_, LV_ANIM_OFF);
}

void BatteryView::capacity_apply_requested(lv_event_t *event)
{
    auto *self = static_cast<BatteryView *>(lv_event_get_user_data(event));
    lv_obj_add_flag(self->capacity_keyboard_, LV_OBJ_FLAG_HIDDEN);
    self->view_model_.request_capacity_apply();
}

void BatteryView::capacity_apply_confirmed(lv_event_t *event)
{
    static_cast<BatteryView *>(lv_event_get_user_data(event))->view_model_.confirm_capacity_apply();
}

void BatteryView::capacity_apply_cancelled(lv_event_t *event)
{
    static_cast<BatteryView *>(lv_event_get_user_data(event))->view_model_.cancel_capacity_apply();
}

void BatteryView::input_current_changed(lv_event_t *event)
{
    auto *self = static_cast<BatteryView *>(lv_event_get_user_data(event));
    self->view_model_.stage_input_current(lv_slider_get_value(self->input_current_.slider));
}

void BatteryView::charge_current_changed(lv_event_t *event)
{
    auto *self = static_cast<BatteryView *>(lv_event_get_user_data(event));
    self->view_model_.stage_charge_current(lv_slider_get_value(self->charge_current_.slider));
}

void BatteryView::input_current_applied(lv_event_t *event)
{
    static_cast<BatteryView *>(lv_event_get_user_data(event))->view_model_.apply_input_current();
}

void BatteryView::charge_current_applied(lv_event_t *event)
{
    static_cast<BatteryView *>(lv_event_get_user_data(event))->view_model_.apply_charge_current();
}

void BatteryView::calibration_requested(lv_event_t *event)
{
    static_cast<BatteryView *>(lv_event_get_user_data(event))->view_model_.request_calibration();
}

void BatteryView::calibration_confirmed(lv_event_t *event)
{
    static_cast<BatteryView *>(lv_event_get_user_data(event))->view_model_.confirm_calibration();
}

void BatteryView::calibration_cancelled(lv_event_t *event)
{
    static_cast<BatteryView *>(lv_event_get_user_data(event))->view_model_.cancel_calibration();
}

void BatteryView::render() noexcept
{
    if (!root_) return;
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
    components::set_text(remaining_charge_, state.remaining_charge.c_str());
    components::set_text(full_charge_, state.full_charge.c_str());
    components::set_text(design_charge_, state.design_charge.c_str());
    components::set_text(gauge_, state.gauge.c_str());
    components::set_text(charger_, state.charger.c_str());
    components::set_text(charger_status_, state.charger_status.c_str());
    components::set_text(charge_voltage_, state.charge_voltage.c_str());
    components::set_text(charging_message_, state.charging_message.c_str());
    lv_obj_set_flag(charging_message_, LV_OBJ_FLAG_HIDDEN, state.charging_message.empty());
    lv_obj_set_flag(charge_voltage_, LV_OBJ_FLAG_HIDDEN, state.charge_voltage.empty());
    components::set_text(calibration_description_, state.calibration_description.c_str());
    components::set_text(calibration_message_, state.calibration_message.c_str());
    lv_obj_set_flag(calibration_message_, LV_OBJ_FLAG_HIDDEN, state.calibration_message.empty());
    set_action_enabled(calibration_request_, state.calibration_enabled);
    set_action_enabled(calibration_confirm_, state.calibration_enabled);
    lv_obj_set_flag(calibration_request_, LV_OBJ_FLAG_HIDDEN,
                    state.calibration_confirmation || state.calibration_active);
    lv_obj_set_flag(calibration_stop_, LV_OBJ_FLAG_HIDDEN, !state.calibration_active);
    set_action_enabled(calibration_stop_, !view_model_.busy(), false);
    const bool was_hidden = lv_obj_has_flag(calibration_confirmation_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flag(calibration_confirmation_, LV_OBJ_FLAG_HIDDEN, !state.calibration_confirmation);
    if (was_hidden && state.calibration_confirmation) {
        lv_obj_scroll_to_view_recursive(calibration_confirmation_, LV_ANIM_OFF);
    }
    const auto &capacity = state.capacity_configuration;
    updating_inputs_     = true;
    if (capacity.design != lv_textarea_get_text(design_capacity_input_))
        lv_textarea_set_text(design_capacity_input_, capacity.design.c_str());
    if (capacity.full != lv_textarea_get_text(full_capacity_input_))
        lv_textarea_set_text(full_capacity_input_, capacity.full.c_str());
    updating_inputs_ = false;
    for (auto *input : {design_capacity_input_, full_capacity_input_})
        lv_obj_set_state(input, LV_STATE_DISABLED, !capacity.enabled || capacity.show_confirmation);
    set_action_enabled(capacity_apply_, capacity.can_apply);
    set_action_enabled(capacity_confirm_, capacity.can_apply);
    lv_obj_set_flag(capacity_apply_, LV_OBJ_FLAG_HIDDEN, capacity.show_confirmation);
    components::set_text(capacity_message_, capacity.message.c_str());
    lv_obj_set_flag(capacity_message_, LV_OBJ_FLAG_HIDDEN, capacity.message.empty());
    components::set_text(capacity_confirmation_text_, capacity.confirmation.c_str());
    const bool capacity_was_hidden = lv_obj_has_flag(capacity_confirmation_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flag(capacity_confirmation_, LV_OBJ_FLAG_HIDDEN, !capacity.show_confirmation);
    if (capacity_was_hidden && capacity.show_confirmation)
        lv_obj_scroll_to_view_recursive(capacity_confirmation_, LV_ANIM_OFF);
    if (!capacity.enabled) lv_obj_add_flag(capacity_keyboard_, LV_OBJ_FLAG_HIDDEN);
    auto render_control = [](ControlWidgets &widgets, const BatteryControlPresentation &control) {
        lv_obj_set_flag(widgets.root, LV_OBJ_FLAG_HIDDEN, !control.available);
        components::set_text(widgets.value, control.value.c_str());
        components::set_text(widgets.range, control.range.c_str());
        lv_slider_set_range(widgets.slider, 0, control.maximum_position);
        lv_slider_set_value(widgets.slider, control.position, LV_ANIM_OFF);
        lv_obj_set_state(widgets.slider, LV_STATE_DISABLED, !control.enabled);
        set_action_enabled(widgets.apply, control.enabled && control.pending);
    };
    render_control(input_current_, state.input_current);
    render_control(charge_current_, state.charge_current);
    lv_bar_set_value(bar_, state.bar_value, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(state.bar_color), LV_PART_INDICATOR);
}

}  // namespace lilygo::settings
