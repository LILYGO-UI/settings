#include "pages/cellular/cellular_serial.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace lilygo::settings {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t maximum_response_bytes = 32768;
constexpr std::size_t maximum_line_bytes = 4096;
constexpr std::size_t maximum_response_lines = 256;

std::string failure(const std::string &action)
{
    const int code = errno;
    if (code == EACCES || code == EPERM) return action + ": permission denied";
    if (code == EBUSY || code == EWOULDBLOCK) return action + ": serial port is busy";
    if (code == ENOENT || code == ENODEV || code == ENXIO) return action + ": serial port is unavailable";
    return action + ": " + std::strerror(code);
}

std::string trimmed(std::string_view line)
{
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = line.find_last_not_of(" \t");
    return std::string(line.substr(first, last - first + 1));
}

bool error_line(const std::string &line)
{
    return line == "ERROR" || line == "+CME ERROR" || line == "+CMS ERROR" ||
           line.compare(0, 11, "+CME ERROR:") == 0 || line.compare(0, 11, "+CMS ERROR:") == 0;
}

int poll_timeout(Clock::time_point deadline)
{
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return static_cast<int>(std::clamp<std::int64_t>(remaining.count(), 1, 50));
}

// The dedicated Cellular application uses this lock. Keep the protocol shared,
// without importing that application as a build or runtime dependency.
std::string lock_path(const std::string &device)
{
    if (device == "/dev/ttyACM0") return "/tmp/lilygo_nrf9151_uart.lock";
    return "/tmp/lilygo-cellular-" + std::to_string(std::hash<std::string>{}(device)) + ".lock";
}

}  // namespace

CellularSerial::CellularSerial(std::string device) : device_(std::move(device)) {}

CellularSerial::~CellularSerial()
{
    close();
}

bool CellularSerial::open(const std::atomic_bool &cancel)
{
    error_.clear();
    if (cancel.load()) {
        error_ = "Cancelled";
        close();
        return false;
    }
    if (fd_ >= 0) return true;

    lock_fd_ = ::open(lock_path(device_).c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_fd_ < 0) {
        error_ = failure("Cannot open serial port lock");
        return false;
    }
    struct stat lock_stat {};
    if (::fstat(lock_fd_, &lock_stat) != 0 || !S_ISREG(lock_stat.st_mode)) {
        error_ = "Serial port lock is not a regular file";
        close();
        return false;
    }
    if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
        error_ = "Serial port is busy in another application";
        close();
        return false;
    }
    fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        error_ = failure("Cannot open " + device_);
        close();
        return false;
    }
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        error_ = "Serial port is busy in another application";
        close();
        return false;
    }
    if (::ioctl(fd_, TIOCEXCL) != 0) {
        error_ = failure("Cannot exclusively access serial port");
        close();
        return false;
    }
    exclusive_ = true;
    if (::tcgetattr(fd_, &original_termios_) != 0) {
        error_ = failure("Cannot read serial settings");
        close();
        return false;
    }
    auto options = original_termios_;
    ::cfmakeraw(&options);
    ::cfsetispeed(&options, B115200);
    ::cfsetospeed(&options, B115200);
    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    options.c_cflag |= CS8;
#ifdef CRTSCTS
    options.c_cflag &= ~CRTSCTS;
#endif
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;
    if (::tcsetattr(fd_, TCSANOW, &options) != 0) {
        error_ = failure("Cannot configure serial port");
        close();
        return false;
    }
    restore_termios_ = true;
    // Only discard pre-existing bytes after both cooperating and kernel locks
    // have succeeded. A failed open must not consume another application's data.
    if (::tcflush(fd_, TCIFLUSH) != 0) {
        error_ = failure("Cannot clear serial input");
        close();
        return false;
    }
    if (cancel.load()) {
        error_ = "Cancelled";
        close();
        return false;
    }
    return true;
}

