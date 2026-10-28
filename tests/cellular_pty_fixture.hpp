#ifndef LILYGO_UI_SETTINGS_TESTS_CELLULAR_PTY_FIXTURE_HPP
#define LILYGO_UI_SETTINGS_TESTS_CELLULAR_PTY_FIXTURE_HPP

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace test_support {

struct CellularPtyReply {
    std::string text;
    std::chrono::milliseconds delay{0};
    std::size_t fragment_size = 0;
    std::chrono::milliseconds fragment_delay{0};
    bool disconnect = false;
};

// Emulate a modem on an actual PTY, exercising the production serial transport
// rather than replacing it with command scripts. An open slave keeps the PTY
// alive while the application repeatedly releases its exclusive session.
class CellularPtyFixture {
public:
    using Handler = std::function<std::string(const std::string &)>;
    using ReplyHandler = std::function<CellularPtyReply(const std::string &)>;

    explicit CellularPtyFixture(Handler handler = {})
    {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (master_ < 0 || ::grantpt(master_) != 0 || ::unlockpt(master_) != 0) {
            if (master_ >= 0) ::close(master_);
            throw std::runtime_error("Cannot create cellular PTY");
        }
        const char *path = ::ptsname(master_);
        if (!path) {
            ::close(master_);
            throw std::runtime_error("Cannot obtain cellular PTY path");
        }
        device_ = path;
        slave_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (slave_ < 0) {
            ::close(master_);
            throw std::runtime_error("Cannot keep cellular PTY open");
        }
        termios options{};
        if (::tcgetattr(slave_, &options) != 0) {
            ::close(slave_);
            ::close(master_);
            throw std::runtime_error("Cannot read cellular PTY settings");
        }
        ::cfmakeraw(&options);
        if (::tcsetattr(slave_, TCSANOW, &options) != 0) {
            ::close(slave_);
            ::close(master_);
            throw std::runtime_error("Cannot configure cellular PTY");
        }
        set_handler(std::move(handler));
        worker_ = std::thread([this] { run(); });
    }

    ~CellularPtyFixture()
    {
        stopped_.store(true);
        if (worker_.joinable()) worker_.join();
        if (master_ >= 0) ::close(master_);
        ::close(slave_);
    }

    CellularPtyFixture(const CellularPtyFixture &) = delete;
    CellularPtyFixture &operator=(const CellularPtyFixture &) = delete;

    [[nodiscard]] const std::string &device_path() const noexcept { return device_; }
    [[nodiscard]] int observation_fd() const noexcept { return slave_; }

    [[nodiscard]] std::vector<std::string> command_log() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return commands_;
    }

    void set_handler(Handler handler)
    {
        set_reply_handler([handler = std::move(handler)](const std::string &command) {
            return CellularPtyReply{handler ? handler(command) : "\r\nERROR\r\n"};
        });
    }

    void set_reply_handler(ReplyHandler handler)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        handler_ = std::move(handler);
    }

private:
    bool pause(std::chrono::milliseconds duration)
    {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (!stopped_.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return !stopped_.load();
    }

    bool send(const CellularPtyReply &reply)
    {
        if (!pause(reply.delay)) return false;
        if (reply.disconnect) {
            ::close(master_);
            master_ = -1;
            return false;
        }
        std::size_t sent = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (sent < reply.text.size() && !stopped_.load()) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            pollfd item{master_, POLLOUT, 0};
            const auto ready = ::poll(&item, 1, 20);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0 || (item.revents & (POLLERR | POLLHUP | POLLNVAL))) return false;
            if (!(item.revents & POLLOUT)) continue;
            const auto count = std::min(reply.text.size() - sent,
                                        reply.fragment_size ? reply.fragment_size : reply.text.size());
            const auto written = ::write(master_, reply.text.data() + sent, count);
            if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
            if (written <= 0) return false;
            sent += static_cast<std::size_t>(written);
            if (!pause(reply.fragment_delay)) return false;
        }
        return !stopped_.load();
    }

    void run()
    {
        std::string command;
        while (!stopped_.load()) {
            pollfd item{master_, POLLIN, 0};
            const auto ready = ::poll(&item, 1, 20);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0 || (item.revents & (POLLERR | POLLHUP | POLLNVAL))) break;
            if (!(item.revents & POLLIN)) continue;
            char buffer[1024];
            const auto count = ::read(master_, buffer, sizeof(buffer));
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
            if (count <= 0) break;
            for (std::ptrdiff_t index = 0; index < count; ++index) {
                if (buffer[index] != '\r' && buffer[index] != '\n') {
                    command += buffer[index];
                    if (command.size() > 4096) return;
                    continue;
                }
                if (command.empty()) continue;
                ReplyHandler handler;
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    commands_.push_back(command);
                    handler = handler_;
                }
                const auto reply = handler ? handler(command) : CellularPtyReply{};
                command.clear();
                if (!send(reply)) return;
            }
        }
    }

    int master_ = -1;
    int slave_ = -1;
    std::string device_;
    std::atomic_bool stopped_{false};
    mutable std::mutex mutex_;
    ReplyHandler handler_;
    std::vector<std::string> commands_;
    std::thread worker_;
};

}  // namespace test_support

#endif
