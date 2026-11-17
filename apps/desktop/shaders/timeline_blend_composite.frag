#version 440

layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragmentColor;

layout(binding = 1) uniform sampler2D backdropSource;
layout(binding = 2) uniform sampler2D foregroundSource;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float blendMode;
} ubuf;

vec3 screenBlend(vec3 backdrop, vec3 foreground)
{
    return 1.0 - (1.0 - backdrop) * (1.0 - foreground);
}

float softLightChannel(float backdrop, float foreground)
{
    if (foreground <= 0.5)
        return backdrop - (1.0 - 2.0 * foreground) * backdrop * (1.0 - backdrop);
    float curve = backdrop <= 0.25
        ? ((16.0 * backdrop - 12.0) * backdrop + 4.0) * backdrop
        : sqrt(max(backdrop, 0.0));
    return backdrop + (2.0 * foreground - 1.0) * (curve - backdrop);
}

vec3 softLightBlend(vec3 backdrop, vec3 foreground)
{
    return vec3(softLightChannel(backdrop.r, foreground.r),
                softLightChannel(backdrop.g, foreground.g),
                softLightChannel(backdrop.b, foreground.b));
}

void main()
{
    vec4 backdropPixel = texture(backdropSource, textureCoordinate);
    vec4 foregroundPixel = texture(foregroundSource, textureCoordinate);
    float backdropAlpha = clamp(backdropPixel.a, 0.0, 1.0);
    float foregroundAlpha = clamp(foregroundPixel.a, 0.0, 1.0);
    vec3 backdrop = backdropAlpha > 0.00001
        ? clamp(backdropPixel.rgb / backdropAlpha, 0.0, 1.0)
        : vec3(0.0);
    vec3 foreground = foregroundAlpha > 0.00001
        ? clamp(foregroundPixel.rgb / foregroundAlpha, 0.0, 1.0)
        : vec3(0.0);
    vec3 blended = ubuf.blendMode > 0.5
        ? softLightBlend(backdrop, foreground)
        : screenBlend(backdrop, foreground);
    float outputAlpha = foregroundAlpha + backdropAlpha * (1.0 - foregroundAlpha);
    vec3 outputRgb = backdrop * backdropAlpha * (1.0 - foregroundAlpha)
        + foreground * foregroundAlpha * (1.0 - backdropAlpha)
        + blended * backdropAlpha * foregroundAlpha;
    fragmentColor = vec4(outputRgb, outputAlpha) * ubuf.qt_Opacity;
}
