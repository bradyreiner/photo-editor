import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: root
    required property var theme
    property real horizontalZoom: 84
    property real minimumZoom: 0.01
    property real maximumZoom: 600
    property real laneScale: 1
    property string zoomLabel: "100%"
    property int noteCount: 0
    property bool embedded: false
    property bool followPlayhead: true
    property bool controlsReady: false
    readonly property bool compact: embedded || width < theme.scaled(680)
    signal horizontalScaleRequested(real zoom)
    signal verticalScaleRequested(real scale)
    signal fitRequested()
    signal addNoteRequested()
    signal notesRequested()
    signal followPlayheadRequested(bool enabled)
    Component.onCompleted: controlsReady = true

    spacing: embedded ? theme.space4 : theme.space8
    ToolButton {
        id: followPlayheadButton
        objectName: "timelineFollowPlayheadButton"
        implicitWidth: root.theme.controlHeightDense
        implicitHeight: root.theme.controlHeightDense
        checkable: true
        checked: root.followPlayhead
        Accessible.name: checked ? "Follow playhead on" : "Follow playhead off"
        Accessible.description: "Keep the playhead visible during playback. Shift+P reveals it immediately."
        contentItem: AppIcon {
            name: "focus-frame"
            color: root.followPlayhead ? root.theme.accentCool400 : root.theme.textMuted
        }
        onClicked: root.followPlayheadRequested(!root.followPlayhead)
        AppToolTip {
            targetItem: followPlayheadButton
            visible: followPlayheadButton.hovered
            text: root.followPlayhead ? "Follow playhead · On" : "Follow playhead · Off"
        }
    }
    MenuButton {
        objectName: "timelineDirectAddNoteButton"
        text: root.compact ? "Notes"
                            : "Notes" + (root.noteCount > 0 ? " · " + root.noteCount : "")
        iconName: "edit"
        accent: "#47b5ff"
        compact: true
        showText: !root.embedded || root.width >= root.theme.scaled(390)
        showMenuIndicator: !root.compact
        primaryOpensMenu: true
        accessibleName: "Timeline notes"
        tooltipText: "Add or review timeline notes. Right-click a clip or the timeline to attach a note there."
        menuItems: [
            {key: "add", label: "Add Note at Playhead", icon: "edit"},
            {key: "review", label: "Review Notes · " + root.noteCount, icon: "layers"}
        ]
        onMenuItemTriggered: function(key) {
            if (key === "add") root.addNoteRequested()
            else root.notesRequested()
        }
    }
    Item { visible: !root.embedded; Layout.fillWidth: true; Layout.minimumWidth: 0 }
    Label {
        text: root.compact ? "Time" : "Time · " + root.zoomLabel
        color: root.theme.textMuted
        font.pixelSize: root.theme.typeCaption
    }
    ToolValueSlider {
        objectName: "timelineHorizontalScaleSlider"
        Layout.preferredWidth: root.embedded ? 76 : root.compact ? 100 : 160
        Layout.minimumWidth: 64
        minimumReadableWidth: 64
        label: "Horizontal timeline scale"
        valueText: root.zoomLabel
        tooltipText: "Zoom time · Cmd/Ctrl + wheel · " + root.zoomLabel
        from: Math.log2(Math.max(1e-12, root.minimumZoom))
        to: Math.log2(Math.max(root.minimumZoom, root.maximumZoom))
        value: Math.log2(Math.max(root.minimumZoom, root.horizontalZoom))
        onValueChanged: {
            if (root.controlsReady && Math.abs(value - Math.log2(Math.max(root.minimumZoom, root.horizontalZoom))) > 0.000001)
                root.horizontalScaleRequested(Math.pow(2, value))
        }
    }
    ActionButton {
        objectName: "timelineScaleFitButton"
        text: "Fit"
        iconName: "fit"
        showText: !root.embedded
        compact: true
        tooltipText: "Fit the full sequence horizontally"
        onClicked: root.fitRequested()
    }
    Rectangle { width: 1; height: 16; color: root.theme.lineSoft }
    Label {
        text: root.compact ? "Lanes" : "Lanes · " + Math.round(root.laneScale * 100) + "%"
        color: root.theme.textMuted
        font.pixelSize: root.theme.typeCaption
    }
    ToolValueSlider {
        objectName: "timelineVerticalScaleSlider"
        Layout.preferredWidth: root.embedded ? 64 : root.compact ? 88 : 128
        Layout.minimumWidth: 64
        minimumReadableWidth: 64
        label: "Vertical lane scale"
        valueText: Math.round(root.laneScale * 100) + "%"
        tooltipText: "Resize lanes · Option/Alt + wheel · " + valueText
        from: 0.65
        to: 2.5
        value: root.laneScale
        onValueChanged: {
            if (root.controlsReady && Math.abs(value - root.laneScale) > 0.000001)
                root.verticalScaleRequested(value)
        }
    }
    ActionButton {
        objectName: "timelineScaleResetButton"
        visible: !root.embedded
        text: "Reset"
        compact: true
        tooltipText: "Reset lane scale to 100%"
        onClicked: root.verticalScaleRequested(1)
    }
}
