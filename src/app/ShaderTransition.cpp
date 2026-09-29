#ifndef __EMSCRIPTEN__
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES // framebuffers and shaders are GL 3 entry points, declared only with the extension prototypes
#endif
#endif

#include "ShaderTransition.hpp"

#include <gnuradio-4.0/Logger.hpp>

#include <imgui_impl_opengl3.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <algorithm>
#include <string>

namespace gr::present {

namespace {

#ifdef __EMSCRIPTEN__
constexpr std::string_view kVersion = "#version 300 es\nprecision highp float;\n";
#else
constexpr std::string_view kVersion = "#version 150\n";
#endif

// one triangle that covers the screen, from the vertex index alone, so no buffer is needed
constexpr std::string_view kVertex = R"(
out vec2 uv;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv          = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Every effect mixes the two slides at `progress`, 0 the leaving one and 1 the arriving one. The noise is value
// noise on a hashed lattice, summed over octaves, which is all a burning edge or a dissolve needs.
constexpr std::string_view kFragment = R"(
in vec2 uv;
out vec4 colour;
uniform sampler2D leaving;
uniform sampler2D arriving;
uniform float progress;
uniform int effect;
uniform vec2 focus;
uniform float aspect;
uniform float time;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) {
    float sum = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 5; ++octave) {
        sum += amplitude * noise(p);
        p *= 2.03;
        amplitude *= 0.5;
    }
    return sum;
}
vec4 old(vec2 at) { return texture(leaving, at); }
vec4 young(vec2 at) { return texture(arriving, at); }
bool inside(vec2 at) { return at.x >= 0.0 && at.x <= 1.0 && at.y >= 0.0 && at.y <= 1.0; }

// The leaving slide burns away: organic, curling holes open, swept up from the bottom, behind a rim that cools from
// white-hot to ember. Adapted from "Dissolve with glowing burn edge" by DimensionAlmios,
// https://godotshaders.com/shader/dissolve-with-glowing-burn-edge/ -- MIT licence, as its page states: "The shader
// code and all code snippets in this post are under MIT license and can be used freely." Ported from Godot's shading
// language: its `dissolve` is `progress`, its texture the leaving slide, and what has burnt shows the arriving one.
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

// fire2: a burning line rises over the screen with flames, smoke and sparks licking up from it; below it the arriving
// slide, above it the leaving one. Built from three pieces, MIT each, as their authors state:
//  - simplex noise by Ian McEwan, Ashima Arts, https://github.com/ashima/webgl-noise -- Copyright (C) 2011 Ashima
//    Arts, distributed under the MIT License;
//  - the hash from Shadertoy 4djSRW by Dave Hoskins;
//  - the flames, smoke and sparks of "301's Fire Shader - Remix 2", Shadertoy MtcGD7, MIT as its author states.
// Adapted: the flames stand on a front that moves with `progress` instead of the bottom of the screen, burn across
// the whole width, and are added over the two slides.
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }
float snoise(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);
    vec3 i  = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 g  = step(x0.yzx, x0.xyz);
    vec3 l  = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;
    i = mod289(i);
    vec4 p = permute(permute(permute(i.z + vec4(0.0, i1.z, i2.z, 1.0)) + i.y + vec4(0.0, i1.y, i2.y, 1.0)) + i.x + vec4(0.0, i1.x, i2.x, 1.0));
    float n_ = 0.142857142857;
    vec3  ns = n_ * D.wyz - D.xzx;
    vec4 j  = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x  = x_ * ns.x + ns.yyyy;
    vec4 y  = y_ * ns.x + ns.yyyy;
    vec4 h  = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    vec4 norm = inversesqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}
