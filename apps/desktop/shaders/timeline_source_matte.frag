#version 440

layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragmentColor;

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D matteSource;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float matteEnabled;
    float matteMode;
    float matteInvert;
    float matteFillEnabled;
    vec4 matteFillColor;
} ubuf;

float linearSrgb(float value)
{
    return value <= 0.04045
        ? value / 12.92
        : pow((value + 0.055) / 1.055, 2.4);
}

void main()
{
    vec4 pixel = texture(source, textureCoordinate);
    if (ubuf.matteEnabled > 0.5) {
        vec4 matte = texture(matteSource, textureCoordinate);
        float coverage = matte.a;
        if (ubuf.matteMode > 0.5) {
            vec3 straightMatte = matte.a > 0.00001
                ? clamp(matte.rgb / matte.a, 0.0, 1.0)
                : vec3(0.0);
            coverage = (0.2126 * linearSrgb(straightMatte.r)
                      + 0.7152 * linearSrgb(straightMatte.g)
                      + 0.0722 * linearSrgb(straightMatte.b)) * matte.a;
        }
        if (ubuf.matteInvert > 0.5)
            coverage = 1.0 - coverage;
        coverage = clamp(coverage, 0.0, 1.0);
        float outputAlpha = pixel.a * coverage;
        if (ubuf.matteFillEnabled > 0.5) {
            outputAlpha *= ubuf.matteFillColor.a;
            pixel.rgb = ubuf.matteFillColor.rgb * outputAlpha;
        } else {
            pixel.rgb *= coverage;
        }
        pixel.a = outputAlpha;
    }
    fragmentColor = pixel * ubuf.qt_Opacity;
}
