import QtQuick

Item {
    id: root

    property bool active: false
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property real transformScale: 1
    property real transformRotation: 0

    signal scaleEdited(real scale)
    signal rotationEdited(real rotation)
    signal interactionStarted()
    signal interactionFinished()

    visible: active && imageWidth > 1 && imageHeight > 1
    enabled: visible

    readonly property real centerX: imageX + imageWidth / 2
    readonly property real centerY: imageY + imageHeight / 2
    readonly property real boundedScale: Math.max(0.25, Math.min(4, transformScale))
    readonly property real frameWidth: imageWidth * boundedScale
    readonly property real frameHeight: imageHeight * boundedScale
    readonly property color guideColor: "#d8e0e7"

    function angleForPoint(point) {
        return Math.atan2(point.y - centerY, point.x - centerX) * 180 / Math.PI;
    }

    function distanceForPoint(point) {
        const dx = point.x - centerX;
        const dy = point.y - centerY;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    QtObject {
        id: dragState

        property real startScale: 1
        property real startDistance: 1
        property real startRotation: 0
        property real startAngle: 0
    }

    Item {
        id: frame
        x: centerX - frameWidth / 2
        y: centerY - frameHeight / 2
        width: frameWidth
        height: frameHeight
        rotation: root.transformRotation
        transformOrigin: Item.Center
        visible: root.visible

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: root.guideColor
            opacity: 0.82
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -42
            width: 1
            height: 34
            color: root.guideColor
            opacity: 0.72
        }

        Rectangle {
            id: rotationHandle
            anchors.horizontalCenter: parent.horizontalCenter
            y: -54
            width: 18
            height: 18
            radius: 9
            color: "#e8edf2"
            border.width: 1
            border.color: "#101419"

            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.CrossCursor
                onPressed: function(mouse) {
                    root.interactionStarted();
                    const point = rotationHandle.mapToItem(root, mouse.x, mouse.y);
                    dragState.startRotation = root.transformRotation;
                    dragState.startAngle = root.angleForPoint(point);
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return;
                    const point = rotationHandle.mapToItem(root, mouse.x, mouse.y);
                    const delta = root.angleForPoint(point) - dragState.startAngle;
                    root.rotationEdited(root.clamp(dragState.startRotation + delta, -45, 45));
                }
                onReleased: root.interactionFinished()
                onCanceled: root.interactionFinished()
            }
        }

        Rectangle {
            id: scaleHandle
            x: parent.width - width / 2
            y: parent.height - height / 2
            width: 18
            height: 18
            radius: 3
            color: "#e8edf2"
            border.width: 1
            border.color: "#101419"

            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.SizeFDiagCursor
                onPressed: function(mouse) {
                    root.interactionStarted();
                    const point = scaleHandle.mapToItem(root, mouse.x, mouse.y);
                    dragState.startScale = root.boundedScale;
                    dragState.startDistance = Math.max(1, root.distanceForPoint(point));
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return;
                    const point = scaleHandle.mapToItem(root, mouse.x, mouse.y);
                    const nextScale = dragState.startScale * root.distanceForPoint(point) / dragState.startDistance;
                    root.scaleEdited(root.clamp(nextScale, 0.25, 4));
                }
                onReleased: root.interactionFinished()
                onCanceled: root.interactionFinished()
            }
        }

        Rectangle {
            x: 8
            y: 8
            radius: 5
            color: "#96101419"
            border.width: 1
            border.color: "#2ef4f7fa"
            implicitWidth: transformLabel.implicitWidth + 14
            implicitHeight: transformLabel.implicitHeight + 8

            Text {
                id: transformLabel
                anchors.centerIn: parent
                text: root.transformScale.toFixed(2) + "x  " + root.transformRotation.toFixed(1) + " deg"
                color: "#f4f7fa"
                font.family: "Avenir Next"
                font.pixelSize: 10
                font.weight: Font.Medium
            }
        }
    }
}
