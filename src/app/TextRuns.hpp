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

/// a piece of drawn text a reader can select: what it says, where it is, and what separates it from the one before
struct TextRun {
    std::string text;
    Rectangle   area;
    std::string separator; // "", " ", "\t" or "\n": what goes before it when it is copied after the run before it
};

/// a label a drawing carries as text: what it says and the box it covers, in fractions of the drawing
struct DrawnLabel {
    std::string text;
    Rectangle   area;
};

/**
 * The text a view drew in one frame, in the order it was drawn, which is reading order.
 *
 * The layout knows where a line wrapped, a cell ended or a block ended, so it says so with `breakWith` before the
 * next run; the strongest break asked for since the last run is the one that run carries.
 */
struct TextRuns {
    /// the labels of a drawing with the given elements hidden; a drawing's text is selectable as a slide's is
    using LabelLookup = std::function<std::span<const DrawnLabel>(std::string_view, std::span<const std::string>)>;

    std::vector<TextRun> runs;
    std::string          pending;
    LabelLookup          labelsOf; // kept across frames: `clear` empties the runs, not where labels come from

    void add(std::string_view text, const Rectangle& area);
    void breakWith(std::string_view separator);
    void clear();
};

/// the run whose rectangle holds the point, if any
[[nodiscard]] std::optional<std::size_t> runAt(std::span<const TextRun> runs, float x, float y);

/// the run nearest the point, for a drag that has left the text; none when there are no runs
[[nodiscard]] std::optional<std::size_t> runNearest(std::span<const TextRun> runs, float x, float y);

/// the text from one run to another, either way round, as it reads: separators between, none before the first,
/// and no space left at the end of a line
[[nodiscard]] std::string copiedText(std::span<const TextRun> runs, std::size_t from, std::size_t to);

} // namespace gr::present

#endif // GR4_PRESENT_TEXT_RUNS_HPP
