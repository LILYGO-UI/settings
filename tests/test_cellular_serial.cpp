#include "pages/cellular/cellular_serial.hpp"
#include "cellular_pty_fixture.hpp"

#include <cassert>
#include <chrono>
#include <fcntl.h>
#include <functional>
#include <string>
#include <sys/file.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;
using lilygo::settings::CellularSerial;
using test_support::CellularPtyFixture;
using test_support::CellularPtyReply;

void assert_same_termios(const termios &expected, const termios &actual)
{
    assert(expected.c_iflag == actual.c_iflag);
    assert(expected.c_oflag == actual.c_oflag);
    assert(expected.c_cflag == actual.c_cflag);
    assert(expected.c_lflag == actual.c_lflag);
    assert(::cfgetispeed(&expected) == ::cfgetispeed(&actual));
    assert(::cfgetospeed(&expected) == ::cfgetospeed(&actual));
    for (std::size_t index = 0; index < NCCS; ++index) assert(expected.c_cc[index] == actual.c_cc[index]);
}

void test_fragmented_echo_urcs_and_settings_restoration()
{
    CellularPtyFixture modem;
    modem.set_reply_handler([](const std::string &command) {
        return CellularPtyReply{"\r\n" + command + "\r\n\n+CEREG: 1\r\n+CFUN: 1\n\rOK\r\n", 0ms, 3, 1ms};
    });
    termios before{};
    assert(::tcgetattr(modem.observation_fd(), &before) == 0);
    std::atomic_bool cancel{false};
    {
        CellularSerial serial(modem.device_path());
        assert(serial.open(cancel) && serial.is_open());
        termios active{};
        assert(::tcgetattr(modem.observation_fd(), &active) == 0);
        assert(::cfgetispeed(&active) == B115200 && ::cfgetospeed(&active) == B115200);
        assert((active.c_cflag & CSIZE) == CS8 && !(active.c_cflag & (PARENB | CSTOPB)));
        assert(!(active.c_lflag & (ICANON | ECHO | ISIG)));
        const auto response = serial.command("AT+CFUN?", cancel);
        assert(response.success && !response.timed_out && response.error.empty());
        assert((response.lines == std::vector<std::string>{"+CEREG: 1", "+CFUN: 1"}));
        assert((modem.command_log() == std::vector<std::string>{"AT+CFUN?"}));
    }
    termios after{};
    assert(::tcgetattr(modem.observation_fd(), &after) == 0);
    assert_same_termios(before, after);
    // The destructor must clear TIOCEXCL as well as the cooperating lock.
    CellularSerial reopened(modem.device_path());
    assert(reopened.open(cancel));
}

void test_rejected_command_keeps_session_and_terminal_tokens_are_exact()
{
    CellularPtyFixture modem([](const std::string &command) {
        if (command == "AT+BAD") return std::string("\r\nERROR\r\n");
        if (command == "AT+CME") return std::string("\r\n+CME ERROR: 10\r\n");
        if (command == "AT+CMS") return std::string("\r\n+CMS ERROR: 500\r\n");
        return std::string("\r\n+NOTE: ERROR OK\r\nOKAY\r\nERROR data\r\nOK\r\n");
    });
    std::atomic_bool cancel{false};
    CellularSerial serial(modem.device_path());
    assert(serial.open(cancel));
    for (const auto *command : {"AT+BAD", "AT+CME", "AT+CMS"}) {
        const auto response = serial.command(command, cancel);
        assert(!response.success && !response.timed_out && !response.error.empty());
        assert(serial.is_open());
    }
    const auto valid = serial.command("AT", cancel);
    assert(valid.success);
    assert((valid.lines == std::vector<std::string>{"+NOTE: ERROR OK", "OKAY", "ERROR data"}));
    const auto invalid = serial.command("AT\rAT+CFUN=0", cancel);
    assert(!invalid.success && invalid.error == "Invalid AT command");
    assert(serial.is_open() && modem.command_log().size() == 4);
}

void test_timeout_closes_session_and_drops_stale_response()
{
    CellularPtyFixture modem;
    modem.set_reply_handler([](const std::string &command) {
        if (command == "AT+SLOW") return CellularPtyReply{"\r\nOK\r\n", 120ms};
        return CellularPtyReply{"\r\n+CFUN: 1\r\nOK\r\n"};
    });
    std::atomic_bool cancel{false};
    CellularSerial serial(modem.device_path());
    assert(serial.open(cancel));
    const auto timeout = serial.command("AT+SLOW", cancel, 20ms);
    assert(!timeout.success && timeout.timed_out && !serial.is_open());
    assert(serial.command("AT", cancel).error == "Serial port is not open");
    std::this_thread::sleep_for(150ms);
    assert(serial.open(cancel));
    const auto response = serial.command("AT+CFUN?", cancel);
    assert(response.success && response.lines == std::vector<std::string>{"+CFUN: 1"});
    assert((modem.command_log() == std::vector<std::string>{"AT+SLOW", "AT+CFUN?"}));
}

