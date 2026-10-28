#include "pages/cellular/cellular_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

namespace lilygo::settings {
namespace {

void constrain(lv_obj_t *object)
{
    if (!object) return;
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_style_max_width(object, 680, 0);
}

lv_obj_t *detail(lv_obj_t *parent, const char *title, const char *row_name, const char *value_name, bool divider = true)
{
    const auto row =
        components::create_value_row(parent, title, "--", components::theme::section_label, divider, row_name);
    lv_obj_set_name_static(row.root, row_name);
    lv_obj_set_name_static(row.value, value_name);
    // Let long identifiers and translated labels grow the row inside its
    // actual parent width, including when the sidebar reduces the viewport.
    static const std::int32_t columns[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {LV_GRID_CONTENT, 1, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(row.root, columns, rows);
    lv_obj_set_height(row.root, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(row.root, 14, 0);
    lv_obj_set_style_pad_bottom(row.root, divider ? 0 : 14, 0);
    // Put the separator after the lower text inset, at the row's bottom edge.
    lv_obj_set_style_pad_row(row.root, 14, 0);
    for (auto *label : {row.title, row.value}) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_height(label, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(label, LV_COORD_MAX, 0);
    }
    return row.value;
}

}  // namespace

CellularView::CellularView(CellularViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *CellularView::create(lv_obj_t *parent)
{
    destroy();
    for (const auto size : {16U, 18U, 20U, 22U, 28U})
        if (!lilygo_ui_font_get(size)) return nullptr;

    root_ = components::create_page(parent, "settings_cellular");
    if (!root_) return nullptr;
    lv_obj_set_name_static(root_, "settings_cellular");
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 12, 0);
    lv_obj_add_event_cb(root_, layout_changed, LV_EVENT_LAYOUT_CHANGED, this);

    const auto header = components::create_detail_header(root_, "Cellular", "settings_cellular_back");
    constrain(header.root);

    const auto toggle =
        components::create_toggle_row(root_, "Cellular", "settings_cellular_enabled_row", "settings_cellular_switch");
    constrain(toggle.root);
    control_ = toggle.control;
    lv_obj_add_event_cb(control_, enabled_changed, LV_EVENT_VALUE_CHANGED, this);

    status_ = components::create_label(root_, "", 18, components::theme::section_label, "settings_cellular_status");
    constrain(status_);

    constrain(components::create_section_label(root_, "SIM INFORMATION"));
    auto *sim = components::create_group(root_, "settings_cellular_sim_group");
    constrain(sim);
    sim_status_ = detail(sim, "SIM Status", "settings_cellular_sim_status_row", "settings_cellular_sim_status");
    iccid_      = detail(sim, "ICCID", "settings_cellular_iccid_row", "settings_cellular_iccid");
    imsi_       = detail(sim, "IMSI", "settings_cellular_imsi_row", "settings_cellular_imsi", false);

    constrain(components::create_section_label(root_, "CONNECTION"));
    auto *connection = components::create_group(root_, "settings_cellular_connection_group");
    constrain(connection);
    operator_   = detail(connection, "Operator", "settings_cellular_operator_row", "settings_cellular_operator");
    technology_ = detail(connection, "Network", "settings_cellular_technology_row", "settings_cellular_technology");
    signal_     = detail(connection, "Signal", "settings_cellular_signal_row", "settings_cellular_signal");
    interface_ =
        detail(connection, "Port", "settings_cellular_interface_row", "settings_cellular_interface", false);

    constrain(components::create_section_label(root_, "DATA USAGE"));
    auto *usage = components::create_group(root_, "settings_cellular_usage_group");
    constrain(usage);
    received_    = detail(usage, "Received", "settings_cellular_received_row", "settings_cellular_received");
    transmitted_ = detail(usage, "Sent", "settings_cellular_transmitted_row", "settings_cellular_transmitted");
    total_       = detail(usage, "Total", "settings_cellular_total_row", "settings_cellular_total", false);
    usage_note_ =
        components::create_label(root_, "", 16, components::theme::section_label, "settings_cellular_usage_note");
    constrain(usage_note_);

    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void CellularView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = control_ = status_ = sim_status_ = iccid_ = imsi_ = operator_ = technology_ = signal_ = interface_ =
        received_ = transmitted_ = total_ = usage_note_ = nullptr;
    updating_controls_                                  = false;
}

void CellularView::layout_changed(lv_event_t *event)
{
    auto *self = static_cast<CellularView *>(lv_event_get_user_data(event));
    // Wrapped rows can shrink after the initial size-change scroll adjustment.
    // Retain any valid offset and clamp only once Flex has resolved the content.
    if (self && self->root_) lv_obj_readjust_scroll(self->root_, LV_ANIM_OFF);
}

void CellularView::enabled_changed(lv_event_t *event)
{
    auto *self = static_cast<CellularView *>(lv_event_get_user_data(event));
    if (!self || self->updating_controls_ || !self->control_ || lv_obj_has_state(self->control_, LV_STATE_DISABLED))
        return;
    self->view_model_.set_enabled(lv_obj_has_state(self->control_, LV_STATE_CHECKED));
}

void CellularView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<CellularView *>(lv_observer_get_user_data(observer))->render();
}

void CellularView::render() noexcept
{
    if (!root_) return;
    const auto &state  = view_model_.presentation();
    updating_controls_ = true;
    components::set_switch(control_, state.enabled, state.control_enabled);
    updating_controls_ = false;
    components::set_text(status_, state.status.c_str());
    components::set_text(sim_status_, state.sim_status.c_str());
    components::set_text(iccid_, state.iccid.c_str());
    components::set_text(imsi_, state.imsi.c_str());
    components::set_text(operator_, state.operator_name.c_str());
    components::set_text(technology_, state.access_technology.c_str());
    components::set_text(signal_, state.signal.c_str());
    components::set_text(interface_, state.interface_name.c_str());
    components::set_text(received_, state.received.c_str());
    components::set_text(transmitted_, state.transmitted.c_str());
    components::set_text(total_, state.total.c_str());
    components::set_text(usage_note_, state.usage_note.c_str());
    lv_obj_set_flag(usage_note_, LV_OBJ_FLAG_HIDDEN, state.usage_note.empty());
}

}  // namespace lilygo::settings
