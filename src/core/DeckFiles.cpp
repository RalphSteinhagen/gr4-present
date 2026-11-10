#include <gr4-present/DeckFiles.hpp>
#include <gr4-present/Manifest.hpp>
#include <gr4-present/PackagePath.hpp>

#include <gnuradio-4.0/Compression.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <new>
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
    const auto inFolder = [folder](const PackageFile& file) { return gr::compression::TarSource{.path = std::format("{}/{}", folder, file.path), .bytes = std::as_bytes(file.bytes)}; };
    const auto sources  = files | std::views::transform(inFolder) | std::ranges::to<std::vector<gr::compression::TarSource>>();
    return packTarGz(sources);
}

std::expected<std::vector<std::byte>, std::string> packTarGz(std::span<const gr::compression::TarSource> files) {
    static constexpr std::array<std::byte, 2UZ * 512UZ> kZeros{};
    gr::compression::StreamCompressor                   compressor;
    std::vector<std::byte>                              packed;
    std::vector<std::byte>                              chunk(1UZ << 16U); // on the heap: an Emscripten thread's stack is 64 kB
    const auto                                          feed = [&](std::span<const std::byte> input, gr::compression::Flush flush) -> std::expected<void, std::string> {
        while (true) {
            const auto step = compressor.process(input, chunk, flush);
            if (!step) [[unlikely]] {
                return std::unexpected(std::format("gzip failed (error {})", static_cast<int>(step.error())));
            }
            packed.insert(packed.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(step->produced));
            input = input.subspan(step->consumed);
            if (step->finished() || (flush == gr::compression::Flush::none && input.empty() && step->status == gr::compression::StreamStatus::needInput)) {
                return {};
            }
        }
    };
#if __cpp_exceptions
    try {
#endif
        for (const gr::compression::TarSource& file : files) {
            const auto header = gr::compression::tarHeader(file.path, file.bytes.size(), file.mode);
            if (!header) {
                return std::unexpected(header.error());
            }
            for (const std::span<const std::byte> part : {std::span<const std::byte>{*header}, file.bytes, std::span{kZeros}.first(gr::compression::tarPadding(file.bytes.size()))}) {
                if (auto fed = feed(part, gr::compression::Flush::none); !fed) [[unlikely]] {
                    return std::unexpected(fed.error());
                }
            }
        }
        if (auto ended = feed(kZeros, gr::compression::Flush::finish); !ended) [[unlikely]] {
            return std::unexpected(ended.error());
        }
#if __cpp_exceptions
    } catch (const std::bad_alloc&) {
        return std::unexpected(std::format("not enough memory for the archive, {} MB packed so far", packed.size() >> 20U));
    }
#endif
    return packed;
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

std::expected<void, std::string> writeFiles(const std::filesystem::path& directory, std::span<const PackageFile> files) {
    for (const PackageFile& file : files) {
        const std::filesystem::path target = directory / file.path;
        std::error_code             error;
        std::filesystem::create_directories(target.parent_path(), error);
        std::ofstream out{target, std::ios::binary};
        out.write(reinterpret_cast<const char*>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
        if (error || !out) {
            return std::unexpected(std::format("{}: cannot be written", target.string()));
        }
    }
    return {};
}

std::filesystem::path deckWithin(const std::filesystem::path& folder, std::string_view base) { return base == "." ? folder : (folder / base).lexically_normal(); }

std::expected<std::filesystem::path, std::string> writeUnpackedDeck(const std::filesystem::path& folder, const UnpackedDeck& deck) {
    const auto asPackageFile = [](const gr::compression::TarEntry& entry) {
        const std::span<const std::uint8_t> bytes{reinterpret_cast<const std::uint8_t*>(entry.bytes.data()), entry.bytes.size()};
        return PackageFile{.path = entry.path, .bytes = bytes};
    };
    const auto files = deck.files | std::views::transform(asPackageFile) | std::ranges::to<std::vector<PackageFile>>();
    return writeFiles(folder, files).transform([&] { return deckWithin(folder, deck.base); });
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
