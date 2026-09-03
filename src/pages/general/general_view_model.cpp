#include "pages/general/general_view_model.hpp"

namespace lilygo::settings {

GeneralViewModel::GeneralViewModel(SettingsModel &model) noexcept : model_(model)
{
}

void GeneralViewModel::publish()
{
    const auto &about = model_.about();
    about_detail_     = about.device_name;
    if (!about.os_name.empty()) {
        if (!about_detail_.empty()) about_detail_ += " - ";
        about_detail_ += about.os_name;
    }
    revision_.publish();
}

const std::string &GeneralViewModel::about_detail() const noexcept
{
    return about_detail_;
}

lv_subject_t *GeneralViewModel::revision_subject() noexcept
{
    return revision_.get();
}

}  // namespace lilygo::settings
