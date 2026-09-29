// @name    burn
// @uses    transition
// @duration 2
// @source  supplied by the author (2026-10-10), adapted; origin of the snippet not recorded
// @param   grain float  6.0 1.0 40.0

// The leaving slide burns away like a film frame stuck in a projector gate: a ragged hole opens on the left and
// spreads to the right, its border scorched and the picture around it bulging; through the hole the arriving slide.
// Adapted: the burn front follows the progress instead of a sine of time, and value noise (`grain` cells across the
// height) replaces the noise texture.

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
    float ragged    = 0.6 * noise(p) + 0.4 * noise(2.7 * p);
    float burn      = ((ragged - 0.5) + 2.0 * uv.x) / 3.0; // -1/6 at the left edge to 5/6 at the right
    float front     = mix(-0.35, 1.0, iProgress);          // below every value at 0, above every value at 1
    float kept      = smoothstep(front - 0.1, front, burn);
    float bulge     = smoothstep(front - 0.3, front + 0.05, burn);
    float unscorched = pow(smoothstep(front - 0.2, front + 0.15, burn), 3.0);
    vec3  picture   = texture(iSlideFrom, uv * (0.7 + bulge * bulge * 0.3)).rgb;
    vec3  scorched  = picture * vec3(0.8, 0.4, 0.2);
    vec3  leaving   = mix(scorched, picture, unscorched);
    colour          = vec4(mix(texture(iSlideTo, uv).rgb, leaving, kept), 1.0);
}
