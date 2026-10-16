#include <boost/ut.hpp>

#include <gr4-present/Tar.hpp>

#include <gnuradio-4.0/Compression.hpp>

#include "FileContents.hpp"

#include <string>
#include <vector>

using namespace boost::ut;
using gr::compression::packTar;
using gr::compression::TarEntry;
using gr::compression::unpackTar;

namespace {

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
    return text | std::views::transform([](char c) { return static_cast<std::byte>(c); }) | std::ranges::to<std::vector<std::byte>>();
}

[[nodiscard]] std::string textOf(std::span<const std::byte> bytes) {
    return bytes | std::views::transform([](std::byte b) { return static_cast<char>(b); }) | std::ranges::to<std::string>();
}

// test/fixtures/tar: made by GNU tar 1.35 (`--format=gnu|pax|ustar --owner=0 --group=0 --mtime=@0 --sort=name`), gnu.tar.gz by `gzip -9 -n`;
// links.tar holds `./d/`, `./d/f` and `./link` (a symbolic link to d/f), as `tar -cf links.tar ./d ./link` writes them
// boost::ut runs the suites at exit, after this file's std::string globals are gone: text constants are views
constexpr std::string_view kManifest = "format: gr4-presentation/1\nentry: talk.md\n";
constexpr std::string_view kTalk     = "# Hello\n";

[[nodiscard]] std::string longName() { return "figures/" + std::string(120UZ, 'a') + ".svg"; }
[[nodiscard]] std::string nestedPath() { return std::string(60UZ, 'd') + "/" + std::string(60UZ, 'e') + "/f.txt"; }

[[nodiscard]] std::vector<std::byte> fixture(std::string_view name) { return fileContents<std::vector<std::byte>>(std::filesystem::path{GR4_PRESENT_TAR_FIXTURES} / name); }

} // namespace

