import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root

    property string markerId: ""
    property string label: "Marker"
    property color accent: theme.markerAccent
    property real seconds: 0
    property real pixelsPerSecond: 1
    property real previewSeconds: seconds
    property real anchorPositionPixels: 0
    property bool dragging: false

    signal jumpRequested(string markerId)
    signal editRequested(string markerId)
    signal movePreviewRequested(string markerId, real proposedSeconds)
    signal moveCommittedRequested(string markerId, real proposedSeconds)

    Theme { id: theme }

    readonly property string timelineMarkerPinContract: "timeline-marker-pin:v2|line:true|label-fit:true|jump-edit:true|drag-preview-commit:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    width: Math.min(140, Math.max(44, markerLabel.implicitWidth + theme.space12))
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: "Timeline marker"

    Keys.onReturnPressed: root.jumpRequested(root.markerId)
    Keys.onEnterPressed: root.jumpRequested(root.markerId)
    Keys.onSpacePressed: root.jumpRequested(root.markerId)

    Rectangle {
        id: markerLine
        anchors.top: parent.top
        x: Math.max(0, Math.min(root.width - width, root.anchorPositionPixels - root.x))
        width: 2
        height: root.height
        color: root.accent
        opacity: markerMouse.containsMouse || root.activeFocus ? 0.92 : 0.70
    }

    Rectangle {
        id: markerLabelSurface
        anchors.top: parent.top
        anchors.topMargin: 2
        radius: theme.radius6
        color: root.accent
        width: root.width
        height: markerLabel.implicitHeight + theme.space6
        x: 0
        border.width: root.activeFocus ? theme.focusBorderWidth : 0
        border.color: theme.focusRing

        Text {
            id: markerLabel
            anchors.centerIn: parent
            width: parent.width - theme.space12
            text: root.label
            color: theme.ink950
            font.family: theme.fontMono
            font.pixelSize: theme.typeMicro
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 6
        }
    }

    MouseArea {
        id: markerMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real pressGlobalX: 0
        property real pressSeconds: 0
        property bool moved: false

        onPressed: function(mouse) {
            pressGlobalX = mapToGlobal(mouse.x, mouse.y).x
            pressSeconds = root.seconds
            root.previewSeconds = root.seconds
            moved = false
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            const deltaPixels = mapToGlobal(mouse.x, mouse.y).x - pressGlobalX
            if (Math.abs(deltaPixels) < 3 && !moved)
                return
            moved = true
            root.dragging = true
            const proposed = Math.max(0, pressSeconds + deltaPixels / Math.max(1, root.pixelsPerSecond))
            root.movePreviewRequested(root.markerId, proposed)
        }
        onReleased: {
            if (moved)
                root.moveCommittedRequested(root.markerId, root.previewSeconds)
            else
                root.jumpRequested(root.markerId)
            root.dragging = false
            moved = false
        }
        onCanceled: {
            root.previewSeconds = root.seconds
            root.dragging = false
            moved = false
        }
        onDoubleClicked: {
            if (!moved)
                root.editRequested(root.markerId)
        }
    }

    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && (markerMouse.containsMouse)
        delay: 350
        timeout: 3000
        text: root.label
    }
}
