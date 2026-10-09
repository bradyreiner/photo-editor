pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Item {
    id: root
    required property var theme
    property string previewFrameSource: ""
    required property var surfaceQuad
    required property real sourceWidth
    required property real sourceHeight
    required property int sourceFillMode
    signal correctionAccepted(var quad)
    signal correctionCancelled

    property var draftQuad: []
    property int activeCorner: -1
    property real contentX: 0
    property real contentY: 0
    property real contentWidth: width
    property real contentHeight: height

    function point(index) {
        return draftQuad.length === 4 ? draftQuad[index] : ({"x":0, "y":0})
    }

    function updateContentRect() {
        contentX = 0
        contentY = 0
        contentWidth = width
        contentHeight = height
        const aspect = sourceWidth / Math.max(1, sourceHeight)
        const itemAspect = width / Math.max(1, height)
        if (sourceFillMode === Image.PreserveAspectFit) {
            if (aspect > itemAspect) {
                contentHeight = width / aspect
                contentY = (height - contentHeight) / 2
            } else {
                contentWidth = height * aspect
                contentX = (width - contentWidth) / 2
            }
        } else if (sourceFillMode === Image.PreserveAspectCrop) {
            if (aspect > itemAspect) {
                contentWidth = height * aspect
                contentX = (width - contentWidth) / 2
            } else {
                contentHeight = width / aspect
                contentY = (height - contentHeight) / 2
            }
        }
    }

    function pointAt(x, y) {
        return ({
            "x": Math.max(0, Math.min(1, (x - contentX) / Math.max(1, contentWidth))),
            "y": Math.max(0, Math.min(1, (y - contentY) / Math.max(1, contentHeight)))
        })
    }

    function screenPoint(index) {
        const p = point(index)
        return Qt.point(contentX + Number(p.x || 0) * contentWidth,
                        contentY + Number(p.y || 0) * contentHeight)
    }

    onSurfaceQuadChanged: draftQuad = (surfaceQuad || []).map(function(point) {
        return {"x": Number(point.x || 0), "y": Number(point.y || 0)}
    })
    onWidthChanged: updateContentRect()
    onHeightChanged: updateContentRect()
    Component.onCompleted: {
        updateContentRect()
        draftQuad = (surfaceQuad || []).map(function(point) {
            return {"x": Number(point.x || 0), "y": Number(point.y || 0)}
        })
    }

    Image {
        anchors.fill: parent
        source: root.visible ? root.previewFrameSource : ""
        fillMode: root.sourceFillMode
        cache: false
    }

    Rectangle {
        anchors.fill: parent
        color: root.theme.alpha(root.theme.ink950, 0.24)
        border.width: 1
        border.color: root.theme.alpha(root.theme.accentWarm300, 0.86)
    }

    Canvas {
        id: outline
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (root.draftQuad.length !== 4)
                return
            const order = [0, 1, 3, 2]
            ctx.beginPath()
            for (let i = 0; i < order.length; ++i) {
                const p = root.screenPoint(order[i])
                if (i === 0)
                    ctx.moveTo(p.x, p.y)
                else
                    ctx.lineTo(p.x, p.y)
            }
            ctx.closePath()
            ctx.strokeStyle = Qt.rgba(246 / 255, 188 / 255, 79 / 255, 1)
            ctx.lineWidth = 2
            ctx.stroke()
        }
        Connections {
            target: root
            function onDraftQuadChanged() { outline.requestPaint() }
            function onContentXChanged() { outline.requestPaint() }
            function onContentYChanged() { outline.requestPaint() }
            function onContentWidthChanged() { outline.requestPaint() }
            function onContentHeightChanged() { outline.requestPaint() }
        }
    }

    Repeater {
        model: 4
        delegate: Item {
            required property int index
            objectName: "timelineSurfaceCorrectionHandle" + index
            readonly property point center: root.screenPoint(index)
            x: center.x - handle.width / 2
            y: center.y - handle.height / 2
            width: handle.width
            height: handle.height
            Rectangle {
                id: handle
                anchors.centerIn: parent
                width: root.theme.scaled(18)
                height: width
                radius: width / 2
                color: root.activeCorner === index ? root.theme.accentWarm300 : root.theme.ink950
                border.width: 2
                border.color: root.theme.accentWarm300
            }
            Label {
                anchors.left: handle.right
                anchors.leftMargin: root.theme.space2
                anchors.verticalCenter: handle.verticalCenter
                text: ["TL", "TR", "BL", "BR"][index]
                color: root.theme.textStrong
                font.family: root.theme.fontMono
                font.pixelSize: root.theme.typeMicro
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -root.theme.space4
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                onPressed: root.activeCorner = index
                onPositionChanged: function(mouse) {
                    if (pressed) {
                        const local = mapToItem(root, mouse.x, mouse.y)
                        const p = root.pointAt(local.x, local.y)
                        const next = root.draftQuad.slice(0)
                        next[index] = p
                        root.draftQuad = next
                    }
                }
                onReleased: root.activeCorner = -1
                onCanceled: root.activeCorner = -1
            }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: root.theme.space6
        width: correctionHud.implicitWidth + root.theme.space12
        height: correctionHud.implicitHeight + root.theme.space8
        radius: root.theme.radiusSurface
        color: root.theme.alpha(root.theme.ink950, 0.94)
        border.width: root.theme.hairlineWidth
        border.color: root.theme.alpha(root.theme.glassEdge, 0.48)
        RowLayout {
            id: correctionHud
            anchors.centerIn: parent
            spacing: root.theme.space6
            Label {
                text: "Drag the four surface corners on this frame"
                color: root.theme.textStrong
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeCaption
            }
            Components.ActionButton {
                objectName: "timelineSurfaceCorrectionCancelButton"
                text: "Cancel"
                iconName: "close"
                compact: true
                tone: "quiet"
                onClicked: root.correctionCancelled()
            }
            Components.ActionButton {
                objectName: "timelineSurfaceCorrectionApplyButton"
                text: "Correct and retrack"
                iconName: "check"
                tone: "primary"
                compact: true
                enabled: root.draftQuad.length === 4
                onClicked: root.correctionAccepted(root.draftQuad)
            }
        }
    }
}
