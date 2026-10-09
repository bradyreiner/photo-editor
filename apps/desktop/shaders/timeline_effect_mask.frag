#version 440

layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragmentColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 pixelSize;
    vec4 contentRect;
    vec4 effect0; vec4 geometry0; vec4 mask0;
    vec4 effect1; vec4 geometry1; vec4 mask1;
    vec4 effect2; vec4 geometry2; vec4 mask2;
    vec4 effect3; vec4 geometry3; vec4 mask3;
    vec4 effect4; vec4 geometry4; vec4 mask4;
    vec4 effect5; vec4 geometry5; vec4 mask5;
} ubuf;

vec3 applyColorAdjustment(vec3 rgb, float code, float amount)
{
    float brightness = 0.0;
    float contrast = 1.0;
    float saturation = 1.0;
    if (code < 1.5) brightness = clamp(amount * 0.40, -1.0, 1.0);
    else if (code < 2.5) contrast = clamp(1.0 + amount * 0.80, 0.0, 2.0);
    else if (code < 3.5) saturation = clamp(1.0 + amount, 0.0, 3.0);
    else if (code < 4.5) saturation = clamp(1.0 - amount, 0.0, 3.0);
    else if (code < 5.5) {
        brightness = clamp(amount * 0.08, -1.0, 1.0);
        contrast = clamp(1.0 - amount * 0.35, 0.0, 2.0);
    } else {
        contrast = clamp(1.0 + amount * 0.35, 0.0, 2.0);
        saturation = clamp(1.0 + amount * 0.40, 0.0, 3.0);
    }

    const float kr = 0.2126;
    const float kg = 0.7152;
    const float kb = 0.0722;
    float luma = dot(rgb, vec3(kr, kg, kb));
    float cb = (rgb.b - luma) / (2.0 * (1.0 - kb)) + 0.5;
    float cr = (rgb.r - luma) / (2.0 * (1.0 - kr)) + 0.5;
    float adjustedLuma = clamp(contrast * (luma - 0.5) + 0.5 + brightness, 0.0, 1.0);
    cb = clamp(saturation * (cb - 0.5) + 0.5, 0.0, 1.0);
    cr = clamp(saturation * (cr - 0.5) + 0.5, 0.0, 1.0);
    float red = adjustedLuma + 2.0 * (1.0 - kr) * (cr - 0.5);
    float blue = adjustedLuma + 2.0 * (1.0 - kb) * (cb - 0.5);
    float green = (adjustedLuma - kr * red - kb * blue) / kg;
    return vec3(red, green, blue);
}

float maskCoverage(vec2 uv, vec4 geometry, vec4 featherShape, vec2 pixelSize)
{
    vec2 centered = (uv - geometry.xy) * pixelSize;
    vec2 halfSize = max(geometry.zw * pixelSize * 0.5, vec2(1.0));
    float signedDistance;
    if (featherShape.y > 0.5) {
        vec2 d = abs(centered) - halfSize;
        signedDistance = length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
    } else {
        float radius = length(centered / halfSize);
        signedDistance = (radius - 1.0) * min(halfSize.x, halfSize.y);
    }
    float featherPixels = featherShape.x * min(pixelSize.x, pixelSize.y);
    return featherPixels <= 0.0001
        ? (signedDistance <= 0.0 ? 1.0 : 0.0)
        : clamp(0.5 - signedDistance / featherPixels, 0.0, 1.0);
}

vec3 applyMaskedEffect(vec3 color, vec2 uv, vec2 pixelSize,
                       vec4 effect, vec4 geometry, vec4 featherShape)
{
    if (effect.z < 0.5 || effect.x < 0.5) return color;
    float coverage = maskCoverage(uv, geometry, featherShape, pixelSize);
    if (effect.w > 0.5) coverage = 1.0 - coverage;
    vec3 effected = applyColorAdjustment(color, effect.x, effect.y);
    return mix(color, effected, coverage);
}

void main()
{
    vec4 color = texture(source, textureCoordinate);
    float alpha = color.a;
    vec3 straightRgb = alpha > 0.00001 ? color.rgb / alpha : vec3(0.0);
    vec2 sourceCoordinate = (textureCoordinate - ubuf.contentRect.xy) / ubuf.contentRect.zw;
    straightRgb = applyMaskedEffect(straightRgb, sourceCoordinate, ubuf.pixelSize.xy,
                                    ubuf.effect0, ubuf.geometry0, ubuf.mask0);
    straightRgb = applyMaskedEffect(straightRgb, sourceCoordinate, ubuf.pixelSize.xy,
                                    ubuf.effect1, ubuf.geometry1, ubuf.mask1);
    straightRgb = applyMaskedEffect(straightRgb, sourceCoordinate, ubuf.pixelSize.xy,
                                    ubuf.effect2, ubuf.geometry2, ubuf.mask2);
    straightRgb = applyMaskedEffect(straightRgb, sourceCoordinate, ubuf.pixelSize.xy,
                                    ubuf.effect3, ubuf.geometry3, ubuf.mask3);
    straightRgb = applyMaskedEffect(straightRgb, sourceCoordinate, ubuf.pixelSize.xy,
                                    ubuf.effect4, ubuf.geometry4, ubuf.mask4);
    straightRgb = applyMaskedEffect(straightRgb, sourceCoordinate, ubuf.pixelSize.xy,
                                    ubuf.effect5, ubuf.geometry5, ubuf.mask5);
    color.rgb = straightRgb * alpha;
    fragmentColor = color * ubuf.qt_Opacity;
}
