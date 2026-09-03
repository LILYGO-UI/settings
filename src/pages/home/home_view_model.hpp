#ifndef LILYGO_UI_SETTINGS_PAGES_HOME_HOME_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_HOME_HOME_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_model.hpp"

#include <string>

namespace lilygo::settings {

struct HomePresentation {
    std::string wifi;
    std::string bluetooth;
    std::string ethernet;
    std::string battery;
};

class HomeViewModel {
public:
    explicit HomeViewModel(SettingsModel &model) noexcept;
    void publish();
    [[nodiscard]] const HomePresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    SettingsModel &model_;
    HomePresentation presentation_;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
