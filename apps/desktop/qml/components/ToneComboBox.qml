import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    property color accent: "#c99a52"

    implicitHeight: 34
    font.family: "Avenir Next"
    font.pixelSize: 11

    delegate: ItemDelegate {
        width: root.width
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            text: modelData
            color: highlighted ? "#f3f6f8" : "#d7dce1"
            font.family: root.font.family
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: highlighted ? "#20262c" : "#14191e"
        }
    }

    indicator: Text {
        text: "˅"
        color: "#8d97a1"
        font.family: "Menlo"
        font.pixelSize: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
    }

    contentItem: Text {
        text: root.displayText
        color: "#e5ebf0"
        leftPadding: 12
        rightPadding: 26
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 7
        color: "#14191e"
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
            radius: 8
            color: "#14191e"
        }
    }
}
