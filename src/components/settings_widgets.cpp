#include "components/settings_widgets.hpp"

#include <cm0/typography.h>

#include <array>

namespace lilygo::settings::components {
namespace {

constexpr std::int32_t kSurfaceRadius = 8;
constexpr char kLockSymbol[]          = "\xEF\x80\xA3";  // Font Awesome lock, U+F023.

const char *text_or_empty(const char *text) noexcept
{
    return text ? text : "";
}

void set_name(lv_obj_t *object, const char *name)
{
    if (object && name && name[0]) lv_obj_set_name(object, name);
}

void set_background(lv_obj_t *object, theme::SemanticColor color, lv_style_selector_t selector = LV_PART_MAIN)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color.rgb), selector);
    lv_obj_set_style_bg_opa(object, color.opacity, selector);
}

void set_text_color(lv_obj_t *object, theme::SemanticColor color)
{
    lv_obj_set_style_text_color(object, lv_color_hex(color.rgb), LV_PART_MAIN);
    lv_obj_set_style_text_opa(object, color.opacity, LV_PART_MAIN);
}

void set_border(lv_obj_t *object, theme::SemanticColor color, std::int32_t width)
{
    lv_obj_set_style_border_color(object, lv_color_hex(color.rgb), LV_PART_MAIN);
    lv_obj_set_style_border_opa(object, color.opacity, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, width, LV_PART_MAIN);
}

