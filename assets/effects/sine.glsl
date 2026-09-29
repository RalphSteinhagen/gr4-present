// @name    sine
// @uses    transition

// a wipe whose edge is a travelling sine wave

vec2 uv;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

vec4 sine() {
    float edge = mix(-0.08, 1.08, progress) + 0.05 * sin(uv.y * 14.0 + progress * 9.0);
    return mix(young(uv), old(uv), smoothstep(edge - 0.01, edge + 0.01, uv.x));
}

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    progress = iProgress;
    colour = sine();
}
