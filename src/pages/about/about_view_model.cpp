#include "pages/about/about_view_model.hpp"

namespace lilygo::settings {

AboutViewModel::AboutViewModel(SettingsModel &model) noexcept : model_(model)
{
}

void AboutViewModel::publish()
{
    const auto &about = model_.about();
    presentation_     = {about.device_name, about.model, about.serial, about.os_name, about.kernel, about.architecture};
    revision_.publish();
}

const AboutPresentation &AboutViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *AboutViewModel::revision_subject() noexcept
{
    return revision_.get();
}

}  // namespace lilygo::settings
