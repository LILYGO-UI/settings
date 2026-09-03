#include "assets/settings_icons.hpp"

#include "assets/settings_icon_battery.inc"
#include "assets/settings_icon_bluetooth.inc"
#include "assets/settings_icon_ethernet.inc"
#include "assets/settings_icon_general.inc"
#include "assets/settings_icon_wifi.inc"

#include <cstdint>

namespace lilygo::settings::assets {
namespace {

lv_image_dsc_t png_icon(const unsigned char *data, std::uint32_t size)
{
    lv_image_dsc_t icon{};
    icon.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon.header.cf    = LV_COLOR_FORMAT_RAW_ALPHA;
    icon.header.w     = 42;
    icon.header.h     = 42;
    icon.data_size    = size;
    icon.data         = data;
    return icon;
}

}  // namespace

const lv_image_dsc_t wifi_icon      = png_icon(settings_icon_wifi_png, sizeof(settings_icon_wifi_png));
const lv_image_dsc_t bluetooth_icon = png_icon(settings_icon_bluetooth_png, sizeof(settings_icon_bluetooth_png));
const lv_image_dsc_t ethernet_icon  = png_icon(settings_icon_ethernet_png, sizeof(settings_icon_ethernet_png));
const lv_image_dsc_t battery_icon   = png_icon(settings_icon_battery_png, sizeof(settings_icon_battery_png));
const lv_image_dsc_t general_icon   = png_icon(settings_icon_general_png, sizeof(settings_icon_general_png));

}  // namespace lilygo::settings::assets
