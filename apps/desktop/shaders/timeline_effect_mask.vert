#version 440

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 textureCoordinate;

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

void main()
{
    textureCoordinate = qt_MultiTexCoord0;
    gl_Position = ubuf.qt_Matrix * qt_Vertex;
}
