import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string markerId: ""
    property string title: "Marker"
    property string note: ""
    property string timecode: ""
    property string statusText: "todo"
    property string statusTone: "neutral"
    property bool completed: false
    property color accent: theme.markerAccent
    readonly property bool hasNote: note.length > 0
    readonly property string timelineMarkerCardContract: "timeline-marker-card:v1|title-wrap:true|note-wrap:true|status-chip:true|jump-edit-done:true|keyboard:true|readable-text:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|note-wrap:true|timecode-fit:true|tooltip-full:true"

    signal jumpRequested(string markerId)
    signal editRequested(string markerId)
    signal completionToggled(string markerId, bool completed)

    Theme { id: theme }

    implicitWidth: 254
    implicitHeight: markerColumn.implicitHeight + theme.space16
    radius: theme.radius8
    color: cardHover.hovered ? theme.alpha(accent, 0.16) : theme.alpha(accent, 0.10)
    border.width: activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
    border.color: activeFocus ? theme.alpha(theme.focusRing, 0.9) : theme.alpha(accent, cardHover.hovered ? 0.56 : 0.36)
    clip: true
    activeFocusOnTab: true
    Accessible.name: title
    Accessible.description: statusText + (timecode.length > 0 ? " at " + timecode : "")
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.jumpRequested(root.markerId)
    Keys.onEnterPressed: root.jumpRequested(root.markerId)
    Keys.onSpacePressed: root.jumpRequested(root.markerId)

    HoverHandler { id: cardHover }

    ColumnLayout {
        id: markerColumn
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space4

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space6

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                ToolTip.visible: titleHover.hovered && titleLabel.contentWidth > titleLabel.width + 1
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.title

                HoverHandler { id: titleHover }
            }

            StatusChip {
                text: root.statusText
                tone: root.statusTone
                compact: true
                tooltipText: "Marker status: " + root.statusText
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.hasNote ? root.note : "No note"
            color: root.hasNote ? theme.textMain : theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: theme.typeLabel
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space6

            Label {
                Layout.fillWidth: true
                text: root.timecode
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: theme.typeCaption
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 7
                elide: Text.ElideNone
            }

            ActionButton {
                text: "Jump"
                compact: true
                tooltipText: "Jump to this marker."
                onClicked: root.jumpRequested(root.markerId)
            }

            ActionButton {
                text: "Edit"
                compact: true
                tooltipText: "Edit this marker."
                onClicked: root.editRequested(root.markerId)
            }

            ActionButton {
                text: root.completed ? "Undo" : "Done"
                compact: true
                active: root.completed
                tooltipText: root.completed ? "Mark this marker incomplete." : "Mark this marker done."
                onClicked: root.completionToggled(root.markerId, !root.completed)
            }
        }
    }
}