float prng(vec2 seed) {
    seed = fract(seed * vec2(5.3983, 5.4427));
    seed += dot(seed.yx, seed.xy + vec2(21.5351, 14.3137));
    return fract(seed.x * seed.y * 95.4337);
}
float noiseStack(vec3 pos, int octaves, float falloff) {
    float value = snoise(pos);
    float off = 1.0;
    for (int octave = 1; octave < 4; ++octave) {
        if (octave < octaves) {
            pos *= 2.0;
            off *= falloff;
            value = (1.0 - off) * value + off * snoise(pos);
        }
    }
    return (1.0 + value) / 2.0;
}
vec2 noiseStackUV(vec3 pos, int octaves, float falloff) {
    return vec2(noiseStack(pos, octaves, falloff), noiseStack(pos + vec3(3984.293, 423.21, 5235.19), octaves, falloff));
}
vec4 fire2() {
    const float PI = 3.1415926535897932;
    vec2 resolution = vec2(aspect * 720.0, 720.0); // the shader's pixel units, at a 720-line height
    // where the flames stand: rising with progress, ahead at the left and right edges, and ragged along the width
    float edges = 0.35 * pow(abs(2.0 * uv.x - 1.0), 3.0);
    float ragged = 0.08 * snoise(vec3(uv.x * 3.5, 0.35 * time, 7.0)) + 0.04 * snoise(vec3(uv.x * 9.0, 0.6 * time, 3.0));
    float front = (mix(-0.35, 1.2, progress) + edges + ragged) * resolution.y;
    vec2 fragCoord = vec2(uv.x * resolution.x, uv.y * resolution.y - front);
    vec4 base = fragCoord.y < 0.0 ? young(uv) : old(uv);
    float xpart = uv.x;
    float ypart = fragCoord.y / resolution.y;
    float clipHeight = 330.0; // taller flames than the original's 210 px
    float ypartClip = max(fragCoord.y, 0.0) / clipHeight;
    float ypartClippedFalloff = clamp(2.0 - ypartClip, 0.0, 1.0);
    float ypartClipped = min(ypartClip, 1.0);
    float ypartClippedn = 1.0 - ypartClipped;
    float xfuel = 1.0 - 0.3 * abs(2.0 * xpart - 1.0); // the whole width burns
    float realTime = 0.5 * time;
    vec2 coordScaled = 0.007 * fragCoord; // and wider tongues
    vec3 position = vec3(coordScaled, 0.0) + vec3(1223.0, 6434.0, 8425.0);
    vec3 flow = vec3(4.1 * (0.5 - xpart) * pow(ypartClippedn, 4.0), -2.0 * xfuel * pow(ypartClippedn, 64.0), 0.0);
    vec3 timing = realTime * vec3(0.0, -1.7, 1.1) + flow;
    vec3 displacePos = vec3(1.0, 0.5, 1.0) * 2.4 * position + realTime * vec3(0.01, -0.7, 1.3);
    vec3 displace3 = vec3(noiseStackUV(displacePos, 2, 0.4), 0.0);
    vec3 noiseCoord = vec3(2.0, 1.0, 1.0) * position + timing + 0.4 * displace3;
    float flameNoise = noiseStack(noiseCoord, 3, 0.4);
    float flames = pow(ypartClipped, 0.3 * xfuel) * pow(flameNoise, 0.3 * xfuel);
    float f = ypartClippedFalloff * pow(1.0 - flames * flames * flames, 8.0);
    float fff = f * f * f;
    vec3 flame = 1.5 * vec3(f, fff, fff * fff);
    float smokeNoise = 0.5 + snoise(0.4 * position + timing * vec3(1.0, 1.0, 0.2)) / 2.0;
    vec3 smoke = vec3(0.3 * pow(xfuel, 3.0) * pow(clamp(ypart, 0.0, 1.0), 2.0) * (smokeNoise + 0.4 * (1.0 - flameNoise)));
    float sparkGridSize = 30.0;
    vec2 sparkCoord = fragCoord - vec2(0.0, 190.0 * realTime);
    sparkCoord -= 30.0 * noiseStackUV(0.01 * vec3(sparkCoord, 30.0 * time), 1, 0.4);
    sparkCoord += 100.0 * flow.xy;
    if (mod(sparkCoord.y / sparkGridSize, 2.0) < 1.0) {
        sparkCoord.x += 0.5 * sparkGridSize;
    }
    vec2 sparkGridIndex = floor(sparkCoord / sparkGridSize);
    float sparkRandom = prng(sparkGridIndex);
    float sparkLife = min(10.0 * (1.0 - min((sparkGridIndex.y + (190.0 * realTime / sparkGridSize)) / (24.0 - 20.0 * sparkRandom), 1.0)), 1.0);
    vec3 sparks = vec3(0.0);
    if (sparkLife > 0.0 && fragCoord.y > 0.0) {
        float sparkSize = xfuel * xfuel * sparkRandom * 0.08;
        float sparkRadians = 999.0 * sparkRandom * 2.0 * PI + 2.0 * time;
        vec2 sparkOffset = (0.5 - sparkSize) * sparkGridSize * vec2(sin(sparkRadians), cos(sparkRadians));
        vec2 sparkModulus = mod(sparkCoord + sparkOffset, sparkGridSize) - 0.5 * vec2(sparkGridSize);
        sparks = sparkLife * max(0.0, 1.0 - length(sparkModulus) / (sparkSize * sparkGridSize)) * vec3(1.0, 0.3, 0.0);
    }
    // the flames only stand on the front: below it lies what has burnt through, the arriving slide
    vec3 heat = fragCoord.y < 0.0 ? vec3(1.0, 0.45, 0.1) * 0.8 * f * smoothstep(-120.0, 0.0, fragCoord.y) : max(flame, sparks) + smoke * step(0.0, fragCoord.y);
    return vec4(base.rgb + heat, 1.0);
}

