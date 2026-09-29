#ifndef GR4_PRESENT_TEXT_RUNS_HPP
#define GR4_PRESENT_TEXT_RUNS_HPP

#include <gr4-present/RegionGeometry.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// a piece of drawn text a reader can select
struct TextRun {
    std::string text;
    Rectangle   area;
    std::string separator; // "", " ", "\t" or "\n": what precedes it when copied
};

/// a label a drawing carries as text, its box in fractions of the drawing
struct DrawnLabel {
    std::string text;
    Rectangle   area;
};

/// the text a view drew in one frame, in reading order; the strongest `breakWith` since the last run is the next
/// separator
struct TextRuns {
    /// the labels of a drawing with the given elements hidden
    using LabelLookup = std::function<std::span<const DrawnLabel>(std::string_view, std::span<const std::string>)>;

    std::vector<TextRun> runs;
    std::string          pending;
    LabelLookup          labelsOf; // survives `clear`

    void add(std::string_view text, const Rectangle& area);
    void breakWith(std::string_view separator);
    void clear();
};

[[nodiscard]] std::optional<std::size_t> runAt(std::span<const TextRun> runs, float x, float y);

[[nodiscard]] std::optional<std::size_t> runNearest(std::span<const TextRun> runs, float x, float y);

/// the text from one run to another in either order: separators between, none before the first, no trailing space
[[nodiscard]] std::string copiedText(std::span<const TextRun> runs, std::size_t from, std::size_t to);

} // namespace gr::present

#endif // GR4_PRESENT_TEXT_RUNS_HPP
