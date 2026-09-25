#include <boost/ut.hpp>

#include <gr4-present/LaunchOptions.hpp>

#include <array>
#include <string_view>

using namespace boost::ut;
using namespace gr::present;

namespace {
[[nodiscard]] LaunchOptions fromCommandLine(std::initializer_list<std::string_view> arguments) {
    const std::vector<std::string_view> stored{arguments};
    return LaunchOptions::from("", "", stored);
}
} // namespace

const suite<"LaunchOptions"> launchOptionTests = [] {
    "fullscreen unless the viewer asks otherwise"_test = [] {
        expect(LaunchOptions{}.windowMode() == WindowMode::fullscreen);
        expect(fromCommandLine({"--windowed"}).windowMode() == WindowMode::windowed);
        expect(fromCommandLine({"--window"}).windowMode() == WindowMode::windowed);
        expect(fromCommandLine({"--fullscreen"}).windowMode() == WindowMode::fullscreen);
    };

    "the mode is matched whatever its casing"_test = [] {
        expect(LaunchOptions::from("", "#mode=FullScreen", {}).windowMode() == WindowMode::fullscreen);
        expect(LaunchOptions::from("", "#MODE=WINDOWED", {}).windowMode() == WindowMode::windowed);
        expect(LaunchOptions::from("", "#Mode=Windowed", {}).windowMode() == WindowMode::windowed);
    };

    "a fragment overrides a query, and a command line overrides both"_test = [] {
        expect(LaunchOptions::from("?mode=windowed", "#mode=fullscreen", {}).windowMode() == WindowMode::fullscreen);

        const std::vector<std::string_view> arguments{"--windowed"};
        expect(LaunchOptions::from("?mode=fullscreen", "#mode=fullscreen", arguments).windowMode() == WindowMode::windowed);
    };

    "arbitrary parameters survive for later use"_test = [] {
        const auto options = LaunchOptions::from("?presentation=demo/talk.yaml&theme=dark", "#mode=fullscreen", {});
        expect(options.value("presentation") == std::optional{std::string_view{"demo/talk.yaml"}});
        expect(options.value("theme") == std::optional{std::string_view{"dark"}});
        expect(options.windowMode() == WindowMode::fullscreen);
    };

    "an unknown parameter is absent rather than empty"_test = [] {
        const auto options = LaunchOptions::from("?a=1", "", {});
        expect(!options.contains("b"));
        expect(!options.value("b").has_value());
        expect(options.contains("a"));
    };

    "leading ? and # are optional"_test = [] {
        expect(LaunchOptions::from("mode=windowed", "", {}).windowMode() == WindowMode::windowed);
        expect(LaunchOptions::from("", "mode=windowed", {}).windowMode() == WindowMode::windowed);
    };

    "a valueless parameter is present with an empty value"_test = [] {
        const auto options = LaunchOptions::from("?verbose", "", {});
        expect(options.contains("verbose"));
        expect(options.value("verbose") == std::optional{std::string_view{""}});
    };

    "both command-line spellings are accepted"_test = [] {
        expect(fromCommandLine({"--presentation=demo/a.yaml"}).value("presentation") == std::optional{std::string_view{"demo/a.yaml"}});
        expect(fromCommandLine({"--presentation", "demo/b.yaml"}).value("presentation") == std::optional{std::string_view{"demo/b.yaml"}});
    };

    "an option followed by another option takes no value"_test = [] {
        const auto options = fromCommandLine({"--verbose", "--windowed"});
        expect(options.value("verbose") == std::optional{std::string_view{""}});
        expect(options.windowMode() == WindowMode::windowed);
    };

    "non-option arguments are ignored"_test = [] {
        const auto options = fromCommandLine({"/usr/bin/gr4-present", "stray", "--windowed"});
        expect(options.windowMode() == WindowMode::windowed);
        expect(!options.contains("stray"));
    };

    "the last spelling of a repeated key wins"_test = [] { expect(LaunchOptions::from("?theme=light&theme=dark", "", {}).value("theme") == std::optional{std::string_view{"dark"}}); };

    "empty input yields no parameters"_test = [] {
        const auto options = LaunchOptions::from("", "", {});
        expect(options.entries.empty());
        expect(options.windowMode() == WindowMode::fullscreen);
    };
};

int main() { return 0; }
