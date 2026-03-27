pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    property color accent: "#c99a52"

    Theme { id: theme }

    implicitHeight: theme.controlHeight
    font.family: theme.fontUi
    font.pixelSize: 11
    hoverEnabled: true

    delegate: ItemDelegate {
        id: optionDelegate
        required property int index
        required property var modelData
        width: root.width
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            text: optionDelegate.modelData
            color: optionDelegate.highlighted ? theme.textStrong : theme.textMain
            font.family: root.font.family
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: theme.space12
            rightPadding: theme.space12
        }

        background: Rectangle {
            color: optionDelegate.highlighted ? theme.ink750 : theme.ink900
        }
    }

    indicator: AppIcon {
        name: "chevron-right"
        color: theme.textFaint
        rotation: 90
        width: 10
        height: 10
        anchors.right: parent.right
        anchors.rightMargin: theme.space12
        anchors.verticalCenter: parent.verticalCenter
    }

    contentItem: Text {
        text: root.displayText
        color: theme.textStrong
        leftPadding: theme.space12
        rightPadding: theme.space24 + theme.space4
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: theme.radiusPanel
        color: theme.ink900
        border.width: 1
        border.color: root.hovered ? theme.alpha(root.accent, 0.22) : theme.alpha(theme.lineSoft, 0.92)

        Behavior on border.color {
            ColorAnimation { duration: theme.durationFast }
        }
    }

    popup: Popup {
        y: root.height + theme.space4
        width: root.width
        padding: theme.space4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }

        background: Rectangle {
            radius: theme.radiusPanel
            color: theme.ink900
            border.width: 1
            border.color: theme.lineSoft
        }
    }
}
