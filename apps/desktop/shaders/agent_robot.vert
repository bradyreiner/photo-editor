#version 440
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 textureCoordinate;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 viewport;
    vec4 pose;
    vec4 behavior;
    vec4 tint;
    float motionPhase;
    float blinkAmount;
    float reactionAmount;
} ubuf;
void main() {
    textureCoordinate = qt_MultiTexCoord0;
    gl_Position = ubuf.qt_Matrix * qt_Vertex;
}
