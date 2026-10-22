import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property int index
    required property var modelData
    objectName: "carouselBoardThumbnail" + String(index + 1)
    readonly property bool selected: Boolean(modelData.selected)
    readonly property bool activeBoard: Boolean(modelData.active)
    readonly property string slideNumber: String(modelData.slideNumber || index + 1)
    readonly property string slideLabel: String(modelData.label || "")
    readonly property string boardName: String(modelData.boardName || "")
    readonly property string boardRole: String(modelData.boardRole || "content")
    readonly property string boardRoleLabel: boardRole === "cta" ? "CTA"
                                                     : boardRole.charAt(0).toUpperCase() + boardRole.slice(1)
    readonly property string previewSource: String(modelData.previewSource || "")
    readonly property string carouselBoardThumbContract: "carousel-board-thumb:v2|preview:true|selection:true|active-edit-target:explicit|keyboard:true|double-click:true|readable-text:true|no-elide:true"
    readonly property string carouselBoardThumbPowerContract: "carousel-board-thumb-power:v1|preview:dominant|duplicate:visible-on-hover|selection:clear|label:fallback-board-number"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string mediaSurfaceContract: "media-surface:v1|board-thumbnail:stable|preview-dominant:true|selection:single-border|badge-corner:bottom|chrome:quiet"
    readonly property string interactionStateContract: theme.interactionStateContract
    property real dragOffsetX: 0
    property int dragOriginIndex: index
    property real reorderStep: width + 8
    property bool mediaDropHighlighted: false

    signal selectedRequested(int index, int modifiers)
    signal duplicateRequested(int index)
    signal reorderRequested(int fromIndex, int toIndex)
    signal reorderPreviewRequested(int fromIndex, int toIndex, bool active)
    signal mediaDropRequested(int boardIndex, var drop)

    Theme { id: theme }

    width: 104
    radius: theme.radius8
    color: reorderDrag.active
             ? theme.alpha(theme.accentWarm500, 0.16)
             : thumbHover.hovered || activeFocus
             ? theme.alpha(theme.ink850, 0.96)
             : theme.alpha(theme.ink900, 0.78)
    border.width: selected || activeFocus ? 2 : 1
    border.color: activeBoard
                  ? theme.accentWarm400
                  : selected
                    ? theme.accentCool400
                  : activeFocus
                    ? theme.alpha(theme.focusRing, 0.9)
                    : theme.alpha(theme.lineSoft, 0.38)
    clip: true
    z: reorderDrag.active ? 50 : 0
    transform: Translate { x: root.dragOffsetX }
    activeFocusOnTab: true
    Accessible.name: "Board " + slideNumber + ", " + boardRoleLabel
                     + (boardName.length > 0 ? ", " + boardName : "")
    Accessible.description: (activeBoard ? "Current editing target. " :
                             selected ? "Selected carousel board. " : "Carousel board. ") +
                            "Control Shift Left or Right reorders this board without dragging."
    Accessible.role: Accessible.Button
    Accessible.checked: selected
    Keys.onReturnPressed: root.selectedRequested(root.index, 0)
    Keys.onEnterPressed: root.selectedRequested(root.index, 0)
    Keys.onSpacePressed: root.selectedRequested(root.index, 0)

    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && (thumbHover.hovered)
        delay: 500
        timeout: 3600
        text: "Board " + root.slideNumber + " · " + root.boardRoleLabel
                  + (root.boardName.length > 0 ? " · " + root.boardName : "")
    }
    HoverHandler {
        id: thumbHover
        cursorShape: Qt.PointingHandCursor
    }

    DragHandler {
        id: reorderDrag
        target: null
        acceptedButtons: Qt.LeftButton
        xAxis.enabled: true
        yAxis.enabled: false
        grabPermissions: PointerHandler.CanTakeOverFromAnything | PointerHandler.ApprovesTakeOverByAnything
        onTranslationChanged: {
            if (active) {
                root.dragOffsetX = translation.x
                const targetIndex = Math.max(0, Math.round(root.dragOriginIndex + root.dragOffsetX / root.reorderStep))
                root.reorderPreviewRequested(root.dragOriginIndex, targetIndex, true)
            }
        }
        onActiveChanged: {
            if (active) {
                root.dragOriginIndex = root.index
                root.selectedRequested(root.index, 0)
                return
            }
            if (Math.abs(root.dragOffsetX) >= 24) {
                const targetIndex = Math.max(0, Math.round(root.dragOriginIndex + root.dragOffsetX / root.reorderStep))
                root.reorderRequested(root.dragOriginIndex, targetIndex)
            }
            root.reorderPreviewRequested(root.dragOriginIndex, root.dragOriginIndex, false)
            root.dragOffsetX = 0
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 4

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 40
            Layout.preferredHeight: 84
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

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    spacing: 3

                    Label {
                        text: root.slideNumber
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: 9
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: root.boardRoleLabel
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 6
                        elide: Text.ElideNone
                    }
                }
            }
        }

        Label {
            id: boardLabel
            objectName: "carouselBoardThumbLabel"
            Layout.fillWidth: true
            text: root.boardName.length > 0 ? root.boardName
                                            : root.slideLabel.length > 0 ? root.slideLabel
                                                                         : root.boardRoleLabel
            color: root.selected ? theme.textStrong : theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 9
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 7
        width: 48
        height: 16
        radius: 3
        visible: root.activeBoard
        z: 9
        color: theme.accentWarm400
        border.width: 1
        border.color: theme.alpha(theme.ink950, 0.32)

        Label {
            anchors.centerIn: parent
            text: "EDITING"
            color: theme.ink950
            font.family: theme.fontMono
            font.pixelSize: 7
            font.weight: Font.Bold
        }
    }

    Rectangle {
        id: popupTooltipAnchor2
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 7
        width: 24
        height: 24
        radius: 12
        visible: thumbHover.hovered || duplicateHover.hovered || root.activeFocus
        z: 10
        color: duplicateHover.hovered
               ? theme.alpha(theme.accentWarm500, 0.28)
               : theme.alpha(theme.ink950, 0.82)
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.42)
        Accessible.name: "Duplicate board " + root.slideNumber
        Accessible.role: Accessible.Button

        AppIcon {
            anchors.centerIn: parent
            width: 12
            height: 12
            name: "source"
            color: theme.textStrong
        }

        HoverHandler {
            id: duplicateHover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: root.duplicateRequested(root.index)
        }

        AppToolTip {
            id: sharedTooltip3
            targetItem: popupTooltipAnchor2
            visible: popupTooltipAnchor2.visible && (duplicateHover.hovered)
            text: "Duplicate board"
            delay: 300
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: function(mouse) { root.selectedRequested(root.index, mouse.modifiers) }
        onDoubleClicked: root.duplicateRequested(root.index)
    }

    DropArea {
        objectName: "carouselBoardThumbnailDropArea"
        anchors.fill: parent
        z: 80
        keys: ["application/x-carousel-media", "text/uri-list"]
        onEntered: function(drag) {
            root.mediaDropHighlighted = true
            drag.accepted = true
        }
        onExited: root.mediaDropHighlighted = false
        onDropped: function(drop) {
            root.mediaDropRequested(root.index, drop)
            root.mediaDropHighlighted = false
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 79
        visible: root.mediaDropHighlighted
        radius: root.radius
        color: theme.alpha(theme.accentCool400, 0.18)
        border.width: 3
        border.color: theme.accentCool400

        Label {
            anchors.centerIn: parent
            width: Math.max(56, parent.width - 12)
            text: "Replace primary media"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: 9
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
    }
}
