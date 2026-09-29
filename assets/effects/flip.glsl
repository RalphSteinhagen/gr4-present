// @name    flip
// @uses    transition

// a card flipped about its vertical axis: the leaving slide on its face, the arriving one on its back

vec2 uv;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

bool inside(vec2 at) { return at.x >= 0.0 && at.x <= 1.0 && at.y >= 0.0 && at.y <= 1.0; }
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

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    progress = iProgress;
    colour = flip();
}
