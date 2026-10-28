#ifndef LILYGO_UI_SETTINGS_PAGES_CELLULAR_CELLULAR_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_CELLULAR_CELLULAR_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "pages/cellular/cellular_model.hpp"

#include <atomic>
#include <optional>
#include <string>
#include <thread>

namespace lilygo::settings {

struct CellularPresentation {
    bool enabled = false;
    bool control_enabled = false;
    std::string status = "Checking Cellular...";
    std::string summary = "Checking...";
    std::string operator_name = "Unavailable";
    std::string sim_status = "Checking...";
    std::string iccid = "Unavailable";
    std::string imsi = "Unavailable";
    std::string access_technology = "Unavailable";
    std::string signal = "Unavailable";
    std::string interface_name = "/dev/ttyACM0";
    std::string received = "Unavailable";
    std::string transmitted = "Unavailable";
    std::string total = "Unavailable";
    std::string usage_note = "Data usage requires modem statistics support.";
};

class CellularViewModel {
public:
    explicit CellularViewModel(CellularEnvironment environment = CellularEnvironment::from_process_environment());
    ~CellularViewModel();
    CellularViewModel(const CellularViewModel &) = delete;
    CellularViewModel &operator=(const CellularViewModel &) = delete;
    void refresh();
    void poll_command();
    void set_enabled(bool enabled);
    void stop() noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] const CellularPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    void start(std::optional<bool> target = {});
    void cancel() noexcept;
    void run(CellularModel snapshot, std::optional<bool> target, int function);
    void publish();
    CellularEnvironment environment_;
    CellularModel model_;
    CellularPresentation presentation_;
    std::thread worker_;
    std::atomic_bool cancelled_{false};
    std::atomic_bool ready_{false};
    // Only the worker writes these; the UI reads them after acquire + join.
    std::optional<CellularModel> completed_model_;
    std::string completed_error_;
    std::string completed_change_error_;
    std::optional<bool> requested_;
    std::optional<bool> queued_change_;
    std::optional<bool> failed_request_;
    std::string error_;
    std::string change_error_;
    bool checked_ = false;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
