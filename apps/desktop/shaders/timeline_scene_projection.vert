#version 440

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 textureCoordinate;
layout(location = 1) out vec3 sceneCoordinate;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float itemWidth;
    float itemHeight;
    vec4 topLeft;
    vec4 topRight;
    vec4 bottomLeft;
    vec4 bottomRight;
    vec4 occlusionRect;
    float occlusionEnabled;
    float occlusionReady;
    vec4 fadeColor;
    float fadeAmount;
} ubuf;

void main()
{
    vec2 p0 = ubuf.topLeft.xy;
    vec2 p1 = ubuf.topRight.xy;
    vec2 p2 = ubuf.bottomRight.xy;
    vec2 p3 = ubuf.bottomLeft.xy;

    float dx1 = p1.x - p2.x;
    float dx2 = p3.x - p2.x;
    float dx3 = p0.x - p1.x + p2.x - p3.x;
    float dy1 = p1.y - p2.y;
    float dy2 = p3.y - p2.y;
    float dy3 = p0.y - p1.y + p2.y - p3.y;
    float denominator = dx1 * dy2 - dx2 * dy1;
    float projectiveX = abs(denominator) > 0.00001
            ? (dx3 * dy2 - dx2 * dy3) / denominator : 0.0;
    float projectiveY = abs(denominator) > 0.00001
            ? (dx1 * dy3 - dx3 * dy1) / denominator : 0.0;

    float a = p1.x - p0.x + projectiveX * p1.x;
    float b = p3.x - p0.x + projectiveY * p3.x;
    float c = p0.x;
    float d = p1.y - p0.y + projectiveX * p1.y;
    float e = p3.y - p0.y + projectiveY * p3.y;
    float f = p0.y;

    vec2 uv = qt_MultiTexCoord0;
    float w = projectiveX * uv.x + projectiveY * uv.y + 1.0;
    vec2 projectedNumerator = vec2(a * uv.x + b * uv.y + c,
                                   d * uv.x + e * uv.y + f);
    gl_Position = ubuf.qt_Matrix
            * vec4(projectedNumerator.x * ubuf.itemWidth,
                   projectedNumerator.y * ubuf.itemHeight,
                   qt_Vertex.z * w,
                   w);
    textureCoordinate = uv;
    sceneCoordinate = vec3(projectedNumerator, w);
}
