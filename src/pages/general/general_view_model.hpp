#ifndef LILYGO_UI_SETTINGS_PAGES_GENERAL_GENERAL_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_GENERAL_GENERAL_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_model.hpp"

#include <string>

namespace lilygo::settings {

class GeneralViewModel {
public:
    explicit GeneralViewModel(SettingsModel &model) noexcept;
    void publish();
    [[nodiscard]] const std::string &about_detail() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    SettingsModel &model_;
    std::string about_detail_;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