// a cathode-ray tube switched off and on: the picture collapses to a line, the line to a dot, and the next opens
vec4 crt() {
    float phase = progress < 0.5 ? progress * 2.0 : (1.0 - progress) * 2.0;
    float tall = 1.0 - smoothstep(0.0, 0.55, phase) * 0.995;
    float wide = 1.0 - smoothstep(0.55, 1.0, phase) * 0.995;
    vec2 at = (uv - 0.5) / vec2(wide, tall) + 0.5;
    vec4 shown = progress < 0.5 ? old(at) : young(at);
    float boost = 1.0 + 2.5 * smoothstep(0.3, 1.0, phase);
    vec4 picture = inside(at) ? vec4(shown.rgb * boost, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
    float line = exp(-pow((uv.y - 0.5) / max(0.004, 0.5 * tall), 2.0) * 4.0) * smoothstep(0.4, 1.0, phase);
    return picture + vec4(vec3(0.7, 0.9, 1.0) * line, 0.0);
}

// a receiver losing lock: bands of the picture tear sideways and its colours split, and settle on the next slide
vec4 glitch() {
    float strength = sin(3.14159265 * progress);
    float band = floor(uv.y * 28.0);
    float tick = floor(progress * 14.0);
    float tear = (hash(vec2(band, tick)) - 0.5) * 0.25 * strength * step(0.55, hash(vec2(band, tick + 7.0)));
    bool next = hash(vec2(band, tick + 3.0)) < progress;
    vec2 at = uv + vec2(tear, 0.0);
    float split = 0.012 * strength;
    vec4 r = next ? young(at + vec2(split, 0.0)) : old(at + vec2(split, 0.0));
    vec4 g = next ? young(at) : old(at);
    vec4 b = next ? young(at - vec2(split, 0.0)) : old(at - vec2(split, 0.0));
    return vec4(r.r, g.g, b.b, 1.0);
}

// a waterfall display writing the next slide line by line from the top, the newest lines in its colour map
vec3 colourMap(float level) {
    return clamp(vec3(1.5 * level - 0.25, sin(3.14159265 * level), 1.0 - 1.4 * level), 0.0, 1.0);
}
vec4 waterfall() {
    float written = progress * 1.15;
    float age = written - (1.0 - uv.y);
    if (age < 0.0) {
        return old(uv);
    }
    vec4 fresh = young(uv);
    float level = dot(fresh.rgb, vec3(0.299, 0.587, 0.114));
    return vec4(mix(colourMap(level), fresh.rgb, smoothstep(0.0, 0.15, age)), 1.0);
}

// a wipe whose edge is a travelling sine wave
vec4 sine() {
    float edge = mix(-0.08, 1.08, progress) + 0.05 * sin(uv.y * 14.0 + progress * 9.0);
    return mix(young(uv), old(uv), smoothstep(edge - 0.01, edge + 0.01, uv.x));
}

// the leaving slide comes apart in grains that drift up and away, left to right
vec4 disintegrate() {
    vec2 grains = vec2(aspect, 1.0) * 140.0;
    vec2 cell = floor(uv * grains);
    float start = uv.x * 0.55 + hash(cell) * 0.3;
    float gone = clamp((progress - start) / 0.25, 0.0, 1.0);
    vec2 drift = vec2(0.03 + 0.04 * hash(cell + 1.0), -0.08 - 0.06 * hash(cell + 2.0)) * gone;
    vec2 at = uv - drift;
    vec4 grain = old(at);
    float present = (1.0 - gone) * step(gone, 0.999);
    return vec4(mix(young(uv).rgb, grain.rgb, present), 1.0);
}

// a ripple spreading from where the slide was left, the next slide inside its front
vec4 ripple() {
    vec2 d = (uv - focus) * vec2(aspect, 1.0);
    float distance = length(d);
    float front = progress * 1.6 * max(aspect, 1.0);
    float wave = sin((distance - front) * 45.0) * 0.012 * (1.0 - progress) * exp(-abs(distance - front) * 6.0);
    vec2 at = uv + normalize(d + 1e-5) / vec2(aspect, 1.0) * wave;
    return mix(young(at), old(at), smoothstep(front - 0.04, front + 0.04, distance));
}

// a page turned from its lower right corner: a fold moves across, the turned part shows its back, shaded
vec4 curl() {
    vec2 direction = normalize(vec2(1.0, -0.35));
    vec2 p = uv * vec2(aspect, 1.0);
    float along = dot(p, direction);
    float reach = dot(vec2(aspect, 0.0), direction);
    float fold = mix(reach + 0.05, -0.45, progress);
    if (along < fold) {
        return old(uv);
    }
    vec2 back = p - direction * 2.0 * (along - fold);
    vec2 at = back / vec2(aspect, 1.0);
    if (inside(at)) {
        float shade = 0.75 - 0.35 * clamp((along - fold) * 3.0, 0.0, 1.0);
        return vec4(old(vec2(1.0 - at.x, at.y)).rgb * shade + 0.12, 1.0);
    }
    float shadow = 1.0 - 0.35 * exp(-(along - fold) * 18.0);
    return vec4(young(uv).rgb * shadow, 1.0);
}

// a cube turning a quarter, the leaving slide on its front face and the arriving one on its side
vec4 cube() {
    float angle = progress * 1.5707963;
    float seam = cos(angle) * cos(angle) ;
    float depth = 0.25 * sin(2.0 * angle);
    if (uv.x < seam) {
        float u = uv.x / max(seam, 1e-4);
        float squeeze = 1.0 - depth * u;
        vec2 at = vec2(u, (uv.y - 0.5) / squeeze + 0.5);
        return inside(at) ? vec4(old(at).rgb * (1.0 - 0.4 * progress), 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
    }
    float u = (uv.x - seam) / max(1.0 - seam, 1e-4);
    float squeeze = 1.0 - depth * (1.0 - u);
    vec2 at = vec2(u, (uv.y - 0.5) / squeeze + 0.5);
    return inside(at) ? vec4(young(at).rgb * (0.6 + 0.4 * progress), 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}

// a card flipped about its vertical axis: the leaving slide on its face, the arriving one on its back
vec4 flip() {
    float angle = progress * 3.14159265;
    float wide = abs(cos(angle));
    bool front = progress < 0.5;
    float lean = 0.15 * sin(angle);
    float u = (uv.x - 0.5) / max(wide, 1e-4) + 0.5;
    float squeeze = 1.0 - lean * (front ? u : 1.0 - u);
    vec2 at = vec2(u, (uv.y - 0.5) / squeeze + 0.5);
    if (!inside(at)) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    return vec4((front ? old(at) : young(at)).rgb * (0.7 + 0.3 * wide), 1.0);
}

void main() {
    if (effect == 0) colour = fire();
    else if (effect == 1) colour = crt();
    else if (effect == 2) colour = glitch();
    else if (effect == 3) colour = waterfall();
    else if (effect == 4) colour = sine();
    else if (effect == 5) colour = disintegrate();
    else if (effect == 6) colour = ripple();
    else if (effect == 7) colour = curl();
    else if (effect == 8) colour = cube();
    else if (effect == 9) colour = flip();
    else colour = fire2();
}
)";

[[nodiscard]] unsigned compiled(GLenum kind, std::string_view body) {
    const std::string source = std::string{kVersion} + std::string{body};
    const char*       text   = source.c_str();
    const GLuint      shader = glCreateShader(kind);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 2048> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        gr::log::warning("a transition's shader did not compile: {}", log.data());
        glDeleteShader(shader);
        return 0U;
    }
    return shader;
}

} // namespace

