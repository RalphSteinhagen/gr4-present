// @name    waterfall
// @uses    transition

// a waterfall display writing the next slide line by line from the top, the newest lines in its colour map

vec2 uv;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

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

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    progress = iProgress;
    colour = waterfall();
}
