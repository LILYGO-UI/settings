#ifndef LILYGO_UI_SETTINGS_COMPONENTS_SETTINGS_THEME_HPP
#define LILYGO_UI_SETTINGS_COMPONENTS_SETTINGS_THEME_HPP

#include <cstdint>

namespace lilygo::settings::components::theme {

struct SemanticColor {
    std::uint32_t rgb;
    std::uint8_t opacity;
};

inline constexpr std::uint8_t opaque = 255;

// Core Settings surfaces, typography, dividers, and primary commands.
inline constexpr SemanticColor page_background{0xf2f2f7, opaque};
inline constexpr SemanticColor container_background{0xffffff, opaque};
inline constexpr SemanticColor container_title{0x1a1a1a, opaque};
inline constexpr SemanticColor text{0x000000, opaque};
inline constexpr SemanticColor border{0xd8dde3, opaque};
inline constexpr SemanticColor primary_action{0x20262d, opaque};

// Secondary Settings controls, status text, and modal presentation.
inline constexpr SemanticColor pressed_background{0xe3e3e8, opaque};
inline constexpr SemanticColor muted_text{0x8e8e93, opaque};
inline constexpr SemanticColor section_label{0x6e6e73, opaque};
inline constexpr SemanticColor accent{0x007aff, opaque};
inline constexpr SemanticColor success{0x2cc65b, opaque};
inline constexpr SemanticColor control_track{0xdedfe4, opaque};
inline constexpr SemanticColor modal_scrim{0x000000, 255 * 45 / 100};
inline constexpr SemanticColor disclosure{0xc7c7cc, opaque};
inline constexpr SemanticColor accent_content{0xffffff, opaque};
inline constexpr SemanticColor back_icon{0x111111, opaque};

// Battery level indicator thresholds.
inline constexpr SemanticColor battery_critical{0xd94841, opaque};
inline constexpr SemanticColor battery_low{0xf59e0b, opaque};
inline constexpr SemanticColor battery_healthy{0x34c759, opaque};

}  // namespace lilygo::settings::components::theme

#endif
