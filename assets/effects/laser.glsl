// @name    laser
// @uses    pointer overlay
// @author  Ralph J. Steinhagen
// @licence GPL-3.0-or-later
// @param   tint    colour #ff2020
// @param   radius  float  8.0  1.0 40.0
// @param   persist float  0.9  0.0 0.99

// A laser-pointer dot leaving a fading trail. Buffer A holds the trail in red, faded by `persist` each frame, with the
// dot drawn along the segment the pointer moved since the frame before; pixel (0,0) keeps that position in blue and
// alpha. The image shows the trail in `tint`, as opaque as it is bright, so the slide shows through everywhere else.

//--- pass: BufferA
// @channel0 BufferA

float segmentDistance(vec2 p, vec2 a, vec2 b) {
    vec2  ab = b - a;
    float t  = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    return length(p - a - t * ab);
}

void mainImage(out vec4 colour, in vec2 fragCoord) {
    vec2  last  = iFrame == 0 ? iMouse.xy : texelFetch(iChannel0, ivec2(0), 0).zw;
    float spot  = smoothstep(radius, 0.6 * radius, segmentDistance(fragCoord, last, iMouse.xy));
    float trail = iFrame == 0 ? spot : max(texelFetch(iChannel0, ivec2(fragCoord), 0).x * persist, spot);
    colour      = vec4(trail, 0.0, iMouse.xy);
}

//--- pass: Image
// @channel0 BufferA

void mainImage(out vec4 colour, in vec2 fragCoord) {
    float trail = texelFetch(iChannel0, ivec2(fragCoord), 0).x;
    colour      = vec4(tint.rgb, trail * tint.a);
}
