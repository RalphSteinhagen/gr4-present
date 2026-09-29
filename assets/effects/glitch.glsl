// @name    glitch
// @uses    transition

// a receiver losing lock: bands of the picture tear sideways and its colours split, and settle on the next slide

vec2 uv;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
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

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    progress = iProgress;
    colour = glitch();
}
