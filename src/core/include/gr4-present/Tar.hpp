#ifndef GR4_PRESENT_TAR_HPP
#define GR4_PRESENT_TAR_HPP

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::compression {

/// POSIX ustar archives (IEEE Std 1003.1-2017, "pax", ustar interchange format) of regular files held in memory: the
/// container a `.tar.gz` puts around several files before gzip (GR4's Compression.hpp) compresses them. Directories are
/// implied by the paths. Reading takes what GNU tar writes: ustar, its `L` long names and pax `path` records; links,
/// devices and other entries are skipped. Std only, so that it can join Compression.hpp in GR4.
struct TarEntry {
    std::string            path; // relative, `/`-separated
    std::vector<std::byte> bytes;

    bool operator==(const TarEntry&) const = default;
};

namespace detail::tar {

inline constexpr std::size_t kBlock = 512UZ;

struct Field {
    std::size_t offset;
    std::size_t size;
};
inline constexpr Field kName{0UZ, 100UZ};
inline constexpr Field kMode{100UZ, 8UZ};
inline constexpr Field kUid{108UZ, 8UZ};
inline constexpr Field kGid{116UZ, 8UZ};
inline constexpr Field kSize{124UZ, 12UZ};
inline constexpr Field kModified{136UZ, 12UZ};
inline constexpr Field kChecksum{148UZ, 8UZ};
inline constexpr Field kType{156UZ, 1UZ};
inline constexpr Field kMagic{257UZ, 6UZ};
inline constexpr Field kVersion{263UZ, 2UZ};
inline constexpr Field kPrefix{345UZ, 155UZ};

inline constexpr std::uint64_t kLargestSize = 077777777777ULL; // eleven octal digits: 8 GiB, more than a 32-bit size_t holds

[[nodiscard]] inline std::span<std::byte>       fieldOf(std::span<std::byte, kBlock> header, Field field) { return header.subspan(field.offset, field.size); }
[[nodiscard]] inline std::span<const std::byte> fieldOf(std::span<const std::byte, kBlock> header, Field field) { return header.subspan(field.offset, field.size); }

inline void putText(std::span<std::byte> field, std::string_view text) {
    std::ranges::copy(text | std::views::transform([](char c) { return static_cast<std::byte>(c); }), field.begin());
}

/// `size - 1` octal digits, zero-padded, then NUL
inline void putOctal(std::span<std::byte> field, std::size_t value) { putText(field, std::format("{:0{}o}", value, field.size() - 1UZ)); }

/// a field's text up to its first NUL
[[nodiscard]] inline std::string textOf(std::span<const std::byte> field) {
    const auto end = std::ranges::find(field, std::byte{0});
    return std::ranges::subrange(field.begin(), end) | std::views::transform([](std::byte b) { return static_cast<char>(b); }) | std::ranges::to<std::string>();
}

/// an octal number, with the spaces and NULs that pad it on either side
[[nodiscard]] inline std::expected<std::size_t, std::string> octalOf(std::span<const std::byte> field) {
    const std::string text    = textOf(field);
    const auto        first   = text.find_first_not_of(' ');
    const auto        last    = text.find_last_not_of(' ');
    std::size_t       value   = 0UZ;
    const std::string trimmed = first == std::string::npos ? std::string{} : text.substr(first, last - first + 1UZ);
    if (trimmed.empty()) {
        return 0UZ;
    }
    const char* const digitsEnd = trimmed.data() + trimmed.size();
    if (const auto [end, error] = std::from_chars(trimmed.data(), digitsEnd, value, 8); error != std::errc{} || end != digitsEnd) {
        return std::unexpected(std::format("'{}' is no octal number", trimmed));
    }
    return value;
}

/// the sum of the header's bytes with the checksum field read as spaces
[[nodiscard]] inline std::size_t checksumOf(std::span<const std::byte, kBlock> header) {
    std::size_t sum = 0UZ;
    for (std::size_t index = 0UZ; index < kBlock; ++index) {
        const bool inChecksum = index >= kChecksum.offset && index < kChecksum.offset + kChecksum.size;
        sum += inChecksum ? std::size_t{' '} : std::to_integer<std::size_t>(header[index]);
    }
    return sum;
}

/// ustar keeps a path of up to 255 characters as prefix and name, split at a `/`
[[nodiscard]] inline std::expected<std::pair<std::string_view, std::string_view>, std::string> splitPath(std::string_view path) {
    if (path.size() <= kName.size) {
        return std::pair{std::string_view{}, path};
    }
    for (std::size_t slash = path.find('/'); slash != std::string_view::npos; slash = path.find('/', slash + 1UZ)) {
        if (slash <= kPrefix.size && path.size() - slash - 1UZ <= kName.size) {
            return std::pair{path.substr(0UZ, slash), path.substr(slash + 1UZ)};
        }
    }
    return std::unexpected(std::format("'{}' is too long for a ustar header", path));
}

/// a path an archive may not hold: absolute, or leaving the directory it is unpacked into
[[nodiscard]] inline bool leavesTheArchive(std::string_view path) {
    const auto upwards = [](auto part) { return std::string_view{part} == ".."; };
    return path.empty() || path.starts_with('/') || std::ranges::any_of(path | std::views::split('/'), upwards);
}

[[nodiscard]] inline std::string withoutDotSlash(std::string path) {
    while (path.starts_with("./")) {
        path.erase(0UZ, 2UZ);
    }
    return path;
}

/// the `path` of pax extended-header records, `<length> <key>=<value>\n` each
[[nodiscard]] inline std::string paxPathOf(std::string_view records) {
    std::string path;
    while (!records.empty()) {
        const auto  space  = records.find(' ');
        std::size_t length = 0UZ;
        if (space == std::string_view::npos) {
            break;
        }
        const std::string_view digits = records.substr(0UZ, space);
        if (std::from_chars(digits.data(), digits.data() + digits.size(), length).ec != std::errc{} || length <= space || length > records.size()) {
            break;
        }
        const std::string_view record = records.substr(space + 1UZ, length - space - 2UZ); // without the trailing newline
        if (record.starts_with("path=")) {
            path = std::string{record.substr(5UZ)};
        }
        records.remove_prefix(length);
    }
    return path;
}

} // namespace detail::tar

/// the files as one archive, each a mode 0644 file dated 0, ended by two zero blocks
[[nodiscard]] inline std::expected<std::vector<std::byte>, std::string> packTar(std::span<const TarEntry> entries) {
    using namespace detail::tar;
    std::vector<std::byte> archive;
    for (const TarEntry& entry : entries) {
        if (leavesTheArchive(entry.path)) {
            return std::unexpected(std::format("'{}' is no relative path inside the archive", entry.path));
        }
        if (std::cmp_greater(entry.bytes.size(), kLargestSize)) {
            return std::unexpected(std::format("'{}' is too large for a ustar header", entry.path));
        }
        const auto split = splitPath(entry.path);
        if (!split) {
            return std::unexpected(split.error());
        }
        std::array<std::byte, kBlock>      block{};
        const std::span<std::byte, kBlock> header{block};
        putText(fieldOf(header, kName), split->second);
        putText(fieldOf(header, kPrefix), split->first);
        putOctal(fieldOf(header, kMode), 0644UZ);
        putOctal(fieldOf(header, kUid), 0UZ);
        putOctal(fieldOf(header, kGid), 0UZ);
        putOctal(fieldOf(header, kSize), entry.bytes.size());
        putOctal(fieldOf(header, kModified), 0UZ);
        putText(fieldOf(header, kType), "0");
        putText(fieldOf(header, kMagic), std::string_view{"ustar\0", 6UZ});
        putText(fieldOf(header, kVersion), "00");
        putText(fieldOf(header, kChecksum), std::format("{:06o}", checksumOf(header)) + std::string{"\0 ", 2UZ});
        archive.insert(archive.end(), block.begin(), block.end());
        archive.insert(archive.end(), entry.bytes.begin(), entry.bytes.end());
        archive.resize(archive.size() + (kBlock - entry.bytes.size() % kBlock) % kBlock, std::byte{0});
    }
    archive.resize(archive.size() + 2UZ * kBlock, std::byte{0});
    return archive;
}

/// the regular files of an archive, in its order; a damaged header, a truncated file or a path leaving the archive fails it
[[nodiscard]] inline std::expected<std::vector<TarEntry>, std::string> unpackTar(std::span<const std::byte> archive) {
    using namespace detail::tar;
    std::vector<TarEntry> entries;
    std::string           nextPath; // from a GNU `L` entry or a pax `x` header, for the entry that follows
    std::size_t           at = 0UZ;
    while (at + kBlock <= archive.size()) {
        const std::span<const std::byte, kBlock> header{archive.subspan(at, kBlock)};
        if (std::ranges::all_of(header, [](std::byte b) { return b == std::byte{0}; })) {
            break;
        }
        const auto stored = octalOf(fieldOf(header, kChecksum));
        if (!stored || *stored != checksumOf(header)) {
            return std::unexpected(std::format("the header at byte {} is damaged: its checksum does not match", at));
        }
        const auto size = octalOf(fieldOf(header, kSize));
        if (!size) {
            return std::unexpected(std::format("the header at byte {} gives no size: {}", at, size.error()));
        }
        const std::size_t dataAt = at + kBlock;
        if (*size > archive.size() - dataAt) {
            return std::unexpected(std::format("the archive ends inside the file at byte {}", at));
        }
        const std::span<const std::byte> data = archive.subspan(dataAt, *size);
        const char                       type = static_cast<char>(fieldOf(header, kType)[0]);
        at                                    = dataAt + (*size + kBlock - 1UZ) / kBlock * kBlock;

        if (type == 'L') {
            nextPath = textOf(data);
            continue;
        }
        if (type == 'x') {
            nextPath = paxPathOf(data | std::views::transform([](std::byte b) { return static_cast<char>(b); }) | std::ranges::to<std::string>());
            continue;
        }
        if (type != '0' && type != '\0') {
            nextPath.clear();
            continue; // a directory, a link, a device, a global pax header: no file to keep
        }
        // GNU's own format (magic "ustar  ") keeps access and change times where POSIX keeps the prefix
        const bool        posix      = textOf(fieldOf(header, kMagic)) == "ustar";
        const std::string prefix     = posix ? textOf(fieldOf(header, kPrefix)) : std::string{};
        const std::string name       = textOf(fieldOf(header, kName));
        std::string       headerPath = prefix.empty() ? name : std::format("{}/{}", prefix, name);
        std::string       path       = withoutDotSlash(!nextPath.empty() ? std::move(nextPath) : std::move(headerPath));
        nextPath.clear();
        if (leavesTheArchive(path)) {
            return std::unexpected(std::format("'{}' leaves the archive", path));
        }
        entries.push_back(TarEntry{.path = std::move(path), .bytes = {data.begin(), data.end()}});
    }
    return entries;
}

} // namespace gr::compression

#endif // GR4_PRESENT_TAR_HPP
