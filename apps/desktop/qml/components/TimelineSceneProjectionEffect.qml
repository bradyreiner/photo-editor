import QtQuick

ShaderEffect {
    id: root

    property var source
    property var sceneQuad: []
    property var occlusionFrame: ({})
    property var occlusionMask: Image {
        source: String(root.occlusionFrame.source || "")
        visible: false
        asynchronous: false
        smooth: true
        cache: false
    }
    readonly property real occlusionEnabled: occlusionFrame.enabled === true ? 1 : 0
    readonly property real occlusionReady: occlusionFrame.ready === true && occlusionMask.status === Image.Ready ? 1 : 0
    readonly property vector4d occlusionRect: Qt.vector4d(
        Number(occlusionFrame.rect && occlusionFrame.rect.x || 0),
        Number(occlusionFrame.rect && occlusionFrame.rect.y || 0),
        Number(occlusionFrame.rect && occlusionFrame.rect.width || 1),
        Number(occlusionFrame.rect && occlusionFrame.rect.height || 1))
    property real itemWidth: width
    property real itemHeight: height

    readonly property vector4d topLeft: pointVector(0)
    readonly property vector4d topRight: pointVector(1)
    readonly property vector4d bottomLeft: pointVector(2)
    readonly property vector4d bottomRight: pointVector(3)

    function pointVector(index) {
        const point = sceneQuad && sceneQuad.length > index ? sceneQuad[index] : null
        return Qt.vector4d(Number(point && point.x || 0),
                           Number(point && point.y || 0), 0, 0)
    }

    vertexShader: "qrc:/photo-editor/shaders/timeline_scene_projection.vert.qsb"
    fragmentShader: "qrc:/photo-editor/shaders/timeline_scene_projection.frag.qsb"
}
