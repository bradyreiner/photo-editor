import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    property color accent: "#c99a52"

    Theme { id: theme }

    implicitHeight: 34
    font.family: theme.fontUi
    font.pixelSize: 11
    hoverEnabled: true

    delegate: ItemDelegate {
        width: root.width
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            text: modelData
            color: highlighted ? theme.textStrong : theme.textMain
            font.family: root.font.family
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: highlighted ? theme.ink750 : theme.ink900
        }
    }

    indicator: Text {
        text: "˅"
        color: theme.textFaint
        font.family: theme.fontMono
        font.pixelSize: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
    }

    contentItem: Text {
        text: root.displayText
        color: theme.textStrong
        leftPadding: 12
        rightPadding: 26
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: theme.radius8
        color: theme.ink900
        border.width: 1
        border.color: root.hovered ? theme.alpha(root.accent, 0.18) : theme.lineSoft
    }

    popup: Popup {
        y: root.height + 4
        width: root.width
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }

        background: Rectangle {
            radius: theme.radius8
            color: theme.ink900
            border.width: 1
            border.color: theme.lineSoft
        }
    }
}
