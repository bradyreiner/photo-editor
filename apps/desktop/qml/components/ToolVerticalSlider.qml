import QtQuick
import QtQuick.Controls.Basic
import "." as Components

Slider {
    id: root

    property color accent: theme.focusRing
    property int trackWidth: theme.scaled(2)
    property int handleSize: theme.scaled(10)
    property string accessibleLabel: ""
    property string tooltipText: ""

    Theme { id: theme }

    orientation: Qt.Vertical
    implicitWidth: theme.scaled(22)
    implicitHeight: theme.scaled(42)
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: root.accessibleLabel
    Accessible.role: Accessible.Slider

    HoverHandler {
        id: sliderHover
        onPointChanged: {
            if (sliderToolTip.parent)
                sliderToolTip.pointerPosition = root.mapToItem(sliderToolTip.parent,
                                                               point.position.x, point.position.y)
        }
    }

    Components.AppToolTip {
        id: sliderToolTip
        targetItem: root
        followPointer: true
        visible: sliderHover.hovered && (root.tooltipText.length > 0 || root.accessibleLabel.length > 0)
        text: root.tooltipText.length > 0 ? root.tooltipText : root.accessibleLabel
    }

    background: Item {
        x: root.leftPadding + (root.availableWidth - width) / 2
        y: root.topPadding
        width: root.trackWidth + theme.scaled(8)
        height: root.availableHeight

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: root.trackWidth
            height: parent.height
            radius: width / 2
            color: theme.alpha(theme.polar, root.enabled ? 0.22 : 0.10)
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * (1 - root.visualPosition)
            width: root.trackWidth
            height: parent.height * root.visualPosition
            radius: width / 2
            color: root.enabled ? root.accent : theme.textFaint
            opacity: root.pressed || root.hovered || root.visualFocus ? 0.95 : 0.74
        }
    }

    handle: Rectangle {
        x: root.leftPadding + (root.availableWidth - width) / 2
        y: root.topPadding + (1 - root.visualPosition) * (root.availableHeight - height)
        width: root.pressed || root.visualFocus ? root.handleSize + theme.scaled(2) : root.handleSize
        height: width
        radius: width / 2
        color: root.enabled ? theme.textStrong : theme.textFaint
        border.width: root.visualFocus ? theme.focusBorderWidth : theme.hairlineWidth
        border.color: root.visualFocus ? theme.focusRing : theme.ink950

        Rectangle {
            anchors.centerIn: parent
            width: parent.width + theme.scaled(8)
            height: width
            radius: width / 2
            color: theme.alpha(root.accent, root.pressed ? 0.18 : (root.hovered ? 0.12 : 0))
            z: -1
        }
    }
}