void make_transparent(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void configure_flex_column(lv_obj_t *object, lv_flex_align_t cross_place)
{
    lv_obj_set_layout(object, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START, cross_place, LV_FLEX_ALIGN_START);
}

void set_grid_cell(lv_obj_t *object, lv_grid_align_t column_align, std::int32_t column, std::int32_t column_span,
                   lv_grid_align_t row_align, std::int32_t row, std::int32_t row_span)
{
    lv_obj_set_grid_cell(object, column_align, column, column_span, row_align, row, row_span);
}

void make_single_line(lv_obj_t *label, std::uint32_t font_size)
{
    const lv_font_t *font          = lilygo_ui_font_get(font_size);
    const std::int32_t line_height = font ? lv_font_get_line_height(font) : static_cast<std::int32_t>(font_size + 4U);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(label, line_height);
    lv_obj_set_style_max_height(label, line_height, LV_PART_MAIN);
}

lv_obj_t *create_divider(lv_obj_t *parent, bool visible)
{
    lv_obj_t *divider = lv_obj_create(parent);
    make_transparent(divider);
    lv_obj_set_size(divider, LV_PCT(100), 1);
    set_background(divider, theme::border);
    lv_obj_set_flag(divider, LV_OBJ_FLAG_HIDDEN, !visible);
    return divider;
}

void style_row(lv_obj_t *row, std::int32_t height)
{
    make_transparent(row);
    lv_obj_set_size(row, LV_PCT(100), height);
    set_background(row, theme::container_background);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    set_background(row, theme::pressed_background, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
}

}  // namespace

bool fonts_available() noexcept
{
    constexpr std::array<std::uint32_t, 8> sizes{14, 16, 18, 20, 22, 28, 38, 48};
    for (const std::uint32_t size : sizes) {
        if (!lilygo_ui_font_get(size)) return false;
    }
    return true;
}

lv_obj_t *create_label(lv_obj_t *parent, const char *text, std::uint32_t size, theme::SemanticColor color,
                       const char *name)
{
    if (!parent || size == 0) return nullptr;
    const lv_font_t *font = lilygo_ui_font_get(size);
    if (!font) return nullptr;

    lv_obj_t *label = lv_label_create(parent);
    set_name(label, name);
    lv_label_set_text(label, text_or_empty(text));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_size(label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    set_text_color(label, color);
    return label;
}

lv_obj_t *create_page(lv_obj_t *parent, const char *name)
{
    if (!parent) return nullptr;

    lv_obj_t *page = lv_obj_create(parent);
    set_name(page, name);
    make_transparent(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    set_background(page, theme::page_background);
    set_text_color(page, theme::text);
    configure_flex_column(page, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

lv_obj_t *create_surface(lv_obj_t *parent, const char *name)
{
    if (!parent) return nullptr;

    lv_obj_t *surface = lv_obj_create(parent);
    set_name(surface, name);
    make_transparent(surface);
    lv_obj_set_size(surface, LV_PCT(100), LV_SIZE_CONTENT);
    set_background(surface, theme::container_background);
    set_border(surface, theme::border, 1);
    lv_obj_set_style_radius(surface, kSurfaceRadius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(surface, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(surface, 12, LV_PART_MAIN);
    configure_flex_column(surface, LV_FLEX_ALIGN_START);
    return surface;
}

lv_obj_t *create_group(lv_obj_t *parent, const char *name)
{
    if (!parent) return nullptr;

    lv_obj_t *group = lv_obj_create(parent);
    set_name(group, name);
    make_transparent(group);
    lv_obj_set_size(group, LV_PCT(100), LV_SIZE_CONTENT);
    set_background(group, theme::container_background);
    set_border(group, theme::border, 1);
    lv_obj_set_style_radius(group, kSurfaceRadius, LV_PART_MAIN);
    configure_flex_column(group, LV_FLEX_ALIGN_START);
    return group;
}

lv_obj_t *create_section_label(lv_obj_t *parent, const char *text, const char *name)
{
    lv_obj_t *label = create_label(parent, text, 16, theme::section_label, name);
    if (!label) return nullptr;
    lv_obj_set_style_margin_left(label, 4, LV_PART_MAIN);
    return label;
}

DetailHeader create_detail_header(lv_obj_t *parent, const char *title, const char *back_name)
{
    DetailHeader result{};
    if (!parent) return result;

    static const std::int32_t columns[] = {48, LV_GRID_FR(1), 48, LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {64, LV_GRID_TEMPLATE_LAST};

    result.root = lv_obj_create(parent);
    make_transparent(result.root);
    lv_obj_set_size(result.root, LV_PCT(100), 64);
    lv_obj_set_layout(result.root, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(result.root, columns, rows);
    lv_obj_set_style_pad_left(result.root, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(result.root, 8, LV_PART_MAIN);

    result.back = create_label(result.root, LV_SYMBOL_LEFT, 28, theme::back_icon, back_name);
    lv_obj_set_size(result.back, 44, 44);
    lv_obj_set_style_text_align(result.back, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(result.back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(result.back, 2);
    set_grid_cell(result.back, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.title = create_label(result.root, title, 28, theme::container_title, "detail_header_title");
    make_single_line(result.title, 28);
    lv_obj_set_style_text_align(result.title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    set_grid_cell(result.title, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    return result;
}

ToggleRow create_toggle_row(lv_obj_t *parent, const char *title, const char *row_name, const char *control_name,
                            bool checked)
{
    ToggleRow result{};
    if (!parent) return result;

    static const std::int32_t columns[] = {LV_GRID_FR(1), 64, LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {72, LV_GRID_TEMPLATE_LAST};

    result.root = lv_obj_create(parent);
    set_name(result.root, row_name);
    make_transparent(result.root);
    lv_obj_set_size(result.root, LV_PCT(100), 72);
    set_background(result.root, theme::container_background);
    set_border(result.root, theme::border, 1);
    lv_obj_set_style_radius(result.root, kSurfaceRadius, LV_PART_MAIN);
    lv_obj_set_layout(result.root, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(result.root, columns, rows);
    lv_obj_set_style_pad_left(result.root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_right(result.root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_column(result.root, 12, LV_PART_MAIN);

    result.title = create_label(result.root, title, 22, theme::container_title);
    make_single_line(result.title, 22);
    set_grid_cell(result.title, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.control = lv_switch_create(result.root);
    set_name(result.control, control_name);
    lv_obj_set_size(result.control, 64, 36);
    set_background(result.control, theme::control_track, LV_PART_MAIN);
    set_background(result.control, theme::success, LV_PART_INDICATOR | LV_STATE_CHECKED);
    set_background(result.control, theme::container_background, LV_PART_KNOB);
    lv_obj_set_style_radius(result.control, 18, LV_PART_MAIN);
    lv_obj_set_style_radius(result.control, 18, LV_PART_INDICATOR);
    lv_obj_set_style_radius(result.control, 18, LV_PART_KNOB);
    lv_obj_set_style_pad_all(result.control, -2, LV_PART_KNOB);
    lv_obj_set_ext_click_area(result.control, 4);
    lv_obj_set_state(result.control, LV_STATE_CHECKED, checked);
    set_grid_cell(result.control, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    return result;
}

MenuRow create_menu_row(lv_obj_t *parent, const char *icon, const char *title, const char *detail, bool show_divider,
                        bool show_chevron, const char *name)
{
    MenuRow result{};
    if (!parent) return result;

    static const std::int32_t columns[] = {40, LV_GRID_FR(5), LV_GRID_FR(2), 20, LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {LV_GRID_FR(1), 1, LV_GRID_TEMPLATE_LAST};

    result.root = lv_obj_create(parent);
    set_name(result.root, name);
    style_row(result.root, 82);
    lv_obj_set_layout(result.root, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(result.root, columns, rows);
    lv_obj_set_style_pad_left(result.root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_right(result.root, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_column(result.root, 8, LV_PART_MAIN);

    result.icon = create_label(result.root, icon, 28, theme::primary_action);
    make_single_line(result.icon, 28);
    lv_obj_set_style_text_align(result.icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    set_grid_cell(result.icon, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.title = create_label(result.root, title, 22, theme::container_title);
    make_single_line(result.title, 22);
    set_grid_cell(result.title, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.detail = create_label(result.root, detail, 18, theme::muted_text);
    make_single_line(result.detail, 18);
    lv_obj_set_style_text_align(result.detail, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    set_grid_cell(result.detail, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.chevron = create_label(result.root, LV_SYMBOL_RIGHT, 20, theme::disclosure);
    make_single_line(result.chevron, 20);
    lv_obj_set_style_text_align(result.chevron, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_flag(result.chevron, LV_OBJ_FLAG_HIDDEN, !show_chevron);
    set_grid_cell(result.chevron, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.divider = create_divider(result.root, show_divider);
    set_grid_cell(result.divider, LV_GRID_ALIGN_STRETCH, 1, 3, LV_GRID_ALIGN_STRETCH, 1, 1);
    return result;
}

MenuRow create_image_menu_row(lv_obj_t *parent, const lv_image_dsc_t *icon, const char *title, const char *detail,
                              bool show_divider, bool show_chevron, const char *name)
{
    MenuRow result{};
    if (!parent || !icon) return result;

    static const std::int32_t columns[] = {48, LV_GRID_FR(5), LV_GRID_FR(2), 20, LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {LV_GRID_FR(1), 1, LV_GRID_TEMPLATE_LAST};

    result.root = lv_obj_create(parent);
    set_name(result.root, name);
    style_row(result.root, 82);
    lv_obj_set_layout(result.root, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(result.root, columns, rows);
    lv_obj_set_style_pad_left(result.root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_right(result.root, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_column(result.root, 8, LV_PART_MAIN);

    result.icon = lv_image_create(result.root);
    lv_image_set_src(result.icon, icon);
    lv_obj_set_size(result.icon, 42, 42);
    set_grid_cell(result.icon, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.title = create_label(result.root, title, 22, theme::container_title);
    make_single_line(result.title, 22);
    set_grid_cell(result.title, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.detail = create_label(result.root, detail, 18, theme::muted_text);
    make_single_line(result.detail, 18);
    lv_obj_set_style_text_align(result.detail, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    set_grid_cell(result.detail, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.chevron = create_label(result.root, LV_SYMBOL_RIGHT, 20, theme::disclosure);
    make_single_line(result.chevron, 20);
    lv_obj_set_style_text_align(result.chevron, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_flag(result.chevron, LV_OBJ_FLAG_HIDDEN, !show_chevron);
    set_grid_cell(result.chevron, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.divider = create_divider(result.root, show_divider);
    set_grid_cell(result.divider, LV_GRID_ALIGN_STRETCH, 1, 3, LV_GRID_ALIGN_STRETCH, 1, 1);
    return result;
}

ValueRow create_value_row(lv_obj_t *parent, const char *title, const char *value, theme::SemanticColor value_color,
                          bool show_divider, const char *name)
{
    ValueRow result{};
    if (!parent) return result;

    static const std::int32_t columns[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {LV_GRID_FR(1), 1, LV_GRID_TEMPLATE_LAST};

    result.root = lv_obj_create(parent);
    set_name(result.root, name);
    make_transparent(result.root);
    lv_obj_set_size(result.root, LV_PCT(100), 72);
    lv_obj_set_layout(result.root, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(result.root, columns, rows);
    lv_obj_set_style_pad_left(result.root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_right(result.root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_column(result.root, 12, LV_PART_MAIN);

    result.title = create_label(result.root, title, 20, theme::container_title);
    make_single_line(result.title, 20);
    set_grid_cell(result.title, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.value = create_label(result.root, value, 18, value_color);
    make_single_line(result.value, 18);
    lv_obj_set_style_text_align(result.value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    set_grid_cell(result.value, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    result.divider = create_divider(result.root, show_divider);
    set_grid_cell(result.divider, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
    return result;
}

lv_obj_t *create_dynamic_row(lv_obj_t *parent, const char *title, const char *subtitle, const char *trailing,
                             bool connected, bool locked)
{
    if (!parent) return nullptr;

    static const std::int32_t columns[] = {24, LV_GRID_FR(1), LV_GRID_CONTENT, 20, LV_GRID_TEMPLATE_LAST};
    static const std::int32_t rows[]    = {LV_GRID_FR(1), LV_GRID_FR(1), 1, LV_GRID_TEMPLATE_LAST};

    lv_obj_t *row = lv_obj_create(parent);
    set_name(row, "dynamic_row");
    style_row(row, 82);
    lv_obj_set_layout(row, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(row, columns, rows);
    lv_obj_set_style_pad_left(row, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);

    lv_obj_t *status = create_label(row, LV_SYMBOL_OK, 16, theme::accent, "dynamic_row_status");
    make_single_line(status, 16);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_flag(status, LV_OBJ_FLAG_HIDDEN, !connected);
    set_grid_cell(status, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 2);

    lv_obj_t *title_label = create_label(row, title, 22, theme::container_title, "settings_dynamic_row_title");
    make_single_line(title_label, 22);
    set_grid_cell(title_label, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_END, 0, 1);

    lv_obj_t *subtitle_label = create_label(row, subtitle, 14, theme::muted_text, "settings_dynamic_row_subtitle");
    make_single_line(subtitle_label, 14);
    set_grid_cell(subtitle_label, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 1, 1);

    lv_obj_t *lock = create_label(row, kLockSymbol, 16, theme::muted_text, "dynamic_row_lock");
    make_single_line(lock, 16);
    lv_obj_set_style_text_align(lock, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_flag(lock, LV_OBJ_FLAG_HIDDEN, !locked);
    set_grid_cell(lock, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_CENTER, 0, 2);

    lv_obj_t *trailing_label = create_label(row, trailing, 14, theme::muted_text, "dynamic_row_trailing");
    make_single_line(trailing_label, 14);
    lv_obj_set_width(trailing_label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(trailing_label, LV_PCT(38), LV_PART_MAIN);
    lv_obj_set_style_text_align(trailing_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    set_grid_cell(trailing_label, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 2);

    lv_obj_t *divider = create_divider(row, true);
    set_name(divider, "dynamic_row_divider");
    set_grid_cell(divider, LV_GRID_ALIGN_STRETCH, 1, 3, LV_GRID_ALIGN_STRETCH, 2, 1);
    return row;
}

}  // namespace lilygo::settings::components
