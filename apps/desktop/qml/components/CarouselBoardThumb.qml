import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property int index
    required property var modelData
    readonly property bool selected: Boolean(modelData.selected)
    readonly property string slideNumber: String(modelData.slideNumber || index + 1)
    readonly property string slideLabel: String(modelData.label || "")
    readonly property string previewSource: String(modelData.previewSource || "")
    readonly property string carouselBoardThumbContract: "carousel-board-thumb:v1|preview:true|selection:true|keyboard:true|double-click:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string mediaSurfaceContract: "media-surface:v1|board-thumbnail:stable|preview-dominant:true|selection:single-border|badge-corner:bottom|chrome:quiet"

    signal selectedRequested(int index)
    signal duplicateRequested(int index)

    Theme { id: theme }

    width: 76
    radius: theme.radius8
    color: thumbHover.hovered || activeFocus
             ? theme.alpha(theme.ink850, 0.96)
             : theme.alpha(theme.ink900, 0.78)
    border.width: selected || activeFocus ? 2 : 1
    border.color: selected
                  ? theme.accentWarm400
                  : activeFocus
                    ? theme.alpha(theme.focusRing, 0.9)
                    : theme.alpha(theme.lineSoft, 0.38)
    clip: true
    activeFocusOnTab: true
    Accessible.name: "Board " + slideNumber + (slideLabel.length > 0 ? ", " + slideLabel : "")
    Accessible.description: selected ? "Selected carousel board" : "Carousel board"
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.selectedRequested(root.index)
    Keys.onEnterPressed: root.selectedRequested(root.index)
    Keys.onSpacePressed: root.selectedRequested(root.index)

    ToolTip.visible: thumbHover.hovered
    ToolTip.delay: 500
    ToolTip.timeout: 3600
    ToolTip.text: "Board " + slideNumber + (slideLabel.length > 0 ? " - " + slideLabel : "")

    HoverHandler {
        id: thumbHover
        cursorShape: Qt.PointingHandCursor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 4

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 50
            Layout.preferredHeight: 62
            radius: theme.radius6
            color: String(root.modelData.backgroundColor || "#fbf7f1")
            gradient: Gradient {
                orientation: String(root.modelData.backgroundKind || "solid") === "linear-horizontal"
                             ? Gradient.Horizontal : Gradient.Vertical
                GradientStop {
                    position: 0
                    color: String(root.modelData.backgroundColor || "#fbf7f1")
                }
                GradientStop {
                    position: 1
                    color: String(root.modelData.backgroundKind || "solid") === "solid"
                           ? String(root.modelData.backgroundColor || "#fbf7f1")
                           : String(root.modelData.backgroundSecondaryColor || "#ffffff")
                }
            }
            border.width: 1
            border.color: theme.alpha("#c5b6a6", 0.75)
            clip: true

            Image {
                anchors.fill: parent
                source: root.previewSource
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                smooth: true
                visible: root.previewSource.length > 0
            }

            Label {
                anchors.centerIn: parent
                visible: root.previewSource.length === 0
                text: root.slideNumber
                color: theme.alpha(theme.textFaint, 0.8)
                font.family: theme.fontMono
                font.pixelSize: 12
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 14
                color: theme.alpha(theme.ink950, 0.7)

                Label {
                    anchors.centerIn: parent
                    text: root.slideNumber
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 9
                }
            }
        }

        Label {
            id: boardLabel
            Layout.fillWidth: true
            text: root.slideLabel
            color: selected ? theme.textStrong : theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 9
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: function(eventPoint, button) {
            root.selectedRequested(root.index)
        }
        onDoubleTapped: function(eventPoint, button) {
            root.duplicateRequested(root.index)
        }
    }
}
