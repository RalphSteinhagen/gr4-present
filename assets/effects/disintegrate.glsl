// @name    disintegrate
// @uses    transition

// the leaving slide comes apart in grains that drift up and away, left to right

vec2 uv;
float aspect;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
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

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    aspect = iResolution.x / iResolution.y;
    progress = iProgress;
    colour = disintegrate();
}
