// @name    crt
// @uses    transition

// a cathode-ray tube switched off and on: the picture collapses to a line, the line to a dot, and the next opens

vec2 uv;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

bool inside(vec2 at) { return at.x >= 0.0 && at.x <= 1.0 && at.y >= 0.0 && at.y <= 1.0; }
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

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    progress = iProgress;
    colour = crt();
}
