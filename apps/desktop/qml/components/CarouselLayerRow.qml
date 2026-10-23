import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string itemId: ""
    property string label: "Layer"
    property string layerType: "media"
    property bool selected: false
    property bool primarySelected: false
    property bool hidden: false
    property bool locked: false
    property bool grouped: false
    property color accent: theme.accentCool400
    readonly property string carouselLayerRowContract: "carousel-layer-row:v2|zones:primary-edge-label-state-secondary-edge|selected:" +
                                                       String(selected) +
                                                       "|primary:" +
                                                       String(primarySelected) +
                                                       "|hidden:" +
                                                       String(hidden) +
                                                       "|locked:" +
                                                       String(locked) +
                                                       "|keyboard:true|multi-select:true|readable-text:true|primary-selection:left-edge|secondary-selection:right-edge"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|state-icons:true"
    readonly property string zoneContract: "carousel-layer-row-zones:v2|primary-edge|type-icon|label|hidden-state|locked-state|secondary-edge"
    readonly property string statePriorityContract: "layer-row-state-priority:v2|primary-selected>multi-selected-secondary>locked>hidden>hover"
    readonly property string interactionContract: "carousel-layer-row-interaction:v1|keyboard:true|multi-select:true|canvas-drag:true|state-icons:true|tooltip-full:true"
    readonly property string interactionStateContract: theme.interactionStateContract

    signal activated(int modifiers)

    Theme { id: theme }

    function iconForType(typeName) {
        const value = String(typeName || "")
        if (value === "text")
            return "edit"
        if (value === "shape")
            return "grid"
        if (value === "brush")
            return "edit"
        if (value === "video")
            return "source"
        return "media"
    }

    implicitHeight: 40
    radius: theme.radiusControl
    color: primarySelected
           ? theme.alpha(accent, 0.14)
           : selected
             ? theme.alpha(accent, 0.07)
           : rowTap.pressed
             ? theme.alpha(accent, 0.12)
           : hidden
             ? theme.alpha(theme.ink900, 0.40)
             : rowHover.hovered
               ? theme.alpha(theme.glassLift, 0.24)
               : "transparent"
    border.width: activeFocus ? 2 : (primarySelected ? 1 : 0)
    border.color: activeFocus ? theme.alpha(theme.focusRing, 0.9) : theme.alpha(accent, 0.62)
    clip: true
    activeFocusOnTab: true
    Accessible.name: label
    Accessible.description: (primarySelected ? "Primary selected. " : selected ? "Selected. " : "") +
                            (hidden ? "Hidden. " : "") + (locked ? "Locked. " : "Unlocked. ") +
                            "Alt Shift Up or Down reorders this layer without dragging."
    Accessible.role: Accessible.Button
    Accessible.checked: selected
    Keys.onReturnPressed: root.activated(0)
    Keys.onEnterPressed: root.activated(0)
    Keys.onSpacePressed: root.activated(0)

    HoverHandler { id: rowHover }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        visible: root.primarySelected
        color: root.accent
        opacity: 1.0
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        visible: root.selected && !root.primarySelected
        color: root.accent
        opacity: 0.62
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space6
        spacing: theme.space6

        AppIcon {
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            name: root.iconForType(root.layerType)
            color: root.hidden ? theme.textFaint : (root.selected ? root.accent : theme.textMuted)
        }

        Label {
            id: titleLabel
            Layout.fillWidth: true
            text: root.label
            color: root.hidden ? theme.textFaint : theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: 11
            font.weight: root.primarySelected ? Font.DemiBold : Font.Medium
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
            AppToolTip {
                id: sharedTooltip1
                targetItem: titleLabel
                visible: titleLabel.visible && (titleHover.hovered && titleLabel.contentWidth > titleLabel.width + 1)
                delay: 350
                timeout: 3600
                text: root.label
            }
            HoverHandler { id: titleHover }
        }

        Rectangle {
            id: popupTooltipAnchor2
            visible: root.grouped
            Layout.preferredWidth: 20
            Layout.preferredHeight: 18
            radius: theme.radius4
            color: theme.alpha(root.accent, 0.12)
            border.width: 1
            border.color: theme.alpha(root.accent, 0.28)

            Label {
                anchors.centerIn: parent
                text: "G"
                color: root.accent
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.Bold
            }

            AppToolTip {
                id: sharedTooltip3
                targetItem: popupTooltipAnchor2
                visible: popupTooltipAnchor2.visible && (groupHover.hovered)
                text: "Grouped layer"
                delay: 300
            }
            HoverHandler { id: groupHover }
        }

        AppIcon {
            visible: root.hidden
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            name: "hidden"
            color: theme.textFaint
        }

        Rectangle {
            visible: root.locked
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            radius: theme.radius4
            color: theme.alpha(theme.warning, 0.15)
            border.width: 1
            border.color: theme.alpha(theme.warning, 0.24)

            AppIcon {
                anchors.centerIn: parent
                width: 10
                height: 10
                name: "lock"
                color: theme.warning
            }
        }
    }

    MouseArea {
        id: rowTap
        anchors.fill: parent
        hoverEnabled: false
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            root.activated(mouse.modifiers)
        }
    }
}
