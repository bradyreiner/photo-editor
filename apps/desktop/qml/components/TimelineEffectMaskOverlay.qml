import QtQuick
import QtQuick.Shapes

Item {
    id: root

    required property var theme
    required property var mask
    property string effectName: "Blur"
    property string frameMode: "fit"
    property real sourceAspectRatio: 16 / 9
    property real frameScale: 1
    property real frameRotationDegrees: 0
    property real frameOffsetX: 0
    property real frameOffsetY: 0
    property real perspectiveX: 0
    property real perspectiveY: 0

    signal maskChangeRequested(var changes)
    signal editStarted()
    signal editFinished()

    readonly property real maskCenterX: Math.max(0, Math.min(1,
        Number(mask.centerX !== undefined ? mask.centerX : 0.5)))
    readonly property real maskCenterY: Math.max(0, Math.min(1,
        Number(mask.centerY !== undefined ? mask.centerY : 0.5)))
    readonly property real maskWidth: Math.max(0.05, Math.min(1, Number(mask.width || 0.65)))
    readonly property real maskHeight: Math.max(0.05, Math.min(1, Number(mask.height || 0.65)))
    readonly property real itemAspectRatio: width / Math.max(1, height)
    readonly property real boundedSourceAspectRatio: Math.max(0.01, Number(sourceAspectRatio || 16 / 9))
    readonly property real contentWidth: frameMode === "stretch"
                                          ? width
                                          : frameMode === "fill"
                                            ? (boundedSourceAspectRatio > itemAspectRatio
                                               ? height * boundedSourceAspectRatio : width)
                                            : (boundedSourceAspectRatio > itemAspectRatio
                                               ? width : height * boundedSourceAspectRatio)
    readonly property real contentHeight: frameMode === "stretch"
                                           ? height
                                           : contentWidth / boundedSourceAspectRatio
    readonly property real contentLeft: (width - contentWidth) / 2
    readonly property real contentTop: (height - contentHeight) / 2
    readonly property real maskPixelWidth: contentWidth * maskWidth
    readonly property real maskPixelHeight: contentHeight * maskHeight
    readonly property var resizeHandles: [
        {"name": "top-left", "horizontal": -1, "vertical": -1},
        {"name": "top-right", "horizontal": 1, "vertical": -1},
        {"name": "bottom-left", "horizontal": -1, "vertical": 1},
        {"name": "bottom-right", "horizontal": 1, "vertical": 1}
    ]

    anchors.centerIn: parent
    width: parent ? Math.max(1, parent.width - 18) : 1
    height: parent ? Math.max(1, parent.height - 18) : 1
    z: 9000
    transformOrigin: Item.Center
    scale: frameScale
    rotation: frameRotationDegrees
    anchors.horizontalCenterOffset: frameOffsetX * width * 0.25
    anchors.verticalCenterOffset: frameOffsetY * height * 0.25
    layer.enabled: Math.abs(perspectiveX) > 0.000001 || Math.abs(perspectiveY) > 0.000001
    layer.effect: TimelinePerspectiveEffect {
        itemWidth: root.width
        itemHeight: root.height
        perspectiveX: root.perspectiveX
        perspectiveY: root.perspectiveY
    }
    Accessible.name: effectName + " effect mask"
    Accessible.description: "Drag the mask to move it. Drag a corner handle to resize it."

    Item {
        id: contentFrame
        x: root.contentLeft
        y: root.contentTop
        width: root.contentWidth
        height: root.contentHeight
        activeFocusOnTab: true

        Item {
            id: maskOutline
            objectName: root.effectName === "Blur"
                        ? "timelineProgramBlurMaskOutline"
                        : "timelineProgramEffectMaskOutline-" + root.effectName
            x: root.maskCenterX * parent.width - width / 2
            y: root.maskCenterY * parent.height - height / 2
            width: root.maskPixelWidth
            height: root.maskPixelHeight

            Rectangle {
                anchors.fill: parent
                visible: String(root.mask.shape || "ellipse") === "rectangle"
                radius: root.theme.radius4
                color: root.theme.alpha(root.theme.accentCool400, 0.10)
                border.width: Math.max(1, root.theme.scaled(1.5))
                border.color: root.theme.accentCool300
            }

            Shape {
                anchors.fill: parent
                visible: String(root.mask.shape || "ellipse") !== "rectangle"
                antialiasing: true

                ShapePath {
                    readonly property real outlineWidth: Math.max(1, root.theme.scaled(1.5))
                    fillColor: root.theme.alpha(root.theme.accentCool400, 0.10)
                    strokeColor: root.theme.accentCool300
                    strokeWidth: outlineWidth
                    startX: maskOutline.width / 2
                    startY: outlineWidth / 2

                    PathAngleArc {
                        centerX: maskOutline.width / 2
                        centerY: maskOutline.height / 2
                        radiusX: Math.max(0, (maskOutline.width - parent.outlineWidth) / 2)
                        radiusY: Math.max(0, (maskOutline.height - parent.outlineWidth) / 2)
                        startAngle: -90
                        sweepAngle: 360
                    }
                }
            }

            Accessible.role: Accessible.Slider
            Accessible.name: root.effectName + " mask position"
            Accessible.description: "Center " + Math.round(root.maskCenterX * 100) +
                                    " percent horizontally and " +
                                    Math.round(root.maskCenterY * 100) + " percent vertically"

            Keys.onLeftPressed: root.maskChangeRequested({
                "centerX": Math.max(0, root.maskCenterX - 1 / Math.max(1, contentFrame.width))
            })
            Keys.onRightPressed: root.maskChangeRequested({
                "centerX": Math.min(1, root.maskCenterX + 1 / Math.max(1, contentFrame.width))
            })
            Keys.onUpPressed: root.maskChangeRequested({
                "centerY": Math.max(0, root.maskCenterY - 1 / Math.max(1, contentFrame.height))
            })
            Keys.onDownPressed: root.maskChangeRequested({
                "centerY": Math.min(1, root.maskCenterY + 1 / Math.max(1, contentFrame.height))
            })

            MouseArea {
                id: moveMaskMouse
                anchors.fill: parent
                anchors.margins: 5
                z: 1
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                hoverEnabled: true
                cursorShape: pressed ? Qt.ClosedHandCursor
                                     : containsMouse ? Qt.OpenHandCursor : Qt.ArrowCursor
                property point pointerOffset: Qt.point(0, 0)

                onPressed: function(mouse) {
                    const pointer = mapToItem(contentFrame, mouse.x, mouse.y)
                    pointerOffset = Qt.point(
                        pointer.x - root.maskCenterX * contentFrame.width,
                        pointer.y - root.maskCenterY * contentFrame.height)
                    root.editStarted()
                    maskOutline.forceActiveFocus()
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    const pointer = mapToItem(contentFrame, mouse.x, mouse.y)
                    root.maskChangeRequested({
                        "centerX": Math.max(0, Math.min(1,
                            (pointer.x - pointerOffset.x) / contentFrame.width)),
                        "centerY": Math.max(0, Math.min(1,
                            (pointer.y - pointerOffset.y) / contentFrame.height))
                    })
                }
                onReleased: root.editFinished()
                onCanceled: root.editFinished()
            }

            Repeater {
                model: root.resizeHandles

                delegate: Rectangle {
                    required property var modelData
                    readonly property real handleSize: Math.max(10, root.theme.scaled(12))
                    x: modelData.horizontal < 0 ? -handleSize / 2 : (parent ? parent.width : 0) - handleSize / 2
                    y: modelData.vertical < 0 ? -handleSize / 2 : (parent ? parent.height : 0) - handleSize / 2
                    width: handleSize
                    height: handleSize
                    radius: 2
                    color: root.theme.textStrong
                    border.width: 1
                    border.color: root.theme.accentCool300
                    z: 3
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Resize " + root.effectName + " mask " + String(modelData.name || "corner")
                    Accessible.description: "Drag to resize the mask from this corner"

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -5
                        acceptedButtons: Qt.LeftButton
                        preventStealing: true
                        hoverEnabled: true
                        cursorShape: modelData.horizontal === modelData.vertical
                                     ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                        property point pressPosition: Qt.point(0, 0)
                        property real startCenterX: 0.5
                        property real startCenterY: 0.5
                        property real startWidth: 0.65
                        property real startHeight: 0.65

                        onPressed: function(mouse) {
                            pressPosition = mapToItem(contentFrame, mouse.x, mouse.y)
                            startCenterX = root.maskCenterX
                            startCenterY = root.maskCenterY
                            startWidth = root.maskWidth
                            startHeight = root.maskHeight
                            root.editStarted()
                            parent.forceActiveFocus()
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed)
                                return
                            const pointer = mapToItem(contentFrame, mouse.x, mouse.y)
                            const deltaX = (pointer.x - pressPosition.x) / contentFrame.width
                            const deltaY = (pointer.y - pressPosition.y) / contentFrame.height
                            const nextWidth = Math.max(0.05, Math.min(1,
                                startWidth + deltaX * modelData.horizontal))
                            const nextHeight = Math.max(0.05, Math.min(1,
                                startHeight + deltaY * modelData.vertical))
                            root.maskChangeRequested({
                                "centerX": Math.max(0, Math.min(1,
                                    startCenterX + deltaX * 0.5)),
                                "centerY": Math.max(0, Math.min(1,
                                    startCenterY + deltaY * 0.5)),
                                "width": nextWidth,
                                "height": nextHeight
                            })
                        }
                        onReleased: root.editFinished()
                        onCanceled: root.editFinished()
                    }
                }
            }
        }
    }
}
