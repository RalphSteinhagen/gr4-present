#ifndef GR4_PRESENT_DECK_FILES_HPP
#define GR4_PRESENT_DECK_FILES_HPP

#include <gr4-present/Navigation.hpp>
#include <gr4-present/PresentationLoader.hpp>
#include <gr4-present/Tar.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// a deck as Load reads it from this computer and Save writes it there: a folder of the package's files, or, where a
/// browser cannot write a folder, `<name>.tar.gz` holding that folder

/// the folder (and archive) name Save writes a deck to: its title with every character a file system or a shell trips
/// over as `-`, else the name of the folder it came from, else `deck`
[[nodiscard]] std::string deckFolderName(std::string_view title, std::string_view folder);

/// the files under `<folder>/`, as a gzip-compressed tar archive
[[nodiscard]] std::expected<std::vector<std::byte>, std::string> packDeck(std::span<const PackageFile> files, std::string_view folder);

inline constexpr std::size_t kLargestUnpackedDeckBytes = 512UZ << 20U; // a deck with its videos; a browser holds it in memory

/// what to load of a deck's files, given as `/`-separated paths relative to its root (a folder or an archive): the
/// folder of the least nested `index.yml`, else the least nested `.md`, the first in alphabetical order on a tie; `.` for
/// the root itself
[[nodiscard]] std::expected<std::string, std::string> deckBaseAmong(std::span<const std::string> paths);

/// a `.tar.gz` deck's files, and what to load of them (`deckBaseAmong`)
struct UnpackedDeck {
    std::vector<gr::compression::TarEntry> files;
    std::string                            base;
};
[[nodiscard]] std::expected<UnpackedDeck, std::string> unpackDeck(std::span<const std::byte> archive);

/// the package-relative path of the entry document a manifest names, or why it names none this viewer shows: the YAML
/// does not parse, it asks for a newer viewer, or its entry lies outside the package
[[nodiscard]] std::expected<std::string, std::string> entryOfManifest(std::string_view manifest);

/// where a reloaded deck resumes: the view and step shown if the view is still there (the step at most its last),
/// else the nearest view before it in `viewsBefore` that is, else the first view
[[nodiscard]] Cursor cursorAfterReload(std::span<const std::string> viewsBefore, const Cursor& before, const NavigationGraph& after);

} // namespace gr::present

#endif // GR4_PRESENT_DECK_FILES_HPP
