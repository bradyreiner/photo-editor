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
    property bool compactPresentation: false
    property bool reviewNote: false
    property bool resolved: false
    property bool draggable: !reviewNote
    property real noteHeadSize: 22
    property real rangeStartPixels: anchorPositionPixels
    property real rangeEndPixels: anchorPositionPixels

    signal jumpRequested(string markerId)
    signal editRequested(string markerId)
    signal movePreviewRequested(string markerId, real proposedSeconds)
    signal moveCommittedRequested(string markerId, real proposedSeconds)

    Theme { id: theme }

    readonly property string timelineMarkerPinContract: "timeline-marker-pin:v3|line:true|label:contextual-fixed-size|input:head-only|jump-edit:true|drag-preview-commit:true|keyboard:true|tooltip-full:true|readable-text:true"
    readonly property string readableTextContract: "readable-text:v3|ellipsis:before-font-shrink|tooltip-full:true|accessibility-full:true"

    width: Math.min(140, Math.max(44, markerLabel.implicitWidth + theme.space12))
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: reviewNote ? (resolved ? "Resolved timeline note" : "Timeline note. Open to edit.") : "Timeline marker"
    Accessible.onPressAction: root.activate()

    function activate() {
        if (reviewNote) editRequested(markerId)
        else jumpRequested(markerId)
    }

    Keys.onReturnPressed: root.activate()
    Keys.onEnterPressed: root.activate()
    Keys.onSpacePressed: root.activate()

    Rectangle {
        id: markerLine
        anchors.top: parent.top
        x: Math.max(0, Math.min(root.width - width, root.anchorPositionPixels - root.x))
        width: 2
        height: root.height
        color: root.accent
        opacity: markerMouse.containsMouse || root.activeFocus || root.dragging ? 0.72 : root.reviewNote ? 0.08 : 0.22
    }

    Rectangle {
        id: noteRange
        objectName: "timelineElectricNoteRange"
        visible: root.reviewNote && root.rangeEndPixels > root.rangeStartPixels
        x: root.rangeStartPixels - root.x
        y: root.noteHeadSize / 2 - height / 2 + 1
        width: Math.max(0, root.rangeEndPixels - root.rangeStartPixels)
        height: 3
        radius: 1.5
        color: theme.alpha(root.accent, root.resolved ? 0.3 : 0.6)
    }

    Rectangle {
        id: noteHead
        objectName: "timelineElectricNoteHead"
        visible: root.reviewNote
        x: Math.max(0, Math.min(root.width - width, root.anchorPositionPixels - root.x - width / 2))
        y: 1
        width: root.noteHeadSize
        height: root.noteHeadSize
        radius: 7
        color: theme.alpha(root.accent, markerMouse.containsMouse ? 0.28 : 0.14)
        border.width: 1
        border.color: theme.alpha(root.accent, root.resolved ? 0.35 : 0.85)
        AppIcon {
            anchors.centerIn: parent
            width: 13; height: 13
            name: root.resolved ? "check" : "edit"
            color: root.accent
        }
    }

    Rectangle {
        id: markerLabelSurface
        visible: (!root.compactPresentation && !root.reviewNote) || markerMouse.containsMouse || root.activeFocus || root.dragging
        anchors.top: parent.top
        anchors.topMargin: root.reviewNote ? 26 : 2
        radius: theme.radius6
        color: theme.ink800
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
            color: theme.textMain
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 1
            elide: Text.ElideRight
            fontSizeMode: Text.FixedSize
        }
    }

    MouseArea {
        id: markerMouse
        // Only the marker head receives input; its guide must never intercept
        // clip selection or trimming throughout a full-height label-width strip.
        x: root.reviewNote ? noteHead.x : root.compactPresentation ? Math.max(0, root.anchorPositionPixels - root.x - 6) : 0
        y: 0
        width: root.reviewNote ? noteHead.width : root.compactPresentation ? 12 : root.width
        height: root.reviewNote ? noteHead.height : markerLabelSurface.height + theme.space4
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: root.draggable ? (root.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
        property real pressGlobalX: 0
        property real pressSeconds: 0
        property bool moved: false

        onPressed: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                root.editRequested(root.markerId)
                return
            }
            pressGlobalX = mapToGlobal(mouse.x, mouse.y).x
            pressSeconds = root.seconds
            root.previewSeconds = root.seconds
            moved = false
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !root.draggable)
                return
            const deltaPixels = mapToGlobal(mouse.x, mouse.y).x - pressGlobalX
            if (Math.abs(deltaPixels) < 3 && !moved)
                return
            moved = true
            root.dragging = true
            const proposed = Math.max(0, pressSeconds + deltaPixels / Math.max(1, root.pixelsPerSecond))
            root.movePreviewRequested(root.markerId, proposed)
        }
        onReleased: function(mouse) {
            if (mouse.button === Qt.RightButton) return
            if (moved)
                root.moveCommittedRequested(root.markerId, root.previewSeconds)
            else
                root.activate()
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
