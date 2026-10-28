#ifndef LILYGO_UI_SETTINGS_PAGES_SHARING_SHARING_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_SHARING_SHARING_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_command_runner.hpp"
#include "pages/sharing/sharing_model.hpp"
#include "pages/sharing/serial_console_model.hpp"

#include <optional>
#include <string>

namespace lilygo::settings {

struct SharingPresentation {
    bool enabled                = false;
    bool control_enabled        = false;
    std::string status          = "Checking Remote Login...";
    bool serial_enabled         = false;
    bool serial_control_enabled = false;
    bool reboot_required        = false;
    bool show_reboot_prompt     = false;
    bool reboot_busy            = false;
    std::string serial_status   = "Checking Serial Console...";
    std::string reboot_error;
};

class SharingViewModel {
public:
    explicit SharingViewModel(
        SharingEnvironment environment              = SharingEnvironment::from_process_environment(),
        SerialConsoleEnvironment serial_environment = SerialConsoleEnvironment::from_process_environment());
    void refresh();
    void poll_command();
    void set_enabled(bool enabled);
    void set_serial_enabled(bool enabled);
    void reboot_now();
    void reboot_later();
    void stop() noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] const SharingPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    void query();
    void query_serial();
    void poll_serial();
    void publish();
    SharingModel model_;
    SettingsCommandRunner commands_;
    SerialConsoleModel serial_model_;
    SettingsCommandRunner serial_commands_;
    std::optional<bool> serial_requested_;
    std::string serial_error_;
    bool reboot_prompt_   = false;
    bool reboot_accepted_ = false;
    SharingPresentation presentation_;
    std::optional<bool> requested_;
    std::optional<bool> failed_request_;
    std::string error_;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
