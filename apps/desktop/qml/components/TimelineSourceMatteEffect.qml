import QtQuick

ShaderEffect {
    property var source
    property var matteSource
    property real matteEnabled: 0
    property real matteMode: 0
    property real matteInvert: 0
    property real matteFillEnabled: 0
    property color matteFillColor: "white"

    vertexShader: "qrc:/photo-editor/shaders/timeline_source_matte.vert.qsb"
    fragmentShader: "qrc:/photo-editor/shaders/timeline_source_matte.frag.qsb"
}
