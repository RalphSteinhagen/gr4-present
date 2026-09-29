// @name    curl
// @uses    transition

// a page turned from its lower right corner: a fold moves across, the turned part shows its back, shaded

vec2 uv;
float aspect;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

bool inside(vec2 at) { return at.x >= 0.0 && at.x <= 1.0 && at.y >= 0.0 && at.y <= 1.0; }
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

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    aspect = iResolution.x / iResolution.y;
    progress = iProgress;
    colour = curl();
}
