// @name    fire
// @uses    transition
// @duration 2
// @author DimensionAlmios
// @source https://godotshaders.com/shader/dissolve-with-glowing-burn-edge/
// @licence MIT

// The leaving slide burns away: organic, curling holes open, swept up from the bottom, behind a rim that cools from
// white-hot to ember. Adapted from "Dissolve with glowing burn edge" by DimensionAlmios,
// https://godotshaders.com/shader/dissolve-with-glowing-burn-edge/ -- MIT licence, as its page states: "The shader
// code and all code snippets in this post are under MIT license and can be used freely." Ported from Godot's shading
// language: its `dissolve` is `progress`, its texture the leaving slide, and what has burnt shows the arriving one.

vec2 uv;
float aspect;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

const float kNoiseScale = 22.0;  // size of the burnt patches
const float kWarp       = 0.6;   // how organic and curly the edges are
const float kSweep      = 0.5;   // 0: holes open everywhere at once, 1: the burn sweeps across
const vec2  kSweepFrom  = vec2(0.0, 1.0); // up from the bottom of the screen
const float kEdgeWidth  = 0.10;
const vec3  kEdgeColour = vec3(1.0, 0.5, 0.1);
const vec3  kEdgeHot    = vec3(1.0, 0.95, 0.6);

float burnHash(vec2 p) {
    p = fract(p * 0.3183099 + vec2(0.1, 0.113));
    p *= 17.0;
    return fract(p.x * p.y * (p.x + p.y));
}
float burnNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); // quintic: no grid creases
    return mix(mix(burnHash(i), burnHash(i + vec2(1.0, 0.0)), u.x), mix(burnHash(i + vec2(0.0, 1.0)), burnHash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float burnFbm(vec2 p) {
    mat2 r = mat2(vec2(0.8, 0.6), vec2(-0.6, 0.8)); // rotated octaves hide the square noise grid
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * burnNoise(p);
        p = r * p * 2.03 + 11.7;
        a *= 0.5;
    }
    return v / 0.9375;
}
vec4 fire() {
    vec2 p = uv * vec2(aspect, 1.0) * kNoiseScale * 0.35 / aspect;
    vec2 w = vec2(burnFbm(p * 0.7 + 3.1), burnFbm(p * 0.7 + 8.4)) - 0.5; // domain warp: curly outlines
    float n = burnFbm(p + w * kWarp * 2.5);
    n = 0.5 + 0.5 * tanh((n - 0.5) / 0.11 * 0.85); // spread fbm's values evenly, so the burn runs steadily
    float g = dot(uv - 0.5, kSweepFrom) / (abs(kSweepFrom.x) + abs(kSweepFrom.y)) + 0.5;
    n = mix(n, g * 0.75 + n * 0.25, kSweep);
    float thr = mix(-kEdgeWidth - 0.02, 1.04, progress);
    float aa = min(fwidth(n), 0.02) + 0.001;
    float kept = smoothstep(thr - aa, thr + aa, n); // what of the leaving slide has not burnt yet
    float e = clamp((n - thr) / kEdgeWidth, 0.0, 1.0); // hottest at the burn line, cooling outward
    vec3 ember = mix(kEdgeHot, kEdgeColour, e);
    vec3 paper = mix(old(uv).rgb, ember, (1.0 - e) * kept);
    return vec4(mix(young(uv).rgb, paper, kept), 1.0);
}

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    aspect = iResolution.x / iResolution.y;
    progress = iProgress;
    colour = fire();
}
