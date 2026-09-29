// @name    cube
// @uses    transition

// a cube turning a quarter, the leaving slide on its front face and the arriving one on its side

vec2 uv;
float progress;
vec4 old(vec2 at) { return texture(iSlideFrom, at); }
vec4 young(vec2 at) { return texture(iSlideTo, at); }

bool inside(vec2 at) { return at.x >= 0.0 && at.x <= 1.0 && at.y >= 0.0 && at.y <= 1.0; }
vec4 cube() {
    float angle = progress * 1.5707963;
    float seam = cos(angle) * cos(angle) ;
    float depth = 0.25 * sin(2.0 * angle);
    if (uv.x < seam) {
        float u = uv.x / max(seam, 1e-4);
        float squeeze = 1.0 - depth * u;
        vec2 at = vec2(u, (uv.y - 0.5) / squeeze + 0.5);
        return inside(at) ? vec4(old(at).rgb * (1.0 - 0.4 * progress), 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
    }
    float u = (uv.x - seam) / max(1.0 - seam, 1e-4);
    float squeeze = 1.0 - depth * (1.0 - u);
    vec2 at = vec2(u, (uv.y - 0.5) / squeeze + 0.5);
    return inside(at) ? vec4(young(at).rgb * (0.6 + 0.4 * progress), 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}

void mainImage(out vec4 colour, in vec2 fragCoord) {
    uv = fragCoord / iResolution.xy;
    progress = iProgress;
    colour = cube();
}
