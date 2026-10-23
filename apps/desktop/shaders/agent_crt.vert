#version 440
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 textureCoordinate;
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
    vec4 displayStyle;
    float displayPhase;
    float reactionAmount;
} ubuf;
void main() {
    textureCoordinate = qt_MultiTexCoord0;
    gl_Position = ubuf.qt_Matrix * qt_Vertex;
}
