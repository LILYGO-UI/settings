#include "domain/settings_command_runner.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using namespace lilygo::settings;
using namespace std::chrono_literals;

class EnvironmentVariable {
public:
    explicit EnvironmentVariable(std::string name) : name_(std::move(name))
    {
        if (const auto *value = std::getenv(name_.c_str())) original_ = value;
    }
    ~EnvironmentVariable()
    {
        if (original_) assert(::setenv(name_.c_str(), original_->c_str(), 1) == 0);
        else assert(::unsetenv(name_.c_str()) == 0);
    }
    void set(const std::string &value) const { assert(::setenv(name_.c_str(), value.c_str(), 1) == 0); }
    void unset() const { assert(::unsetenv(name_.c_str()) == 0); }
private:
    std::string name_;
    std::optional<std::string> original_;
};

class Fixture {
public:
    Fixture()
    {
        char pattern[] = "/tmp/lilygo-ui-command-tests-XXXXXX";
        const auto *directory = ::mkdtemp(pattern);
        assert(directory);
        root_ = directory;
    }
    ~Fixture() { std::filesystem::remove_all(root_); }
    std::string path(const std::string &relative) const { return (root_ / relative).string(); }
    void write(const std::string &relative, const std::string &text, mode_t mode = 0755) const
    {
        const auto file = path(relative);
        std::filesystem::create_directories(std::filesystem::path(file).parent_path());
        std::ofstream stream(file);
        stream << text;
        stream.close();
        assert(stream && ::chmod(file.c_str(), mode) == 0);
    }
private:
    std::filesystem::path root_;
};

SettingsCommandResult run(const std::vector<std::string> &arguments, std::string_view input = {},
                          std::chrono::milliseconds timeout = 2s)
{
    SettingsCommandRunner runner;
    assert(runner.start(SettingsCommand::wifi_scan, arguments, input, timeout));
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = runner.poll()) {
            assert(!runner.busy());
            return std::move(*result);
        }
        std::this_thread::sleep_for(1ms);
    }
    assert(false && "command runner failed to complete");
    return {};
}

void test_environment_arguments_and_stdin_during_background_allocations()
{
    Fixture fixture;
    fixture.write("show-environment", "#!/bin/sh\nIFS= read -r line\n"
        "printf '%s|%s|%s|%s\\n' \"$LC_ALL\" \"$LILYGO_COMMAND_TEST_VALUE\" \"$1\" \"$line\"\n");
    EnvironmentVariable locale("LC_ALL");
    EnvironmentVariable inherited("LILYGO_COMMAND_TEST_VALUE");
    locale.set("POSIX");
    inherited.set("inherited value");
    std::atomic_bool stop{false};
    std::thread allocator([&stop] {
        while (!stop.load()) {
            std::vector<std::string> values(100, std::string(2048, 'x'));
            values.back() += 'y';
        }
    });
    for (unsigned repetition = 0; repetition < 20; ++repetition) {
        const auto result = run({fixture.path("show-environment"), "literal ; $HOME `uname`"}, "input ; $USER\n");
        assert(result.success);
        assert(result.output == "C|inherited value|literal ; $HOME `uname`|input ; $USER\n");
    }
    stop.store(true);
    allocator.join();
    // Locale normalization is isolated to the child environment.
    assert(std::string(std::getenv("LC_ALL")) == "POSIX");
}

void test_path_search_continues_after_missing_and_inaccessible_entries()
{
    Fixture fixture;
    fixture.write("denied/probe", "#!/bin/sh\nprintf 'wrong\\n'\n", 0644);
    fixture.write("not-a-directory", "plain file", 0644);
    fixture.write("working/probe", "#!/bin/sh\nprintf 'found:%s\\n' \"$1\"\n");
    EnvironmentVariable path("PATH");
    path.set(fixture.path("missing") + ':' + fixture.path("denied") + ':' + fixture.path("not-a-directory") + ':' +
             fixture.path("working"));
    const auto result = run({"probe", "argument"});
    assert(result.success && result.output == "found:argument\n");
    assert(!run({"does-not-exist"}).success);
    assert(!run({fixture.path("denied/probe")}).success); // Explicit paths never search PATH.
    assert(!run({""}).success);
}

void test_shell_fallback_and_empty_or_unset_path()
{
    Fixture fixture;
    fixture.write("working/plain-script", "printf 'script:%s\\n' \"$1\"\n");
    EnvironmentVariable path("PATH");
    path.set(fixture.path("working"));
    const auto result = run({"plain-script", "argument ; $HOME"});
    assert(result.success && result.output == "script:argument ; $HOME\n");

    const auto original_directory = std::filesystem::current_path();
    std::filesystem::current_path(fixture.path("working"));
    path.set(""); // An empty PATH component means the current directory.
    const auto current = run({"plain-script", "current"});
    std::filesystem::current_path(original_directory);
    assert(current.success && current.output == "script:current\n");

    path.unset();
    const auto default_path = run({"sh", "-c", "printf 'default-path\\n'"});
    assert(default_path.success && default_path.output == "default-path\n");
}

void test_output_limit_and_timeout_still_apply()
{
    const auto too_large = run({"/bin/sh", "-c", "while :; do printf '%01024d' 0; done"});
    assert(!too_large.success);
    assert(too_large.output.size() == 1024U * 1024U);
    const auto timed_out = run({"/bin/sh", "-c", "sleep 10 & wait"}, {}, 25ms);
    assert(!timed_out.success && timed_out.output == "Command timed out");
}

}  // namespace

int main()
{
    test_environment_arguments_and_stdin_during_background_allocations();
    test_path_search_continues_after_missing_and_inaccessible_entries();
    test_shell_fallback_and_empty_or_unset_path();
    test_output_limit_and_timeout_still_apply();
}
