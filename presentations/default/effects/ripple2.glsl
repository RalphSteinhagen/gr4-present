// @name    ripple2
// @uses    transition
// @duration 2
// @source  supplied by the author (2026-10-10), adapted; origin of the snippet not recorded
// @param   scale  float  5.0 0.5 20.0
// @param   amount float  1.0 0.0 2.0

// The two slides flow into each other along curl noise: each is smeared vertically by the noise's x component, the
// leaving one more as it goes and the arriving one less as it comes, and the two are blended by the progress.
// Built from: simplex noise by Ian McEwan, Ashima Arts, https://github.com/ashima/webgl-noise -- Copyright (C) 2011
// Ashima Arts, distributed under the MIT License; curl noise after P. Werner, "Intro to curl noise", 2015,
// https://petewerner.blogspot.com/2015/02/intro-to-curl-noise.html.
// Adapted: the blend follows the progress instead of a sine of time, `scale` and `amount` set the noise's size and
// the smear's length.

vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

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
    vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

vec3 curlNoise(vec3 p) {
    const float e  = 0.1;
    float       n1 = snoise(vec3(p.x, p.y + e, p.z));
    float       n2 = snoise(vec3(p.x, p.y - e, p.z));
    float       n3 = snoise(vec3(p.x, p.y, p.z + e));
    float       n4 = snoise(vec3(p.x, p.y, p.z - e));
    float       n5 = snoise(vec3(p.x + e, p.y, p.z));
    float       n6 = snoise(vec3(p.x - e, p.y, p.z));
    vec3        curl = vec3(n2 - n1 - n4 + n3, n4 - n3 - n6 + n5, n6 - n5 - n2 + n1) / (2.0 * e);
    return curl / max(length(curl), 1e-6); // normalised; a flat spot has no direction rather than NaN
}

void mainImage(out vec4 colour, in vec2 fragCoord) {
    vec2  uv      = fragCoord / iResolution.xy;
    float f       = iProgress;
    float smear   = amount * curlNoise(vec3(uv, 1.0) * scale + iTime).x;
    vec4  leaving = texture(iSlideFrom, vec2(uv.x, uv.y + f * smear));
    vec4  coming  = texture(iSlideTo, vec2(uv.x, uv.y + (1.0 - f) * smear));
    colour        = mix(leaving, coming, f);
}
