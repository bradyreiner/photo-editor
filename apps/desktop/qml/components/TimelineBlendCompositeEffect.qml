import QtQuick

ShaderEffect {
    property var backdropSource
    property var foregroundSource
    property real blendMode: 0

    vertexShader: "qrc:/photo-editor/shaders/timeline_blend_composite.vert.qsb"
    fragmentShader: "qrc:/photo-editor/shaders/timeline_blend_composite.frag.qsb"
}
