// @name    ripple
// @uses    transition

// a ripple spreading from where the slide was left, the next slide inside its front

vec2 uv;
vec2 focus;
float aspect;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

vec4 ripple() {
    vec2 d = (uv - focus) * vec2(aspect, 1.0);
    float distance = length(d);
    float front = progress * 1.6 * max(aspect, 1.0);
    float wave = sin((distance - front) * 45.0) * 0.012 * (1.0 - progress) * exp(-abs(distance - front) * 6.0);
    vec2 at = uv + normalize(d + 1e-5) / vec2(aspect, 1.0) * wave;
    return mix(young(at), old(at), smoothstep(front - 0.04, front + 0.04, distance));
}

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    focus = iFocus;
    aspect = iResolution.x / iResolution.y;
    progress = iProgress;
    colour = ripple();
}
