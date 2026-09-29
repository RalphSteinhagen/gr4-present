#include "Morph.hpp"

#include "Transition.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <ranges>
#include <vector>

namespace gr::present {

namespace {

[[nodiscard]] Rectangle boundsOf(std::span<const ImDrawVert> vertices, int from, int to) {
    ImVec2 low{FLT_MAX, FLT_MAX};
    ImVec2 high{-FLT_MAX, -FLT_MAX};
    for (const ImDrawVert& vertex : vertices.subspan(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from))) {
        low  = ImVec2{std::min(low.x, vertex.pos.x), std::min(low.y, vertex.pos.y)};
        high = ImVec2{std::max(high.x, vertex.pos.x), std::max(high.y, vertex.pos.y)};
    }
    return Rectangle{.x = low.x, .y = low.y, .width = std::max(high.x - low.x, 1.0f), .height = std::max(high.y - low.y, 1.0f)};
}

[[nodiscard]] Rectangle between(const Rectangle& there, const Rectangle& here, float eased) { return Rectangle{.x = std::lerp(there.x, here.x, eased), .y = std::lerp(there.y, here.y, eased), .width = std::lerp(there.width, here.width, eased), .height = std::lerp(there.height, here.height, eased)}; }

} // namespace

void morphPositions(std::span<ImDrawVert> vertices, std::span<const DrawnPiece> leaving, std::span<const DrawnPiece> arriving, float eased) {
    struct Move {
        DrawnPiece piece;
        Rectangle  own;
        Rectangle  now;
    };
    // every box measured where it was drawn, before anything moves: a block's box holds its lines
    const auto movesOf = [&](bool lines) {
        const auto                    ofKind = [lines](std::span<const DrawnPiece> pieces) { return pieces | std::views::filter([lines](const DrawnPiece& piece) { return piece.line == lines; }) | std::ranges::to<std::vector<DrawnPiece>>(); };
        const auto                    keysOf = [](const std::vector<DrawnPiece>& pieces) { return pieces | std::views::transform(&DrawnPiece::key) | std::ranges::to<std::vector<std::string>>(); };
        const std::vector<DrawnPiece> before = ofKind(leaving);
        const std::vector<DrawnPiece> after  = ofKind(arriving);
        std::vector<Move>             moves;
        for (const auto& [from, to] : matchedByKey(keysOf(before), keysOf(after))) {
            const Rectangle there = boundsOf(vertices, before[from].vertexFrom, before[from].vertexTo);
            const Rectangle here  = boundsOf(vertices, after[to].vertexFrom, after[to].vertexTo);
            const Rectangle now   = between(there, here, eased);
            moves.push_back({.piece = before[from], .own = there, .now = now});
            moves.push_back({.piece = after[to], .own = here, .now = now});
        }
        return moves;
    };
    const std::vector<Move> lineMoves  = movesOf(true);
    const std::vector<Move> blockMoves = movesOf(false);

    std::vector<bool> movedAlone(vertices.size(), false);
    const auto        apply = [&](const Move& move, bool markMoved) {
        for (int index = move.piece.vertexFrom; index < move.piece.vertexTo; ++index) {
            if (movedAlone[static_cast<std::size_t>(index)]) {
                continue;
            }
            ImVec2& at = vertices[static_cast<std::size_t>(index)].pos;
            at         = ImVec2{move.now.x + (at.x - move.own.x) * move.now.width / move.own.width, move.now.y + (at.y - move.own.y) * move.now.height / move.own.height};
        }
        if (markMoved) {
            std::fill(movedAlone.begin() + move.piece.vertexFrom, movedAlone.begin() + move.piece.vertexTo, true);
        }
    };
    for (const Move& move : lineMoves) {
        apply(move, true);
    }
    for (const Move& move : blockMoves) {
        apply(move, false);
    }
}

} // namespace gr::present
