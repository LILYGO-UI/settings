#ifndef LILYGO_UI_SETTINGS_PAGES_BLUETOOTH_BLUETOOTH_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_BLUETOOTH_BLUETOOTH_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_command_runner.hpp"
#include "domain/settings_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace lilygo::settings {

struct BluetoothPresentation {
    bool available       = false;
    bool enabled         = false;
    bool control_enabled = false;
    std::string status;
    std::size_t list_generation = 0;
};

class BluetoothViewModel {
public:
    BluetoothViewModel(SettingsModel &model, SettingsCommandRunner &commands) noexcept;

    void publish();
    void refresh();
    [[nodiscard]] bool sync_controller();
    [[nodiscard]] bool resume_pending_scan();
    void cancel_pending_scan() noexcept;
    void scan();
    void set_enabled(bool enabled);
    void connect(std::size_t index);
    void command_finished(const SettingsCommandResult &result);

    [[nodiscard]] const BluetoothPresentation &presentation() const noexcept;
    [[nodiscard]] const std::vector<BluetoothDevice> &devices() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    [[nodiscard]] bool start(SettingsCommand command, std::vector<std::string> arguments, const char *progress);
    void update_list_generation() noexcept;
    [[nodiscard]] std::string derived_status() const;

    SettingsModel &model_;
    SettingsCommandRunner &commands_;
    BluetoothPresentation presentation_;
    std::string status_override_;
    bool scan_pending_                     = false;
    std::size_t list_generation_           = 0;
    std::size_t observed_model_generation_ = 0;
    bool observed_list_state_valid_        = false;
    bool observed_available_               = false;
    bool observed_enabled_                 = false;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