void test_cooperating_lock_contends_before_touching_port()
{
    CellularPtyFixture modem([](const std::string &) { return std::string("\r\nOK\r\n"); });
    std::atomic_bool cancel{false};
    CellularSerial first(modem.device_path());
    CellularSerial second(modem.device_path());
    assert(first.open(cancel));
    assert(!second.open(cancel) && second.error().find("busy") != std::string::npos);
    assert(first.command("AT", cancel).success);
    first.close();
    assert(second.open(cancel));
    second.close();

    // This is the lock naming protocol used by the separate Cellular app.
    const auto path = "/tmp/lilygo-cellular-" + std::to_string(std::hash<std::string>{}(modem.device_path())) + ".lock";
    const int owner = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    assert(owner >= 0 && ::flock(owner, LOCK_EX | LOCK_NB) == 0);
    termios before{};
    assert(::tcgetattr(modem.observation_fd(), &before) == 0);
    assert(!first.open(cancel) && first.error().find("busy") != std::string::npos);
    termios after{};
    assert(::tcgetattr(modem.observation_fd(), &after) == 0);
    assert_same_termios(before, after);
    assert(modem.command_log().size() == 1);
    assert(::flock(owner, LOCK_UN) == 0);
    ::close(owner);
    assert(first.open(cancel));
}

void test_cancellation_is_bounded_and_closes_session()
{
    CellularPtyFixture modem([](const std::string &) { return std::string{}; });
    std::atomic_bool cancel{false};
    CellularSerial serial(modem.device_path());
    assert(serial.open(cancel));
    std::thread stopper([&cancel] {
        std::this_thread::sleep_for(25ms);
        cancel.store(true);
    });
    const auto start = std::chrono::steady_clock::now();
    const auto response = serial.command("AT", cancel, 5s);
    stopper.join();
    assert(!response.success && !response.timed_out && response.error == "Cancelled");
    assert(std::chrono::steady_clock::now() - start < 300ms);
    assert(!serial.is_open());
    assert(!serial.open(cancel) && serial.error() == "Cancelled");
    cancel.store(false);
    assert(serial.open(cancel));
}

void test_disconnect_missing_port_and_response_limits()
{
    std::atomic_bool cancel{false};
    CellularSerial missing("/dev/lilygo-cellular-test-missing");
    assert(!missing.open(cancel));
    assert(missing.error().find("unavailable") != std::string::npos);
    {
        CellularPtyFixture modem;
        modem.set_reply_handler([](const std::string &) { return CellularPtyReply{{}, 0ms, 0, 0ms, true}; });
        CellularSerial serial(modem.device_path());
        assert(serial.open(cancel));
        const auto response = serial.command("AT", cancel);
        assert(!response.success && !response.timed_out && !serial.is_open());
        assert(response.error.find("disconnected") != std::string::npos);
    }
    for (const auto &oversized : {std::string(4097, 'x') + "\r\nOK\r\n",
                                  [] {
                                      std::string result;
                                      for (unsigned i = 0; i < 257; ++i) result += "+NOTICE: busy\r\n";
                                      return result + "OK\r\n";
                                  }(),
                                  [] {
                                      std::string result;
                                      for (unsigned i = 0; i < 9; ++i) result += std::string(4000, 'x') + "\r\n";
                                      return result + "OK\r\n";
                                  }()}) {
        CellularPtyFixture modem([&oversized](const std::string &) { return oversized; });
        CellularSerial serial(modem.device_path());
        assert(serial.open(cancel));
        const auto response = serial.command("AT", cancel);
        assert(!response.success && !response.timed_out && !serial.is_open());
        assert(!response.error.empty());
    }
}

}  // namespace

int main()
{
    test_fragmented_echo_urcs_and_settings_restoration();
    test_rejected_command_keeps_session_and_terminal_tokens_are_exact();
    test_timeout_closes_session_and_drops_stale_response();
    test_cooperating_lock_contends_before_touching_port();
    test_cancellation_is_bounded_and_closes_session();
    test_disconnect_missing_port_and_response_limits();
}
