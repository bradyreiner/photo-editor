#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragmentColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 resolution;
    float lightPhase;
    vec4 parameters;
    vec4 tint;
} ubuf;
const float TAU = 6.28318530718;
// Optical RGB channels add into cyan, magenta, yellow and a white focus.
const vec3 channels[3] = vec3[3](vec3(1.0, 0.17, 0.11),
    vec3(0.12, 0.94, 0.42), vec3(0.18, 0.38, 1.0));
float filament(float d, float size) {
    return 0.64 * exp(-d*d/(size*size))
         + 0.08 * exp(-d*d/(size*size*36.0));
}
void main() {
    vec2 p = uv * 2.0 - 1.0;
    float t = ubuf.lightPhase * TAU;
    float kind = ubuf.parameters.x;
    float variant = ubuf.parameters.y;
    float progress = ubuf.parameters.z;
    vec3 light = vec3(0.0);
    if (kind < 0.5) {
        // Folded wavefronts create bright caustic envelopes where rays converge.
        float x = p.x * 2.5;
        float envelope = pow(max(0.0, 1.0 - p.x*p.x), 1.7);
        for (int channel = 0; channel < 3; ++channel) {
            float dispersion = float(channel - 1);
            for (int ray = 0; ray < 5; ++ray) {
                float r = float(ray) - 2.0;
                float wave = sin(x * 1.5 + t + variant * 0.43);
                float fold = sin(x * 2.3 - t + r * 0.12);
                float y = 0.34 * wave + 0.20 * fold
                    + dispersion * 0.055 * cos(x + t)
                    + r * 0.095 * sin(x - t);
                float d = p.y - y;
                float thickness = max(0.006, 1.2 / max(ubuf.resolution.y, 1.0));
                light += channels[channel] * filament(d, thickness) * envelope * 0.48;
            }
        }
        // A soft pool grounds the bright, finely resolved wavefronts.
        light += vec3(0.05, 0.12, 0.19) * exp(-p.y*p.y * 6.0 - p.x*p.x * 3.0) * 0.22;
        light *= smoothstep(1.0, 0.76, abs(p.y));
    } else if (kind < 1.5) {
        float radius = length(p);
        float angle = atan(p.y, p.x);
        float pixel = 2.0 / max(1.0, min(ubuf.resolution.x, ubuf.resolution.y));
        for (int channel = 0; channel < 3; ++channel) {
            float offset = float(channel - 1);
            float orbit = 0.70 + offset * 0.055 * sin(angle * 2.0 + t);
            float trail = progress >= 0.0 ? fract((angle + TAU * 0.25) / TAU + 1.0)
                : fract((angle - t) / TAU + float(channel) * 0.055 + 1.0);
            float gate = progress >= 0.0 ? step(trail, clamp(progress, 0.0, 1.0))
                : smoothstep(0.10, 0.85, trail) * (1.0 - smoothstep(0.94, 1.0, trail));
            light += channels[channel] * filament(radius - orbit, pixel * 0.63) * gate;
        }
        light += vec3(0.09, 0.13, 0.16) * exp(-pow((radius - 0.70) / pixel, 2.0)) * 0.45;
    } else {
        float head = progress < 0.0 ? -0.2 + fract(ubuf.lightPhase) * 1.4 : clamp(progress, 0.0, 1.0);
        float body = progress < 0.0 ? exp(-pow((uv.x - head) / 0.17, 2.0))
            : 1.0 - smoothstep(head - 0.002, head + 0.002, uv.x);
        for (int channel = 0; channel < 3; ++channel) {
            float y = 0.20 * sin(uv.x * 12.0 + t + float(channel) * 0.55);
            light += channels[channel] * filament(p.y - y, 0.42) * body * 0.7;
        }
        light += vec3(0.7, 0.8, 0.83) * exp(-pow((uv.x - head) * ubuf.resolution.x / 2.5, 2.0))
            * exp(-p.y*p.y * 2.0) * body;
    }
    if (ubuf.parameters.w > 0.5) light = vec3(0.80, 0.36, 0.20) * dot(light, vec3(0.333));
    // Exponential response preserves colored fringes and avoids clipped white bands.
    vec3 rgb = vec3(1.0) - exp(-light * 1.8);
    float alpha = max(rgb.r, max(rgb.g, rgb.b));
    fragmentColor = vec4(rgb, alpha) * ubuf.qt_Opacity;
}