bool ShaderTransition::ensureTargets(int width, int height) {
    if (width == _width && height == _height && _targets[0].framebuffer != 0U) {
        return true;
    }
    _width  = width;
    _height = height;
    for (Target& target : _targets) {
        if (target.framebuffer == 0U) {
            glGenFramebuffers(1, &target.framebuffer);
            glGenTextures(1, &target.texture);
        }
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            gr::log::warning("a transition's framebuffer is incomplete; it fades instead");
            _failed = true;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return !_failed;
}

bool ShaderTransition::ensureProgram() {
    if (_program != 0U || _failed) {
        return !_failed;
    }
    const unsigned vertex   = compiled(GL_VERTEX_SHADER, kVertex);
    const unsigned fragment = compiled(GL_FRAGMENT_SHADER, kFragment);
    if (vertex == 0U || fragment == 0U) {
        _failed = true;
        return false;
    }
    _program = glCreateProgram();
    glAttachShader(_program, vertex);
    glAttachShader(_program, fragment);
    glLinkProgram(_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(_program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        gr::log::warning("a transition's shader did not link; it fades instead");
        _failed = true;
        return false;
    }
    glGenVertexArrays(1, &_array); // a core context draws nothing without one, even with no attributes
    return true;
}

bool ShaderTransition::usable() { return ensureProgram(); }

void ShaderTransition::beginCapture(ImDrawList& list, int slide, ImU32 background) {
    _background = background;
    list.AddCallback(&ShaderTransition::onBegin, &_captures[static_cast<std::size_t>(std::clamp(slide, 0, 1))]);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ShaderTransition::endCapture(ImDrawList& list) {
    list.AddCallback(&ShaderTransition::onEnd, this);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ShaderTransition::composite(ImDrawList& list, ShaderEffect effect, float progress, ImVec2 focus) {
    _effect   = effect;
    _progress = progress;
    _focus    = focus;
    _time     = static_cast<float>(ImGui::GetTime());
    list.AddCallback(&ShaderTransition::onComposite, this);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ShaderTransition::onBegin(const ImDrawList*, const ImDrawCmd* command) {
    const auto&       capture = *static_cast<const Capture*>(command->UserCallbackData);
    ShaderTransition& self    = *capture.owner;
    const ImGuiIO&    io      = ImGui::GetIO();
    const int         width   = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    const int         tall    = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    const std::size_t slide   = capture.slide;
    if (!self.ensureTargets(width, tall)) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, self._targets[slide].framebuffer);
    const ImVec4 clear = ImGui::ColorConvertU32ToFloat4(self._background);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(clear.x, clear.y, clear.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void ShaderTransition::onEnd(const ImDrawList*, const ImDrawCmd*) { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

void ShaderTransition::onComposite(const ImDrawList*, const ImDrawCmd* command) {
    auto& self = *static_cast<ShaderTransition*>(command->UserCallbackData);
    if (!self.ensureProgram() || self._targets[0].framebuffer == 0U) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, self._width, self._height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glUseProgram(self._program);
    glBindVertexArray(self._array);
    for (int unit = 0; unit < 2; ++unit) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D, self._targets[static_cast<std::size_t>(unit)].texture);
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(self._program, "leaving"), 0);
    glUniform1i(glGetUniformLocation(self._program, "arriving"), 1);
    glUniform1f(glGetUniformLocation(self._program, "progress"), std::clamp(self._progress, 0.0f, 1.0f));
    glUniform1i(glGetUniformLocation(self._program, "effect"), static_cast<GLint>(self._effect));
    glUniform2f(glGetUniformLocation(self._program, "focus"), self._focus.x / std::max(io.DisplaySize.x, 1.0f), 1.0f - self._focus.y / std::max(io.DisplaySize.y, 1.0f));
    glUniform1f(glGetUniformLocation(self._program, "aspect"), io.DisplaySize.x / std::max(io.DisplaySize.y, 1.0f));
    glUniform1f(glGetUniformLocation(self._program, "time"), self._time);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

} // namespace gr::present
