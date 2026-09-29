// @name    hexagons
// @uses    reveal transition
// @duration 1.5
// @source  supplied by the author (2026-10-10), adapted; origin of the snippet not recorded
// @param   cells  float  10.0 2.0 60.0
// @param   spread float  1.5  0.0 4.0
// @param   rim    float  0.05 0.0 0.3

// What arrives opens in hexagonal cells, each from its centre, in an order set by fractal value noise over the cells;
// the rim of an opening cell is drawn in the theme's accent colour. `cells` hexagons span the height of the slide,
// `spread` staggers them (0: all at once), `rim` is the rim's width as a share of a cell.
// Adapted: the cells open with the progress instead of pulsing with time, and the colours are the two slides'.

float random(vec2 st) { return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453123); }

float noise(vec2 st) {
    vec2  i = floor(st);
    vec2  f = fract(st);
    float a = random(i);
    float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0));
    float d = random(i + vec2(1.0, 1.0));
    vec2  u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 st) {
    float value     = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 3; ++octave) {
        value += amplitude * noise(st);
        st *= 2.0;
        amplitude *= 0.5;
    }
    return value / 0.875; // the three octaves' amplitudes sum to 0.875: 0..1
}

// distance from a hexagon's centre in units where its edge is at 0.5
float hexDistance(vec2 p) {
    p = abs(p);
    return max(dot(p, normalize(vec2(1.0, 1.73))), p.x);
}

// the vector from the nearest hexagon's centre, and that centre
vec4 hexCoordinates(vec2 at) {
    vec2 r = vec2(1.0, 1.732);
    vec2 h = r / 2.0;
    vec2 a = mod(at, r) - h;
    vec2 b = mod(at - h, r) - h;
    vec2 g = length(a) < length(b) ? a : b;
    return vec4(g, at - g);
}

void mainImage(out vec4 colour, in vec2 fragCoord) {
    vec2  uv      = fragCoord / iResolution.xy;
    vec4  hex     = hexCoordinates(fragCoord / iResolution.y * cells);
    float order   = fbm(hex.zw * 0.35);
    float opening = clamp(iProgress * (1.0 + spread) - order * spread, 0.0, 1.0);
    float radius  = 0.5 * opening * (1.0 + rim * 2.0); // a little past the edge, so an open cell leaves no seam
    float inside  = hexDistance(hex.xy);
    float arrived = inside < radius ? 1.0 : 0.0;
    float onRim   = step(radius - rim, inside) * arrived * step(0.0001, opening) * step(opening, 0.9999);
    vec4  before  = texture(iSlideFrom, uv);
    vec4  after   = texture(iSlideTo, uv);
    colour        = mix(mix(before, after, arrived), iThemeAccent, onRim);
}
