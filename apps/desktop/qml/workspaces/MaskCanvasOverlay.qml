import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root

    required property var editRoot
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0

    visible: imageWidth > 0 && imageHeight > 0

    function previewPointX(normalizedX) {
        return normalizedX * previewItem.width
    }

    function previewPointY(normalizedY) {
        return normalizedY * previewItem.height
    }

    Item {
        id: previewItem
        x: root.imageX
        y: root.imageY
        width: root.imageWidth
        height: root.imageHeight
        visible: root.visible

        Repeater {
            model: editorPreview.maskStack

            delegate: Item {
                required property int index
                required property var modelData

                readonly property var mask: modelData
                readonly property bool active: index === editorPreview.activeMaskIndex
                readonly property color accent: editRoot.maskAccent(mask)

                anchors.fill: parent
                visible: !!mask && mask.enabled

                Item {
                    id: linearOverlay
                    visible: mask.kind === "linear"
                    anchors.centerIn: parent
                    width: parent.width * 1.35
                    height: parent.height * 1.35
                    rotation: mask.rotation
                    opacity: active ? 0.92 : 0.48

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.alpha(accent, 0.34) }
                            GradientStop {
                                position: Math.max(0.0, mask.position - mask.feather * 0.5)
                                color: Qt.alpha(accent, 0.14)
                            }
                            GradientStop { position: mask.position; color: Qt.rgba(0.95, 0.97, 0.98, 0.72) }
                            GradientStop {
                                position: Math.min(1.0, mask.position + mask.feather * 0.5)
                                color: Qt.alpha(accent, 0.06)
                            }
                            GradientStop { position: 1.0; color: Qt.alpha(accent, 0.0) }
                        }
                    }

                    Rectangle {
                        x: 0
                        y: Math.max(0, Math.min(linearOverlay.height - 1, linearOverlay.height * mask.position))
                        width: linearOverlay.width
                        height: 1
                        color: "#f4f7fa"
                        opacity: 0.9
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: active ? Qt.CrossCursor : Qt.PointingHandCursor
                        onPressed: {
                            editorPreview.activeMaskIndex = index
                            if (active)
                                editorPreview.activeMaskPosition = editRoot.clamp(mouse.y / linearOverlay.height, 0, 1)
                        }
                    }

                    Rectangle {
                        visible: active
                        x: linearOverlay.width / 2 - width / 2
                        y: linearOverlay.height * mask.position - height / 2
                        width: 16
                        height: 16
                        radius: 8
                        color: "#f4f7fa"
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeVerCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskPosition = editRoot.clamp(
                                            (parent.y + mouse.y + parent.height / 2) / linearOverlay.height,
                                            0,
                                            1)
                            }
                        }
                    }
                }

                Item {
                    id: radialOverlay
                    visible: mask.kind === "radial"
                    x: mask.centerX * parent.width - width / 2
                    y: mask.centerY * parent.height - height / 2
                    width: Math.max(24, parent.width * mask.radiusX * 2)
                    height: Math.max(24, parent.height * mask.radiusY * 2)
                    rotation: mask.rotation
                    opacity: active ? 0.92 : 0.48

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: active ? Qt.SizeAllCursor : Qt.PointingHandCursor
                        onPressed: editorPreview.activeMaskIndex = index
                    }

                    Canvas {
                        anchors.fill: parent

                        Connections {
                            target: editorPreview

                            function onControlsChanged() {
                                parent.requestPaint()
                            }
                        }

                        onPaint: {
                            const ctx = getContext("2d")
                            const innerScale = Math.max(0.0, 1.0 - mask.feather)
                            ctx.reset()
                            ctx.strokeStyle = "#f4f7fa"
                            ctx.lineWidth = active ? 1.8 : 1.2
                            ctx.fillStyle = Qt.alpha(accent, active ? 0.18 : 0.10)
                            ctx.beginPath()
                            ctx.ellipse(width / 2, height / 2, Math.max(1, width / 2 - 1), Math.max(1, height / 2 - 1), 0, 0, Math.PI * 2)
                            ctx.fill()
                            ctx.stroke()
                            ctx.strokeStyle = "rgba(244,247,250,0.45)"
                            ctx.beginPath()
                            ctx.ellipse(width / 2, height / 2,
                                        Math.max(1, (width / 2 - 1) * innerScale),
                                        Math.max(1, (height / 2 - 1) * innerScale),
                                        0, 0, Math.PI * 2)
                            ctx.stroke()
                        }
                    }

                    Rectangle {
                        visible: active
                        x: radialOverlay.width / 2 - width / 2
                        y: radialOverlay.height / 2 - height / 2
                        width: 16
                        height: 16
                        radius: 8
                        color: "#f4f7fa"
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskCenterX = editRoot.clamp((radialOverlay.x + parent.x + mouse.x) / previewItem.width, 0, 1)
                                editorPreview.activeMaskCenterY = editRoot.clamp((radialOverlay.y + parent.y + mouse.y) / previewItem.height, 0, 1)
                            }
                        }
                    }
                }

                Item {
                    id: brushOverlay
                    visible: mask.kind === "brush"
                    anchors.fill: parent
                    opacity: active ? 0.92 : 0.52

                    Repeater {
                        model: mask.brushPoints || []

                        delegate: Rectangle {
                            required property var modelData

                            readonly property real pointDiameter: Math.max(8, Math.min(previewItem.width, previewItem.height) * (modelData.size || mask.brushSize || 0.08) * 2)
                            x: (modelData.x || 0.5) * brushOverlay.width - width / 2
                            y: (modelData.y || 0.5) * brushOverlay.height - height / 2
                            width: pointDiameter
                            height: pointDiameter
                            radius: pointDiameter / 2
                            color: Qt.alpha(accent, Math.max(0.12, (modelData.flow || mask.brushFlow || 1.0) * 0.28))
                            border.width: active ? 1 : 0
                            border.color: Qt.alpha("#f4f7fa", 0.45)
                        }
                    }

                    Item {
                        visible: active && mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
                        x: (mask.cloneSourceX || 0.5) * brushOverlay.width - width / 2
                        y: (mask.cloneSourceY || 0.5) * brushOverlay.height - height / 2
                        width: 22
                        height: 22

                        Rectangle {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            radius: 9
                            color: "transparent"
                            border.width: 1.5
                            border.color: "#f4f7fa"
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 2
                            height: 22
                            radius: 1
                            color: "#f4f7fa"
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 22
                            height: 2
                            radius: 1
                            color: "#f4f7fa"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        enabled: active
                        cursorShape: Qt.CrossCursor
                        onPressed: {
                            editorPreview.activeMaskIndex = index
                            if (mask.retouchMode === "clone" &&
                                    (editRoot.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                editorPreview.setActiveMaskCloneSource(
                                            mouse.x / brushOverlay.width,
                                            mouse.y / brushOverlay.height)
                                editRoot.cloneSourcePickArmed = false
                                return
                            }
                            editorPreview.addBrushPoint(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height)
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed || editRoot.cloneSourcePickArmed ||
                                    (mask.retouchMode === "clone" && !(mask.cloneSourceSet || false)))
                                return
                            editorPreview.addBrushPoint(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height)
                        }
                    }
                }

                Item {
                    id: polygonOverlay
                    visible: mask.kind === "polygon"
                    anchors.fill: parent
                    opacity: active ? 0.95 : 0.56

                    Canvas {
                        anchors.fill: parent

                        Connections {
                            target: editorPreview

                            function onControlsChanged() {
                                parent.requestPaint()
                            }
                        }

                        onPaint: {
                            const points = mask.polygonPoints || []
                            const count = points.length
                            const ctx = getContext("2d")
                            ctx.reset()
                            if (count === 0)
                                return

                            ctx.beginPath()
                            ctx.moveTo(root.previewPointX(points[0].x || 0.5), root.previewPointY(points[0].y || 0.5))
                            for (let i = 1; i < count; ++i)
                                ctx.lineTo(root.previewPointX(points[i].x || 0.5), root.previewPointY(points[i].y || 0.5))
                            if (count >= 3)
                                ctx.closePath()

                            if (count >= 3) {
                                ctx.fillStyle = Qt.alpha(accent, active ? 0.16 : 0.08)
                                ctx.fill()
                            }

                            ctx.strokeStyle = Qt.alpha("#f4f7fa", active ? 0.88 : 0.55)
                            ctx.lineWidth = active ? 1.8 : 1.1
                            if (ctx.setLineDash)
                                ctx.setLineDash([8, 6])
                            ctx.lineDashOffset = -editRoot.selectionOutlinePhase
                            ctx.stroke()
                        }
                    }
                }
            }
        }

        Canvas {
            id: activeSelectionOutline
            anchors.fill: parent
            visible: editorPreview.hasActiveMask &&
                     editorPreview.activeMaskOutlinePoints.length >= 2 &&
                     (editorPreview.activeMaskKind === "subject" ||
                      editorPreview.activeMaskKind === "object" ||
                      editorPreview.activeMaskKind === "polygon")
            z: 96

            Connections {
                target: editorPreview

                function onControlsChanged() {
                    activeSelectionOutline.requestPaint()
                }
            }

            Connections {
                target: editRoot

                function onSelectionOutlinePhaseChanged() {
                    activeSelectionOutline.requestPaint()
                }
            }

            onPaint: {
                const points = editorPreview.activeMaskOutlinePoints
                const count = points.length
                const ctx = getContext("2d")
                ctx.reset()
                if (count < 2)
                    return

                const activeMask = editorPreview.activeMaskIndex >= 0
                                   ? editorPreview.maskStack[editorPreview.activeMaskIndex]
                                   : null
                const accent = editRoot.maskAccent(activeMask)
                ctx.beginPath()
                ctx.moveTo(root.previewPointX(points[0].x || 0.5), root.previewPointY(points[0].y || 0.5))
                for (let i = 1; i < count; ++i)
                    ctx.lineTo(root.previewPointX(points[i].x || 0.5), root.previewPointY(points[i].y || 0.5))
                if (count >= 3)
                    ctx.closePath()

                if (count >= 3) {
                    ctx.fillStyle = Qt.alpha(accent, 0.12)
                    ctx.fill()
                }

                ctx.lineWidth = 2
                if (ctx.setLineDash)
                    ctx.setLineDash([8, 6])
                ctx.lineDashOffset = -editRoot.selectionOutlinePhase
                ctx.strokeStyle = "#f4f7fa"
                ctx.stroke()
                if (ctx.setLineDash)
                    ctx.setLineDash([8, 6])
                ctx.lineDashOffset = 12 - editRoot.selectionOutlinePhase
                ctx.strokeStyle = Qt.alpha("#101419", 0.78)
                ctx.stroke()
            }
        }

        Canvas {
            id: smartPreviewOutline
            anchors.fill: parent
            visible: editRoot.objectPickArmed &&
                     editorPreview.smartSelectPreviewActive &&
                     editorPreview.smartSelectPreviewOutlinePoints.length >= 2
            z: 97

            Connections {
                target: editorPreview

                function onSmartSelectPreviewChanged() {
                    smartPreviewOutline.requestPaint()
                }
            }

            Connections {
                target: editRoot

                function onSelectionOutlinePhaseChanged() {
                    smartPreviewOutline.requestPaint()
                }
            }

            onPaint: {
                const points = editorPreview.smartSelectPreviewOutlinePoints
                const count = points.length
                const ctx = getContext("2d")
                ctx.reset()
                if (count < 2)
                    return

                ctx.beginPath()
                ctx.moveTo(root.previewPointX(points[0].x || 0.5), root.previewPointY(points[0].y || 0.5))
                for (let i = 1; i < count; ++i)
                    ctx.lineTo(root.previewPointX(points[i].x || 0.5), root.previewPointY(points[i].y || 0.5))
                if (count >= 3)
                    ctx.closePath()

                if (count >= 3) {
                    ctx.fillStyle = Qt.alpha("#5e95d3", 0.18)
                    ctx.fill()
                }

                ctx.lineWidth = 2
                if (ctx.setLineDash)
                    ctx.setLineDash([10, 6])
                ctx.lineDashOffset = -editRoot.selectionOutlinePhase
                ctx.strokeStyle = "#f4f7fa"
                ctx.stroke()
                if (ctx.setLineDash)
                    ctx.setLineDash([10, 6])
                ctx.lineDashOffset = 14 - editRoot.selectionOutlinePhase
                ctx.strokeStyle = Qt.alpha("#5e95d3", 0.88)
                ctx.stroke()
            }
        }

        MouseArea {
            x: 0
            y: 0
            width: previewItem.width
            height: previewItem.height
            enabled: editRoot.objectPickArmed || editRoot.polygonDrawArmed || editRoot.lassoDrawArmed
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: (editRoot.polygonDrawArmed || editRoot.lassoDrawArmed || editRoot.objectPickArmed)
                         ? Qt.CrossCursor
                         : Qt.ArrowCursor
            z: 100

            onEntered: function(mouse) {
                if (editRoot.objectPickArmed)
                    editorPreview.updateSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onExited: {
                if (editRoot.objectPickArmed)
                    editorPreview.clearSmartSelectPreview()
            }

            onPressed: function(mouse) {
                if (!editRoot.lassoDrawArmed)
                    return
                editorPreview.startPolygonSelection()
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onClicked: function(mouse) {
                if (editRoot.lassoDrawArmed)
                    return
                if (editRoot.polygonDrawArmed) {
                    editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
                } else {
                    editorPreview.smartSelectObjectAt(
                                mouse.x / previewItem.width,
                                mouse.y / previewItem.height,
                                editRoot.objectPickCreateNew)
                    editRoot.objectPickCreateNew = false
                }
            }

            onPositionChanged: function(mouse) {
                if (editRoot.objectPickArmed)
                    editorPreview.updateSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)

                if (!editRoot.lassoDrawArmed || !pressed)
                    return
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onDoubleClicked: function(mouse) {
                if (!editRoot.polygonDrawArmed)
                    return
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
                editRoot.polygonDrawArmed = false
            }

            onReleased: {
                if (!editRoot.lassoDrawArmed)
                    return
                editRoot.lassoDrawArmed = false
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: editRoot.objectPickArmed || editRoot.polygonDrawArmed || editRoot.lassoDrawArmed || editRoot.cloneSourcePickArmed
            z: 99
            color: Qt.alpha("#0b0e11", 0.16)
            border.width: 1
            border.color: editRoot.cloneSourcePickArmed ? "#7cb5ff"
                         : editRoot.lassoDrawArmed ? "#7ec97a"
                         : editRoot.polygonDrawArmed ? "#7ec97a"
                         : "#5e95d3"
            radius: 12

            Label {
                anchors.centerIn: parent
                text: editRoot.cloneSourcePickArmed
                      ? "Click a clean source area for the Clone Stamp."
                      : editRoot.lassoDrawArmed
                      ? "Drag to draw a freehand selection."
                      : editRoot.polygonDrawArmed
                      ? "Click to place polygon points. Double-click to finish."
                      : (editRoot.objectPickCreateNew
                         ? "Hover to preview a new smart selection, then click to create it."
                         : "Hover to preview objects in realtime, then click to add them.")
                color: "#f2f4f6"
                font.family: "Avenir Next"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 10
        z: 110

        Rectangle {
            width: 52
            radius: 14
            color: Qt.alpha("#101419", 0.88)
            border.width: 1
            border.color: "#2d3339"
            implicitHeight: toolRail.implicitHeight + 14

            Column {
                id: toolRail
                anchors.fill: parent
                anchors.margins: 7
                spacing: 6

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "smart-select"
                    tooltipText: "Smart Select"
                    active: editRoot.objectPickArmed && !editRoot.objectPickCreateNew
                    enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                    onClicked: editRoot.selectTool(editRoot.objectPickArmed && !editRoot.objectPickCreateNew ? "" : "smart")
                }

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "plus"
                    tooltipText: "New Smart Selection"
                    active: editRoot.objectPickArmed && editRoot.objectPickCreateNew
                    enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                    onClicked: editRoot.selectTool(editRoot.objectPickArmed && editRoot.objectPickCreateNew ? "" : "smart-new")
                }

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "lasso"
                    tooltipText: "Lasso"
                    active: editRoot.lassoDrawArmed
                    onClicked: editRoot.selectTool(editRoot.lassoDrawArmed ? "" : "lasso")
                }

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "polygon-tool"
                    tooltipText: "Polygon"
                    active: editRoot.polygonDrawArmed
                    onClicked: {
                        if (!editRoot.polygonDrawArmed && editorPreview.activeMaskKind !== "polygon")
                            editorPreview.startPolygonSelection()
                        editRoot.selectTool(editRoot.polygonDrawArmed ? "" : "polygon")
                    }
                }

                Rectangle {
                    width: 38
                    height: 1
                    radius: 1
                    color: "#2d3339"
                }

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "brush-tool"
                    tooltipText: "Refine Add Brush"
                    active: editorPreview.activeMaskKind === "brush" &&
                            editorPreview.activeMaskOperation === "add" &&
                            editorPreview.activeMaskRetouchMode.length === 0
                    onClicked: {
                        editRoot.clearTransientTools()
                        editorPreview.startSelectionBrush(false)
                    }
                }

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "brush-subtract"
                    tooltipText: "Refine Subtract Brush"
                    active: editorPreview.activeMaskKind === "brush" &&
                            editorPreview.activeMaskOperation === "subtract" &&
                            editorPreview.activeMaskRetouchMode.length === 0
                    onClicked: {
                        editRoot.clearTransientTools()
                        editorPreview.startSelectionBrush(true)
                    }
                }

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "effects"
                    tooltipText: "Heal Brush"
                    active: editorPreview.activeMaskKind === "brush" &&
                            editorPreview.activeMaskRetouchMode === "heal"
                    onClicked: {
                        editRoot.clearTransientTools()
                        editorPreview.startHealBrush()
                    }
                }

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "source"
                    tooltipText: "Clone Stamp"
                    active: editorPreview.activeMaskKind === "brush" &&
                            editorPreview.activeMaskRetouchMode === "clone"
                    onClicked: {
                        editRoot.clearTransientTools()
                        if (editorPreview.activeMaskKind === "brush" &&
                                editorPreview.activeMaskRetouchMode === "clone") {
                            editRoot.cloneSourcePickArmed = true
                        } else {
                            editorPreview.startCloneBrush()
                            editRoot.cloneSourcePickArmed = true
                        }
                    }
                }

                ActionButton {
                    width: 38
                    compact: true
                    showText: false
                    iconName: "erase-tool"
                    tooltipText: "Remove Brush"
                    active: editorPreview.activeMaskKind === "brush" &&
                            editorPreview.activeMaskRetouchMode === "remove"
                    onClicked: {
                        editRoot.clearTransientTools()
                        editorPreview.startRemoveBrush()
                    }
                }
            }
        }

        Rectangle {
            width: toolLabel.implicitWidth + 20
            height: toolLabel.implicitHeight + 12
            radius: 10
            color: Qt.alpha("#101419", 0.82)
            border.width: 1
            border.color: "#2d3339"

            Label {
                id: toolLabel
                anchors.centerIn: parent
                text: editRoot.activeToolLabel()
                color: "#f2f4f6"
                font.family: "Avenir Next"
                font.pixelSize: 11
                font.weight: Font.Medium
            }
        }
    }
}
