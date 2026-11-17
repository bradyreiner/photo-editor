import QtQuick

// Native preview shader for the procedural light field used by the trailer.
// The time and gather inputs are driven by the active Timeline clip, so this
// remains an editable native generator rather than an imported movie.
ShaderEffect {
    id: root

    property real timeSeconds: 0
    property real gather: 0
    property size resolution: Qt.size(width, height)

    vertexShader: "qrc:/photo-editor/shaders/timeline_desktop_light.vert.qsb"
    fragmentShader: "qrc:/photo-editor/shaders/timeline_desktop_light.frag.qsb"
}
