#include "pages/keyboard/keyboard_view_model.hpp"

#include <utility>

namespace lilygo::settings {

KeyboardViewModel::KeyboardViewModel(KeyboardEnvironment environment) : model_(std::move(environment))
{
}

void KeyboardViewModel::refresh()
{
    model_.refresh();
    publish();
}

void KeyboardViewModel::set_strength(int percent)
{
    apply_failed_ = !model_.set_strength(percent);
    publish();
}

void KeyboardViewModel::publish()
{
    const auto availability = model_.availability();
    presentation_.enabled   = availability == KeyboardAvailability::ready;
    presentation_.strength  = model_.strength();
    presentation_.value     = availability == KeyboardAvailability::unavailable ? "Unavailable"
                              : model_.strength() == 0                          ? "Off"
                                                       : std::to_string(model_.strength()) + "%";
    if (availability == KeyboardAvailability::unavailable)
        presentation_.status = "Keyboard vibration control is not available on this device.";
    else if (availability == KeyboardAvailability::read_only)
        presentation_.status = "Permission is required to change keyboard vibration.";
    else if (apply_failed_)
        presentation_.status = "Could not apply vibration strength. Try again.";
    else
        presentation_.status = "Adjust vibration after each key press. Set to 0% to turn it off.";
    revision_.publish();
}

const KeyboardPresentation &KeyboardViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *KeyboardViewModel::revision_subject() noexcept
{
    return revision_.get();
}

}  // namespace lilygo::settings
