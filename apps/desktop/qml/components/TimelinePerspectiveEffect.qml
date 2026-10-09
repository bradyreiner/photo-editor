import QtQuick

ShaderEffect {
    property var source
    property real itemWidth: width
    property real itemHeight: height
    property real perspectiveX: 0
    property real perspectiveY: 0

    vertexShader: "qrc:/photo-editor/shaders/timeline_perspective.vert.qsb"
    fragmentShader: "qrc:/photo-editor/shaders/timeline_perspective.frag.qsb"
}
