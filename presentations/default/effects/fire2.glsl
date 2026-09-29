// @name    fire2
// @uses    transition
// @duration 2
// @author 301
// @source  https://www.shadertoy.com/view/MtcGD7
// @licence MIT

// fire2: a burning line rises over the screen with flames, smoke and sparks licking up from it; below it the arriving
// slide, above it the leaving one. Built from three pieces, MIT each, as their authors state:
//  - simplex noise by Ian McEwan, Ashima Arts, https://github.com/ashima/webgl-noise -- Copyright (C) 2011 Ashima
//    Arts, distributed under the MIT License;
//  - the hash by Dave Hoskins, https://www.shadertoy.com/view/4djSRW;
//  - the flames, smoke and sparks of "301's Fire Shader - Remix 2", https://www.shadertoy.com/view/MtcGD7, MIT as
//    its author states.
// Adapted: the flames stand on a front that moves with `progress` instead of the bottom of the screen, burn across
// the whole width, and are added over the two slides.

vec2 uv;
float aspect;
float progress;
float time;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

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

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    aspect = iResolution.x / iResolution.y;
    progress = iProgress;
    time = iTime;
    colour = fire2();
}