CellularAtResponse CellularSerial::command(std::string_view command_text, const std::atomic_bool &cancel,
                                          std::chrono::milliseconds timeout)
{
    CellularAtResponse response;
    error_.clear();
    auto fail = [this, &response](std::string message, bool disconnect = true) {
        error_ = std::move(message);
        response.error = error_;
        if (disconnect) close();
        return response;
    };
    if (cancel.load()) return fail("Cancelled");
    if (fd_ < 0) return fail("Serial port is not open");
    if (command_text.empty() || command_text.size() > 512 ||
        std::any_of(command_text.begin(), command_text.end(), [](unsigned char character) {
            return character < 0x20 || character > 0x7e;
        }))
        return fail("Invalid AT command", false);

    const auto deadline = Clock::now() + std::max(timeout, std::chrono::milliseconds::zero());
    const std::string bytes = std::string(command_text) + "\r\n";
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        if (cancel.load()) return fail("Cancelled");
        if (Clock::now() >= deadline) {
            response.timed_out = true;
            return fail("Modem response timed out");
        }
        pollfd item{fd_, POLLOUT, 0};
        const int ready = ::poll(&item, 1, poll_timeout(deadline));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return fail(failure("Serial write poll failed"));
        if (item.revents & (POLLERR | POLLHUP | POLLNVAL)) return fail("Serial port disconnected");
        if (!(item.revents & POLLOUT)) continue;
        const auto count = ::write(fd_, bytes.data() + sent, bytes.size() - sent);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        if (count < 0) return fail(failure("Serial write failed"));
        if (count == 0) return fail("Serial port disconnected");
        sent += static_cast<std::size_t>(count);
    }

    std::string line;
    std::size_t received = 0;
    while (true) {
        if (cancel.load()) return fail("Cancelled");
        if (Clock::now() >= deadline) {
            response.timed_out = true;
            return fail("Modem response timed out");
        }
        pollfd item{fd_, POLLIN, 0};
        const int ready = ::poll(&item, 1, poll_timeout(deadline));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return fail(failure("Serial read poll failed"));
        if (item.revents & (POLLERR | POLLHUP | POLLNVAL)) return fail("Serial port disconnected");
        if (!(item.revents & POLLIN)) continue;
        char buffer[1024];
        const auto count = ::read(fd_, buffer, sizeof(buffer));
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        if (count < 0) return fail(failure("Serial read failed"));
        if (count == 0) return fail("Serial port disconnected");
        received += static_cast<std::size_t>(count);
        if (received > maximum_response_bytes) return fail("Modem response exceeds 32 KiB");
        for (std::ptrdiff_t index = 0; index < count; ++index) {
            const char character = buffer[index];
            if (character != '\r' && character != '\n') {
                line += character;
                if (line.size() > maximum_line_bytes) return fail("Modem response line is too long");
                continue;
            }
            const auto text = trimmed(line);
            line.clear();
            if (text.empty() || text == command_text) continue;
            if (text == "OK") {
                response.success = true;
                return response;
            }
            if (error_line(text)) return fail(text, false);
            if (response.lines.size() >= maximum_response_lines) return fail("Too many modem response lines");
            // Preserve unsolicited messages. The command-specific parser knows
            // which prefixes belong to the response and ignores the remainder.
            response.lines.push_back(text);
        }
    }
}

void CellularSerial::close() noexcept
{
    if (fd_ >= 0) {
        if (restore_termios_) ::tcsetattr(fd_, TCSANOW, &original_termios_);
        if (exclusive_) ::ioctl(fd_, TIOCNXCL);
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
    if (lock_fd_ >= 0) {
        ::flock(lock_fd_, LOCK_UN);
        ::close(lock_fd_);
    }
    fd_ = -1;
    lock_fd_ = -1;
    exclusive_ = false;
    restore_termios_ = false;
}

const std::string &CellularSerial::error() const noexcept
{
    return error_;
}

bool CellularSerial::is_open() const noexcept
{
    return fd_ >= 0;
}

}  // namespace lilygo::settings
