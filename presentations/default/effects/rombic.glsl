// @name    rombic
// @uses    transition
// @duration 1.5
// @source  supplied by the author (2026-10-10), adapted; origin of the snippet not recorded
// @param   rows   float  15.0 2.0 60.0
// @param   spread float  1.5  0.0 4.0

// A grid of diamonds sweeps from left to right: in each cell a black diamond grows over the leaving slide until it
// fills the cell, then shrinks again over the arriving one. `rows` cells span the height, square cells across;
// `spread` staggers the columns (0: all at once).
// Adapted: one sweep follows the progress instead of a repeating fade in and out with time, and the slide under the
// diamonds changes at the moment they cover their cell.

void mainImage(out vec4 colour, in vec2 fragCoord) {
    vec2  uv       = fragCoord / iResolution.xy;
    vec2  grid     = vec2(rows * iResolution.x / iResolution.y, rows);
    vec2  cell     = floor(uv * grid);
    vec2  inCell   = fract(uv * grid) * 2.0 - 1.0;          // -1..1 across a cell
    float column   = cell.x / max(grid.x - 1.0, 1.0);       // 0 at the left edge, 1 at the right
    float sweep    = clamp(iProgress * (1.0 + spread) - column * spread, 0.0, 1.0);
    float diamond  = 2.0 * (1.0 - abs(2.0 * sweep - 1.0)); // 0, growing to 2 (the whole cell) half-way, back to 0
    vec4  slide    = sweep < 0.5 ? texture(iSlideFrom, uv) : texture(iSlideTo, uv);
    colour         = abs(inCell.x) + abs(inCell.y) < diamond ? vec4(0.0, 0.0, 0.0, 1.0) : slide;
}
