#include <gr4-present/DeckFiles.hpp>
#include <gr4-present/Manifest.hpp>
#include <gr4-present/PackagePath.hpp>

#include <gnuradio-4.0/Compression.hpp>

#include <algorithm>
#include <format>
#include <ranges>

namespace gr::present {

std::string deckFolderName(std::string_view title, std::string_view folder) {
    const auto safe = [](std::string_view name) {
        const auto  kept = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'; };
        std::string replaced;
        for (const char c : name) {
            const char put = kept(c) ? c : '-';
            if (put != '-' || !replaced.ends_with('-')) {
                replaced.push_back(put);
            }
        }
        const auto first = replaced.find_first_not_of("-.");
        const auto last  = replaced.find_last_not_of("-.");
        return first == std::string::npos ? std::string{} : replaced.substr(first, last - first + 1UZ);
    };
    const std::string fromFolder = safe(folder.substr(folder.find_last_of('/') == std::string_view::npos ? 0UZ : folder.find_last_of('/') + 1UZ));
    if (std::string fromTitle = safe(title); !fromTitle.empty()) {
        return fromTitle;
    }
    return fromFolder.empty() ? std::string{"deck"} : fromFolder;
}

std::expected<std::vector<std::byte>, std::string> packDeck(std::span<const PackageFile> files, std::string_view folder) {
    const auto inFolder = [folder](const PackageFile& file) {
        const auto bytes = std::as_bytes(file.bytes);
        return gr::compression::TarEntry{.path = std::format("{}/{}", folder, file.path), .bytes = {bytes.begin(), bytes.end()}};
    };
    const auto entries = files | std::views::transform(inFolder) | std::ranges::to<std::vector<gr::compression::TarEntry>>();
    const auto archive = gr::compression::packTar(entries);
    if (!archive) {
        return std::unexpected(archive.error());
    }
    auto packed = gr::compression::compress(*archive, gr::compression::Format::gzip);
    if (!packed) {
        return std::unexpected(std::format("gzip failed (error {})", static_cast<int>(packed.error())));
    }
    return std::move(*packed);
}

std::expected<std::string, std::string> deckBaseAmong(std::span<const std::string> paths) {
    auto ordered = std::vector<std::string>{paths.begin(), paths.end()};
    std::ranges::sort(ordered, {}, [](const std::string& path) { return std::pair{std::ranges::count(path, '/'), std::string_view{path}}; });
    const std::string nested     = std::format("/{}", PresentationLoader::kManifestName);
    const auto        isManifest = [&nested](std::string_view path) { return path == PresentationLoader::kManifestName || path.ends_with(nested); };
    if (const auto manifest = std::ranges::find_if(ordered, isManifest); manifest != ordered.end()) {
        return manifest->contains('/') ? manifest->substr(0UZ, manifest->rfind('/')) : std::string{"."};
    }
    if (const auto document = std::ranges::find_if(ordered, [](std::string_view path) { return path.ends_with(".md"); }); document != ordered.end()) {
        return *document;
    }
    return std::unexpected(std::format("it holds no {} and no .md", PresentationLoader::kManifestName));
}

std::expected<UnpackedDeck, std::string> unpackDeck(std::span<const std::byte> archive) {
    const auto unzipped = gr::compression::decompress(archive, gr::compression::Format::gzip, kLargestUnpackedDeckBytes);
    if (!unzipped) {
        if (unzipped.error() == gr::compression::Error::sizeLimitExceeded) {
            return std::unexpected(std::format("unpacked, it would exceed {} MiB", kLargestUnpackedDeckBytes >> 20U));
        }
        return std::unexpected(std::string{"it is no gzip-compressed archive, or a damaged one"});
    }
    auto files = gr::compression::unpackTar(*unzipped);
    if (!files) {
        return std::unexpected(files.error());
    }
    const auto paths = *files | std::views::transform(&gr::compression::TarEntry::path) | std::ranges::to<std::vector<std::string>>();
    auto       base  = deckBaseAmong(paths);
    if (!base) {
        return std::unexpected(base.error());
    }
    return UnpackedDeck{.files = std::move(*files), .base = std::move(*base)};
}

std::expected<std::string, std::string> entryOfManifest(std::string_view manifest) {
    const auto parsed = parseManifest(manifest);
    if (!parsed) {
        return std::unexpected(parsed.error().message());
    }
    const auto entry = resolvePackagePath({}, parsed->entry);
    if (!entry || entry->external) {
        return std::unexpected(std::format("its entry {} lies outside the package", parsed->entry));
    }
    return entry->path;
}

Cursor cursorAfterReload(std::span<const std::string> viewsBefore, const Cursor& before, const NavigationGraph& after) {
    if (after.views.empty()) {
        return before;
    }
    if (const View* same = after.find(before.viewId); same != nullptr) {
        return Cursor{.viewId = same->id, .step = std::min(before.step, same->stepCount - 1UZ)};
    }
    const auto shown = std::ranges::find(viewsBefore, before.viewId);
    for (const std::string& earlier : std::ranges::subrange(viewsBefore.begin(), shown) | std::views::reverse) {
        if (after.find(earlier) != nullptr) {
            return Cursor{.viewId = earlier, .step = 0UZ};
        }
    }
    return Cursor{.viewId = after.views.front().id, .step = 0UZ};
}

} // namespace gr::present
