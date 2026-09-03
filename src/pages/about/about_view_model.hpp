#ifndef LILYGO_UI_SETTINGS_PAGES_ABOUT_ABOUT_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_ABOUT_ABOUT_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_model.hpp"

#include <string>

namespace lilygo::settings {

struct AboutPresentation {
    std::string device;
    std::string model;
    std::string serial;
    std::string os;
    std::string kernel;
    std::string architecture;
};

class AboutViewModel {
public:
    explicit AboutViewModel(SettingsModel &model) noexcept;
    void publish();
    [[nodiscard]] const AboutPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    SettingsModel &model_;
    AboutPresentation presentation_;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
