// gr4-present-bundle: the viewer's own "download viewer + deck" (packPresenterBundle), made from a built viewer-web/ and a
// deck folder, so a release archive and the in-app download cannot drift; only kSiteFiles are taken from viewer-web/

#include "PresenterBundle.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv) try {
    using namespace gr::present;
    std::map<std::string, std::string, std::less<>> options;
    for (int index = 1; index + 1 < argc; index += 2) {
        if (const std::string_view key{argv[index]}; key.starts_with("--")) {
            options[std::string{key.substr(2UZ)}] = argv[index + 1];
        }
    }
    for (const std::string_view wanted : {"viewer", "deck", "site", "title", "folder", "out"}) {
        if (!options.contains(wanted)) {
            std::println(stderr, "usage: gr4-present-bundle --viewer <viewer-web> --deck <deck> --site <url> --title <text> --folder <name> --out <file.tar.gz>; --{} is missing", wanted);
            return 2;
        }
    }

    std::deque<std::vector<std::uint8_t>> held; // what the PackageFiles' spans look at; a deque never moves them
    const auto                            packageFile = [&held](const std::filesystem::path& folder, const std::filesystem::path& path) {
        // read by size: GCC 15 at -O2 reports a false null dereference inside istreambuf_iterator, an error under -Werror
        std::error_code sizeError;
        const auto      size = std::filesystem::file_size(folder / path, sizeError);
        if (sizeError) {
            return std::optional<PackageFile>{};
        }
        auto&         bytes = held.emplace_back(static_cast<std::size_t>(size));
        std::ifstream file{folder / path, std::ios::binary};
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            return std::optional<PackageFile>{};
        }
        return std::optional{PackageFile{.path = path.generic_string(), .bytes = bytes}};
    };
    const auto unreadable = [](const std::filesystem::path& path) {
        std::println(stderr, "could not read {}", path.string());
        return 1;
    };

    const std::filesystem::path viewer = options["viewer"];
    std::vector<PackageFile>    site;
    for (const std::string_view name : kSiteFiles) {
        if (!std::filesystem::is_regular_file(viewer / name)) {
            std::println(stderr, "{} has no {}: is it a built viewer-web folder?", viewer.string(), name);
            return 1;
        }
        auto file = packageFile(viewer, name);
        if (!file.has_value()) {
            return unreadable(viewer / name);
        }
        site.push_back(*std::move(file));
    }
    const std::filesystem::path        deck = options["deck"];
    std::vector<std::filesystem::path> deckPaths;
    std::error_code                    walkError;
    for (auto entry = std::filesystem::recursive_directory_iterator(deck, walkError); !walkError && entry != std::filesystem::recursive_directory_iterator{}; entry.increment(walkError)) {
        if (entry->is_regular_file()) {
            deckPaths.push_back(std::filesystem::relative(entry->path(), deck));
        }
    }
    if (walkError) {
        std::println(stderr, "could not read the deck folder {}: {}", deck.string(), walkError.message());
        return 1;
    }
    std::ranges::sort(deckPaths); // the same deck packs to the same archive
    std::vector<PackageFile> deckFiles;
    for (const std::filesystem::path& path : deckPaths) {
        auto file = packageFile(deck, path);
        if (!file.has_value()) {
            return unreadable(deck / path);
        }
        deckFiles.push_back(*std::move(file));
    }

    const auto packed = packPresenterBundle(options["folder"], PresenterBundle{.site = options["site"], .title = options["title"], .siteFiles = site, .deckFiles = deckFiles});
    if (!packed) {
        std::println(stderr, "packing failed: {}", packed.error());
        return 1;
    }
    std::ofstream out(options["out"], std::ios::binary);
    out.write(reinterpret_cast<const char*>(packed->data()), static_cast<std::streamsize>(packed->size()));
    if (!out) {
        std::println(stderr, "could not write {}", options["out"]);
        return 1;
    }
    std::println("{}: {} site files, {} deck files, {} bytes", options["out"], site.size(), deckFiles.size(), packed->size());
    return 0;
} catch (const std::exception& error) {
    static_cast<void>(std::fprintf(stderr, "%s\n", error.what()));
    return 1;
} catch (...) {
    return 1;
}
