#ifndef LILYGO_UI_SETTINGS_PAGES_CELLULAR_CELLULAR_SERIAL_HPP
#define LILYGO_UI_SETTINGS_PAGES_CELLULAR_CELLULAR_SERIAL_HPP

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <termios.h>
#include <vector>

namespace lilygo::settings {

struct CellularAtResponse {
    bool success = false;
    bool timed_out = false;
    std::vector<std::string> lines;
    std::string error;
};

// A single worker owns a session. Cancellation may be signalled by another
// thread; close() and command() must not be called concurrently. No unsolicited
// modem settings are changed, including echo, SIM credentials or PDP contexts.
class CellularSerial {
public:
    explicit CellularSerial(std::string device);
    ~CellularSerial();
    CellularSerial(const CellularSerial &) = delete;
    CellularSerial &operator=(const CellularSerial &) = delete;
    [[nodiscard]] bool open(const std::atomic_bool &cancel);
    // Timeout, cancellation and transport failures close the session, so late
    // response bytes cannot be accepted by a subsequent command on that session.
    // The caller must explicitly open a new session before retrying.
    [[nodiscard]] CellularAtResponse command(
        std::string_view command, const std::atomic_bool &cancel,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const std::string &error() const noexcept;

private:
    std::string device_;
    std::string error_;
    int fd_ = -1;
    int lock_fd_ = -1;
    bool exclusive_ = false;
    bool restore_termios_ = false;
    struct termios original_termios_ {};
};

}  // namespace lilygo::settings

#endif
