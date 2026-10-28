#ifndef LILYGO_UI_SETTINGS_PAGES_SHARING_SHARING_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_SHARING_SHARING_MODEL_HPP

#include <string>
#include <string_view>
#include <vector>

namespace lilygo::settings {

struct SharingEnvironment {
    std::string systemctl;
    std::string sudo = {};
    static SharingEnvironment from_process_environment();
};

struct SharingState {
    bool known          = false;
    bool available      = false;
    bool enabled        = false;
    bool masked         = false;
    bool transitioning  = false;
    bool starts_at_boot = false;
};

class SharingModel {
public:
    explicit SharingModel(SharingEnvironment environment = SharingEnvironment::from_process_environment());
    [[nodiscard]] bool update(std::string_view output);
    void refresh_failed() noexcept;
    [[nodiscard]] const SharingState &state() const noexcept;
    [[nodiscard]] std::vector<std::string> query_arguments() const;
    [[nodiscard]] std::vector<std::string> change_arguments(bool enabled) const;

private:
    SharingEnvironment environment_;
    SharingState state_;
    bool socket_loaded_ = false;
};

}  // namespace lilygo::settings

#endif