const suite<"Tar"> tarTests = [] {
    "GNU tar's own format with a long name (an L entry) reads back every file"_test = [] {
        const auto entries = unpackTar(fixture("gnu.tar"));
        expect(fatal(entries.has_value())) << (entries ? std::string{} : entries.error());
        expect(fatal(eq(entries->size(), 3UZ)));
        expect(eq((*entries)[0].path, std::string{"index.yml"}));
        expect(eq(textOf((*entries)[0].bytes), std::string{kManifest}));
        expect(eq((*entries)[1].path, std::string{"talk.md"}));
        expect(eq(textOf((*entries)[1].bytes), std::string{kTalk}));
        expect(eq((*entries)[2].path, longName()));
        expect(eq(textOf((*entries)[2].bytes), std::string{"<svg/>\n"}));
    };

    "a .tar.gz from GNU tar and gzip unpacks through GR4's gzip, as Load reads one"_test = [] {
        const auto archive = gr::compression::decompress(fixture("gnu.tar.gz"), gr::compression::Format::gzip);
        expect(fatal(archive.has_value()));
        expect(*archive == fixture("gnu.tar")) << "gzip -9 -n of gnu.tar";
        const auto entries = unpackTar(*archive);
        expect(fatal(entries.has_value()));
        expect(eq(entries->size(), 3UZ));
    };

    "a pax archive's long name comes from its path record"_test = [] {
        const auto entries = unpackTar(fixture("pax.tar"));
        expect(fatal(entries.has_value())) << (entries ? std::string{} : entries.error());
        expect(fatal(eq(entries->size(), 3UZ)));
        expect(eq((*entries)[2].path, longName()));
        expect(eq(textOf((*entries)[0].bytes), std::string{kManifest}));
    };

    "a POSIX ustar archive joins prefix and name; its directories are not files"_test = [] {
        const auto entries = unpackTar(fixture("ustar.tar"));
        expect(fatal(entries.has_value())) << (entries ? std::string{} : entries.error());
        expect(fatal(eq(entries->size(), 3UZ)));
        expect(eq((*entries)[2].path, nestedPath()));
        expect(eq(textOf((*entries)[2].bytes), std::string{"nested\n"}));
    };

    "what packTar writes reads back as the same files, in order"_test = [] {
        const std::vector<TarEntry> files{{.path = "index.yml", .bytes = bytesOf(kManifest)}, {.path = nestedPath(), .bytes = bytesOf(kTalk)}, {.path = "empty", .bytes = {}}};
        const auto                  packed = packTar(files);
        expect(fatal(packed.has_value()));
        const auto unpacked = unpackTar(*packed);
        expect(fatal(unpacked.has_value()));
        expect(*unpacked == files);
    };

    "a header follows ustar: magic, version, size and a checksum over the header with the field as spaces"_test = [] {
        const auto packed = packTar(std::vector<TarEntry>{{.path = "talk.md", .bytes = bytesOf(kTalk)}});
        expect(fatal(packed.has_value()));
        expect(eq(packed->size() % 512UZ, 0UZ));
        expect(eq(packed->size(), 512UZ + 512UZ + 2UZ * 512UZ)) << "header, one block of data, two zero blocks";
        const std::span<const std::byte> header{packed->data(), 512UZ};
        expect(eq(textOf(header.subspan(257UZ, 6UZ)), std::string{"ustar\0", 6UZ}));
        expect(eq(textOf(header.subspan(263UZ, 2UZ)), std::string{"00"}));
        expect(eq(textOf(header.subspan(124UZ, 12UZ)), std::string{"00000000010\0", 12UZ})) << "8 bytes in octal";
        std::size_t sum = 0UZ;
        for (std::size_t index = 0UZ; index < 512UZ; ++index) {
            sum += index >= 148UZ && index < 156UZ ? 32UZ : std::to_integer<std::size_t>(header[index]);
        }
        expect(eq(textOf(header.subspan(148UZ, 8UZ)), std::format("{:06o}", sum) + std::string{"\0 ", 2UZ}));
    };

    "a path too long to split at a slash, or leaving the archive, is refused"_test = [] {
        expect(!packTar(std::vector<TarEntry>{{.path = std::string(101UZ, 'x'), .bytes = {}}}).has_value());
        expect(!packTar(std::vector<TarEntry>{{.path = "../outside", .bytes = {}}}).has_value());
        expect(!packTar(std::vector<TarEntry>{{.path = "/etc/passwd", .bytes = {}}}).has_value());
    };

    "a damaged header, a cut-off file or a file outside the archive is refused"_test = [] {
        const auto packed = packTar(std::vector<TarEntry>{{.path = "aa/x", .bytes = {}}});
        expect(fatal(packed.has_value()));
        auto damaged    = *packed;
        damaged.at(0UZ) = std::byte{'X'}; // the name changes, the checksum does not
        expect(!unpackTar(damaged).has_value());

        auto escaping    = *packed; // `aa/x` becomes `../x`, with a checksum that matches again
        escaping.at(0UZ) = escaping.at(1UZ) = std::byte{'.'};
        std::size_t sum                     = 0UZ;
        for (std::size_t index = 0UZ; index < 512UZ; ++index) {
            sum += index >= 148UZ && index < 156UZ ? 32UZ : std::to_integer<std::size_t>(escaping[index]);
        }
        std::ranges::copy(std::format("{:06o}", sum) | std::views::transform([](char c) { return static_cast<std::byte>(c); }), escaping.begin() + 148);
        const auto unpacked = unpackTar(escaping);
        expect(!unpacked.has_value());
        expect(!unpacked.has_value() && unpacked.error().contains("leaves the archive")) << "refused for its path, not its checksum";

        auto cutOff = packTar(std::vector<TarEntry>{{.path = "talk.md", .bytes = bytesOf(std::string(600UZ, 'x'))}});
        expect(fatal(cutOff.has_value()));
        expect(!unpackTar(std::span{*cutOff}.first(512UZ + 100UZ)).has_value());
    };

    "an empty archive is two zero blocks, and reads back as no files"_test = [] {
        const auto packed = packTar(std::vector<TarEntry>{});
        expect(fatal(packed.has_value()));
        expect(eq(packed->size(), 1024UZ));
        const auto unpacked = unpackTar(*packed);
        expect(unpacked.has_value() && unpacked->empty());
    };

    "only regular files are kept: a directory and a symbolic link are skipped, a leading ./ dropped"_test = [] {
        const auto entries = unpackTar(fixture("links.tar"));
        expect(fatal(entries.has_value())) << (entries ? std::string{} : entries.error());
        expect(fatal(eq(entries->size(), 1UZ)));
        expect(eq((*entries)[0].path, std::string{"d/f"}));
        expect(eq(textOf((*entries)[0].bytes), std::string{"inside\n"}));
    };

    "a size that is no octal number, or no size at all, is read as the header says"_test = [] {
        auto       archive = *packTar(std::vector<TarEntry>{{.path = "a", .bytes = bytesOf("12345678")}});
        const auto stamp   = [&archive](std::string_view size) { // the size field rewritten, the checksum made to match again
            std::ranges::fill(std::span{archive}.subspan(124UZ, 12UZ), std::byte{0});
            std::ranges::copy(size | std::views::transform([](char c) { return static_cast<std::byte>(c); }), archive.begin() + 124);
            std::size_t sum = 0UZ;
            for (std::size_t index = 0UZ; index < 512UZ; ++index) {
                sum += index >= 148UZ && index < 156UZ ? 32UZ : std::to_integer<std::size_t>(archive[index]);
            }
            std::ranges::copy(std::format("{:06o}", sum) | std::views::transform([](char c) { return static_cast<std::byte>(c); }), archive.begin() + 148);
        };
        stamp("0000000001x");
        const auto damaged = unpackTar(archive);
        expect(!damaged.has_value() && damaged.error().contains("no octal number")) << "a size of 0000000001x";
        archive = *packTar(std::vector<TarEntry>{{.path = "a", .bytes = {}}}); // no data that could be taken for a header
        stamp("");
        const auto empty = unpackTar(archive);
        expect(fatal(empty.has_value())) << "an empty size field is zero";
        expect(eq(empty->front().bytes.size(), 0UZ));
    };

    "a pax record that does not parse leaves the header's own name"_test = [] {
        const std::string records = "garbage without a length\n";
        auto              pax     = *packTar(std::vector<TarEntry>{{.path = "PaxHeaders/a", .bytes = bytesOf(records)}, {.path = "a", .bytes = bytesOf("x")}});
        pax[156]                  = std::byte{'x'}; // the first entry becomes a pax extended header
        std::size_t sum           = 0UZ;
        for (std::size_t index = 0UZ; index < 512UZ; ++index) {
            sum += index >= 148UZ && index < 156UZ ? 32UZ : std::to_integer<std::size_t>(pax[index]);
        }
        std::ranges::copy(std::format("{:06o}", sum) | std::views::transform([](char c) { return static_cast<std::byte>(c); }), pax.begin() + 148);
        const auto entries = unpackTar(pax);
        expect(fatal(entries.has_value()));
        expect(fatal(eq(entries->size(), 1UZ)));
        expect(eq((*entries)[0].path, std::string{"a"}));
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
