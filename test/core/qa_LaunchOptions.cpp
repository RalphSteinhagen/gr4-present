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
    "presentation and load name the same package, whichever case they are written in"_test = [] {
        for (const std::string_view spelling : {"presentation", "load", "PRESENTATION", "Load"}) {
            LaunchOptions options;
            LaunchOptions::parseUrlParameters(options, std::string{spelling} + "=talks/demo");
            expect(options.presentationBase().has_value()) << spelling;
            expect(eq(options.presentationBase().value_or(""), std::string_view{"talks/demo"})) << spelling;
        }
    };

    "presentation wins when a link carries both spellings"_test = [] {
        LaunchOptions options;
        LaunchOptions::parseUrlParameters(options, "load=old/package&presentation=new/package");
        expect(eq(options.presentationBase().value_or(""), std::string_view{"new/package"}));
    };

    "no package named means none reported"_test = [] {
        LaunchOptions options;
        LaunchOptions::parseUrlParameters(options, "mode=windowed");
        expect(!options.presentationBase().has_value());
    };

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
