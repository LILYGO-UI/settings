#ifndef LILYGO_UI_SETTINGS_PAGES_BATTERY_BATTERY_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_BATTERY_BATTERY_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "components/settings_theme.hpp"
#include "domain/settings_model.hpp"

#include <cstdint>
#include <string>

namespace lilygo::settings {

struct BatteryPresentation {
    std::string percent;
    std::string status;
    std::string technology;
    std::string health;
    std::string capacity;
    std::string cycles;
    std::string voltage;
    std::string current;
    std::string temperature;
    int bar_value           = 0;
    std::uint32_t bar_color = components::theme::battery_healthy.rgb;
};

class BatteryViewModel {
public:
    explicit BatteryViewModel(SettingsModel &model) noexcept;
    void publish();
    [[nodiscard]] const BatteryPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    SettingsModel &model_;
    BatteryPresentation presentation_;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
