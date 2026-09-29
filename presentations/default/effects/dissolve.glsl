// @name    dissolve
// @uses    reveal transition
// @duration 1.5
// @author  Ralph J. Steinhagen
// @licence GPL-3.0-or-later
// @param   grain float  24.0 2.0 200.0
// @param   edge  float  0.08 0.0 0.5

// What arrives burns in through value noise: each cell appears once the progress passes its noise value, and the
// border of what has just appeared glows in the theme's accent colour for `edge` of the way.

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float noise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f    = fract(p);
    vec2 u    = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(cell), hash(cell + vec2(1.0, 0.0)), u.x), mix(hash(cell + vec2(0.0, 1.0)), hash(cell + vec2(1.0, 1.0)), u.x), u.y);
}

void mainImage(out vec4 colour, in vec2 fragCoord) {
    vec2  uv        = fragCoord / iResolution.xy;
    vec2  p         = fragCoord / iResolution.y * grain;
    float threshold = 0.5 * noise(p) + 0.3 * noise(2.1 * p) + 0.2 * noise(4.3 * p);
    float shown     = iProgress * (1.0 + edge);
    vec4  before    = texture(iSlideFrom, uv);
    vec4  after     = texture(iSlideTo, uv);
    float arrived   = step(threshold, shown - edge);
    float glowing   = step(threshold, shown) * (1.0 - arrived);
    colour          = mix(mix(before, after, arrived), iThemeAccent, glowing);
}
