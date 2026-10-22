#version 440

layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragmentColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float itemWidth;
    float itemHeight;
    float perspectiveX;
    float perspectiveY;
    vec4 fadeColor;
    float fadeAmount;
} ubuf;

void main()
{
    vec4 pixel = texture(source, textureCoordinate);
    pixel.rgb = mix(pixel.rgb, ubuf.fadeColor.rgb * pixel.a, clamp(ubuf.fadeAmount, 0.0, 1.0));
    fragmentColor = pixel * ubuf.qt_Opacity;
}
