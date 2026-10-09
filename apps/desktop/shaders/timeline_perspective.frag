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
} ubuf;

void main()
{
    fragmentColor = texture(source, textureCoordinate) * ubuf.qt_Opacity;
}
