#include "TextRuns.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gr::present {

namespace {

[[nodiscard]] int strengthOf(std::string_view separator) noexcept {
    if (separator == "\n") {
        return 3;
    }
    if (separator == "\t") {
        return 2;
    }
    return separator == " " ? 1 : 0;
}

[[nodiscard]] bool holds(const Rectangle& area, float x, float y) noexcept { return x >= area.x && x <= area.x + area.width && y >= area.y && y <= area.y + area.height; }

void trimTrailingSpaces(std::string& text) {
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
}

} // namespace

void TextRuns::add(std::string_view text, const Rectangle& area) {
    if (text.empty()) {
        return;
    }
    runs.push_back(TextRun{.text = std::string{text}, .area = area, .separator = runs.empty() ? std::string{} : pending});
    pending.clear();
}

void TextRuns::breakWith(std::string_view separator) {
    if (strengthOf(separator) > strengthOf(pending)) {
        pending = std::string{separator};
    }
}

void TextRuns::clear() {
    runs.clear();
    pending.clear();
}

std::optional<std::size_t> runAt(std::span<const TextRun> runs, float x, float y) {
    const auto found = std::ranges::find_if(runs, [x, y](const TextRun& run) { return holds(run.area, x, y); });
    return found == runs.end() ? std::nullopt : std::optional{static_cast<std::size_t>(found - runs.begin())};
}

std::optional<std::size_t> runNearest(std::span<const TextRun> runs, float x, float y) {
    std::optional<std::size_t> nearest;
    float                      best = std::numeric_limits<float>::max();
    for (std::size_t index = 0UZ; index < runs.size(); ++index) {
        const Rectangle& area     = runs[index].area;
        const float      dx       = std::max({area.x - x, 0.0f, x - (area.x + area.width)});
        const float      dy       = std::max({area.y - y, 0.0f, y - (area.y + area.height)});
        const float      distance = dx * dx + dy * dy;
        if (distance < best) {
            best    = distance;
            nearest = index;
        }
    }
    return nearest;
}

std::string copiedText(std::span<const TextRun> runs, std::size_t from, std::size_t to) {
    if (runs.empty()) {
        return {};
    }
    const std::size_t first = std::min({from, to, runs.size() - 1UZ});
    const std::size_t last  = std::min(std::max(from, to), runs.size() - 1UZ);
    std::string       text;
    for (std::size_t index = first; index <= last; ++index) {
        const std::string& separator = runs[index].separator;
        if (index > first && (separator == "\n" || separator == "\t")) {
            trimTrailingSpaces(text);
            text += separator;
        } else if (index > first && separator == " " && !text.ends_with(' ')) {
            text += ' ';
        }
        text += runs[index].text;
    }
    trimTrailingSpaces(text);
    return text;
}

} // namespace gr::present
