#ifndef GR4_PRESENT_TEST_FILE_CONTENTS_HPP
#define GR4_PRESENT_TEST_FILE_CONTENTS_HPP

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

/// a whole file as text or bytes (`std::vector<std::uint8_t>`), empty when it cannot be read. Read by its size: GCC 15
/// at -O2 reports a false null dereference inside libstdc++'s streambuf when a file is read through istreambuf_iterator,
/// an error under the project's -Werror (GCC15-Release)
template<typename TContents = std::string>
[[nodiscard]] TContents fileContents(const std::filesystem::path& path) {
    std::error_code   error;
    const std::size_t size = std::filesystem::file_size(path, error);
    std::ifstream     file{path, std::ios::binary};
    if (error || !file) {
        return {};
    }
    TContents contents(size, typename TContents::value_type{});
    file.read(reinterpret_cast<char*>(contents.data()), static_cast<std::streamsize>(size));
    contents.resize(static_cast<std::size_t>(file.gcount()));
    return contents;
}

#endif // GR4_PRESENT_TEST_FILE_CONTENTS_HPP
