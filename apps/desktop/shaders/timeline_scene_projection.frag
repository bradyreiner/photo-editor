#version 440

layout(location = 0) in vec2 textureCoordinate;
layout(location = 1) in vec3 sceneCoordinate;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D occlusionMask;
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
} ubuf;

void main()
{
    fragColor = texture(source, textureCoordinate) * ubuf.qt_Opacity;
    if (ubuf.occlusionEnabled > 0.5) {
        if (ubuf.occlusionReady < 0.5) {
            fragColor = vec4(0.0);
            return;
        }
        vec2 sceneUV = sceneCoordinate.xy / sceneCoordinate.z;
        vec2 maskUV = (sceneUV - ubuf.occlusionRect.xy) / ubuf.occlusionRect.zw;
        if (all(greaterThanEqual(maskUV, vec2(0.0))) && all(lessThanEqual(maskUV, vec2(1.0))))
            fragColor *= 1.0 - texture(occlusionMask, maskUV).a;
    }
}
