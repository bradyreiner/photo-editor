import QtQuick

ShaderEffect {
    id: root

    property var source
    property url sourceMatteUrl: ""
    property int sourceMatteFillMode: Image.Stretch
    property var matteSource: matteTexture
    property real matteEnabled: matteImage.status === Image.Ready ? 1 : 0
    property real matteMode: 0
    property real matteInvert: 0
    property real matteFillEnabled: 0
    property color matteFillColor: "transparent"
    property real itemWidth: width
    property real itemHeight: height
    property real perspectiveX: 0
    property real perspectiveY: 0
    property var sceneQuad: []
    readonly property real frameQuadEnabled: sceneQuad && sceneQuad.length === 4 ? 1 : 0
    readonly property vector4d frameQuadTop: Qt.vector4d(
        scenePoint(0).x, scenePoint(0).y, scenePoint(1).x, scenePoint(1).y)
    readonly property vector4d frameQuadBottom: Qt.vector4d(
        scenePoint(3).x, scenePoint(3).y, scenePoint(2).x, scenePoint(2).y)
    property color fadeColor: "black"
    property real fadeAmount: 0
    property real frameCornerRadius: 0
    property real clipMaskEnabled: 0
    property real clipMaskShape: 0
    property vector4d clipMaskRect: Qt.vector4d(0, 0, 1, 1)
    property real clipMaskCornerRadius: 0
    property size clipMaskCanvasSize: Qt.size(width, height)
    property point clipMaskFrameCenter: Qt.point(width * 0.5, height * 0.5)
    property real clipMaskFrameScale: 1
    property real clipMaskFrameRotationDegrees: 0

    Image {
        id: matteImage
        anchors.fill: parent
        source: root.sourceMatteUrl
        fillMode: root.sourceMatteFillMode
        asynchronous: true
        cache: true
        visible: true
    }

    function scenePoint(index) {
        const point = sceneQuad && sceneQuad.length === 4 ? sceneQuad[index] : null
        if (point)
            return Qt.point(Number(point.x), Number(point.y))
        return Qt.point(0, 0)
    }

    ShaderEffectSource {
        id: matteTexture
        anchors.fill: parent
        sourceItem: matteImage
        live: true
        hideSource: true
        visible: true
        opacity: 0
    }

    vertexShader: "qrc:/photo-editor/shaders/timeline_perspective.vert.qsb"
    fragmentShader: "qrc:/photo-editor/shaders/timeline_perspective.frag.qsb"
}
