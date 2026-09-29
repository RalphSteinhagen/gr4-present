// @name    scanlines
// @uses    overlay
// @author  Ralph J. Steinhagen
// @licence GPL-3.0-or-later
// @param   lines     float  180.0 20.0 1000.0
// @param   strength  float  0.35  0.0  1.0
// @param   curvature float  0.12  0.0  0.5

// What is under it, shown as on an old monitor: the picture bulges by `curvature` towards the edges, with dark scan
// lines `lines` to the height, colour fringes and a slow bright band rolling down; `strength` 0 leaves it as it is.

void mainImage(out vec4 colour, in vec2 fragCoord) {
    vec2  uv      = fragCoord / iResolution.xy;
    vec2  centred = uv - 0.5;
    vec2  bent    = 0.5 + centred * (1.0 + curvature * dot(centred, centred) * 4.0) / (1.0 + curvature);
    float inside  = step(0.0, bent.x) * step(bent.x, 1.0) * step(0.0, bent.y) * step(bent.y, 1.0);
    vec2  fringe  = vec2(1.5 / iResolution.x, 0.0);
    vec3  shown   = vec3(texture(iContent, bent + fringe).r, texture(iContent, bent).g, texture(iContent, bent - fringe).b) * inside;
    float scan    = 0.5 + 0.5 * cos(6.2832 * bent.y * lines);
    float band    = smoothstep(0.92, 1.0, fract(bent.y + 0.15 * iTime));
    vec3  monitor = shown * mix(0.55, 1.0, scan) * (1.0 + 0.4 * band);
    colour        = vec4(mix(texture(iContent, uv).rgb, monitor, strength), 1.0);
}
