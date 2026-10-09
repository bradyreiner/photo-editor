#version 440
layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragmentColor;
layout(binding = 1) uniform sampler2D source;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 screenMetrics;
    float effectStrength;
    float activityPhase;
    float signalBreak;
    float blinkAmount;
    vec4 activityData;
    vec4 activityBounds;
    vec4 eyeBounds;
    vec4 activityColor;
} ubuf;
void main() {
    vec2 pixels = max(ubuf.screenMetrics.xy, vec2(1.0));
    vec2 uv = textureCoordinate;
    // A short signal displacement follows actual character state changes.
    // No random grain: the display pattern stays at physical pixel scale.
    float band = step(0.0, sin((uv.y + ubuf.screenMetrics.z) * 113.0) - 0.985);
    uv.x += band * ubuf.signalBreak * 3.0 / pixels.x;
    vec2 fringe = vec2(1.05 / pixels.x, 0.0) * ubuf.effectStrength;
    vec4 center = texture(source, uv);
    vec4 red = texture(source, uv + fringe);
    vec4 blue = texture(source, uv - fringe);
    // Blink only the two narrow eye marks, keeping the cached body texture.
    float eyeX = min(abs(uv.x - ubuf.eyeBounds.x), abs(uv.x - ubuf.eyeBounds.y));
    bool inEye = ubuf.eyeBounds.w > 0.0 && eyeX < 2.0 / pixels.x
        && uv.y > ubuf.eyeBounds.z - 2.0 / pixels.y
        && uv.y < ubuf.eyeBounds.z + ubuf.eyeBounds.w + 2.0 / pixels.y;
    float blink = inEye ? ubuf.blinkAmount : 1.0;
    center *= blink; red *= blink; blue *= blink;
    float alpha = max(center.a, max(red.a, blue.a));
    vec3 rgb = vec3(red.r, center.g, blue.b);
    float scan = 0.985 + 0.015 * cos(uv.y * pixels.y * 2.0943951);
    // Stagger phosphor rows and keep the modulation low: aligned vertical
    // and horizontal stripes read as woven cloth on broad ivory surfaces.
    float rowOffset = mod(floor(uv.y * pixels.y / 3.0), 2.0);
    float subpixel = mod(floor(uv.x * pixels.x) + rowOffset, 3.0);
    vec3 triad = subpixel < 1.0 ? vec3(1.006, 0.997, 0.997)
              : subpixel < 2.0 ? vec3(0.997, 1.006, 0.997) : vec3(0.997, 0.997, 1.006);
    rgb *= mix(vec3(1.0), triad * scan, ubuf.effectStrength);
    // A continuous, eased ping-pong signal is built into the character body.
    // Only this scalar changes on the render thread; the source art stays cached.
    if (ubuf.activityData.z > 0.5) {
        float start = ubuf.activityBounds.x;
        float end = ubuf.activityBounds.y;
        float y = ubuf.activityBounds.z;
        float extent = max(end - start, 0.0001);
        float p = 0.5 - 0.5 * cos(ubuf.activityPhase * 6.2831853);
        float position = mix(start, end, p);
        float stroke = 1.0 / pixels.y;
        float distanceToLight;
        if (ubuf.activityData.y >= 0.0) {
            // Brightness travels only inside the real measured fill.
            position = mix(start, start + extent * clamp(ubuf.activityData.y, 0.0, 1.0), p);
            distanceToLight = max(abs(uv.y - y) - stroke * 0.5,
                                  abs(uv.x - position) - extent * 0.035);
        } else if (ubuf.activityData.x < 0.5) {
            distanceToLight = max(abs(uv.y - y) - stroke * 0.5,
                                  abs(uv.x - position) - extent * 0.085);
        } else if (ubuf.activityData.x < 1.5) {
            float gap = (1.0 - sin(p * 3.1415927)) * extent * 0.22;
            distanceToLight = max(abs(uv.y - y) - stroke * 0.5,
                max(start - uv.x, max(uv.x - end, gap - abs(uv.x - (start + end) * 0.5))));
        } else if (ubuf.activityData.x < 2.5) {
            float index = clamp(floor((uv.x - start) / extent * 3.0), 0.0, 2.0);
            float tickX = start + extent * (index + 0.5) / 3.0;
            distanceToLight = max(abs(uv.x - tickX) - 0.65 / pixels.x,
                                  abs(uv.y - y) - ubuf.activityBounds.w * 0.012);
            distanceToLight += (1.0 - max(0.0, sin((p - index * 0.18) * 6.2831853))) * 1.3 / pixels.y;
        } else {
            // A tiny chevron slides along the same filament during export.
            float dy = abs(uv.y - y);
            float dx = abs(uv.x - (position - dy * pixels.y / pixels.x));
            distanceToLight = max(dx - 0.65 / pixels.x, dy - ubuf.activityBounds.w * 0.014);
        }
        float light = 1.0 - smoothstep(0.0, 1.2 / pixels.y, distanceToLight);
        // Premultiplied source-over preserves transparent surroundings.
        float lightAlpha = light * ubuf.activityColor.a;
        rgb = ubuf.activityColor.rgb * lightAlpha + rgb * (1.0 - lightAlpha);
        alpha = lightAlpha + alpha * (1.0 - lightAlpha);
    }
    // All channels are premultiplied; transparent surroundings stay clear.
    fragmentColor = vec4(min(rgb, vec3(alpha)), alpha) * ubuf.qt_Opacity;
}
