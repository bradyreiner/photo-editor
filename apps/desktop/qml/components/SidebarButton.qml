import QtQuick
import QtQuick.Controls

Button {
    id: root

    property color accent: "#c99a52"
    property string kind: "library"
    property string title: ""
    property string subtitle: ""
    property string tooltipText: ""
    property string badgeText: ""
    property bool badgeEmphasis: false
    property bool compact: false

    implicitHeight: compact ? 60 : 68
    font.family: "Avenir Next"
    hoverEnabled: true

    ToolTip.visible: root.compact && root.hovered && root.tooltipText.length > 0
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.tooltipText

    background: Rectangle {
        radius: 15
        color: root.down
               ? "#1d2329"
               : root.highlighted
                 ? "#212830"
                 : (root.hovered ? "#181e24" : "transparent")
        border.width: root.highlighted || root.hovered ? 1 : 0
        border.color: root.highlighted ? "#3a352a" : "#232b33"

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 10
            width: 4
            radius: 2
            visible: root.highlighted
            color: root.accent
        }

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }

    contentItem: Text {
        text: ""
    }

    Item {
        anchors.fill: parent

        Rectangle {
            id: iconChip
            width: 32
            height: 32
            radius: 10
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: root.compact ? undefined : parent.left
            anchors.leftMargin: 14
            anchors.horizontalCenter: root.compact ? parent.horizontalCenter : undefined
            color: root.highlighted
                   ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.16)
                   : (root.hovered ? "#20262d" : "#1a2026")
            border.width: root.highlighted ? 1 : 0
            border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.2)

            Item {
                anchors.centerIn: parent
                width: 14
                height: 14

                Repeater {
                    model: root.kind === "library" ? 4 : 0

                    delegate: Rectangle {
                        required property int index

                        width: 5
                        height: 5
                        radius: 1
                        color: root.highlighted ? root.accent : "#d7dde3"
                        x: (index % 2) * 7
                        y: Math.floor(index / 2) * 7
                    }
                }

                Repeater {
                    model: root.kind === "edit" ? 3 : 0

                    delegate: Rectangle {
                        required property int index

                        width: index === 1 ? 11 : 8
                        height: 2
                        radius: 1
                        color: root.highlighted ? root.accent : "#d7dde3"
                        x: index === 1 ? 0 : 3
                        y: index * 5 + 1
                    }
                }

                Rectangle {
                    visible: root.kind === "mask"
                    anchors.centerIn: parent
                    width: 11
                    height: 11
                    radius: 5
                    color: "transparent"
                    border.width: 1
                    border.color: root.highlighted ? root.accent : "#d7dde3"

                    Rectangle {
                        anchors.centerIn: parent
                        width: 5
                        height: 5
                        radius: 3
                        color: root.highlighted ? root.accent : "#d7dde3"
                    }
                }

                Item {
                    visible: root.kind === "export"
                    anchors.fill: parent

                    Rectangle {
                        width: 2
                        height: 10
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        radius: 1
                        color: root.highlighted ? root.accent : "#d7dde3"
                    }

                    Rectangle {
                        width: 9
                        height: 2
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        radius: 1
                        color: root.highlighted ? root.accent : "#d7dde3"
                    }

                    Rectangle {
                        width: 7
                        height: 2
                        radius: 1
                        rotation: 45
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 2
                        color: root.highlighted ? root.accent : "#d8d0c5"
                    }

                    Rectangle {
                        width: 7
                        height: 2
                        radius: 1
                        rotation: -45
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 2
                        color: root.highlighted ? root.accent : "#d8d0c5"
                    }
                }
            }
        }

        Column {
            visible: !root.compact
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: iconChip.right
            anchors.leftMargin: 12
            anchors.right: badgeBubble.left
            anchors.rightMargin: 12
            spacing: 3

            Text {
                text: root.title
                color: root.highlighted ? "#f2f4f6" : "#d9dee3"
                font.family: root.font.family
                font.pixelSize: 14
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: root.highlighted ? "#a6b0ba" : "#87909a"
                font.family: root.font.family
                font.pixelSize: 11
                elide: Text.ElideRight
                width: parent.width
            }
        }

        Rectangle {
            id: badgeBubble
            visible: !root.compact && root.badgeText.length > 0
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 14
            radius: 9
            color: root.highlighted || root.badgeEmphasis
                   ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, root.highlighted ? 0.18 : 0.12)
                   : "#1b2127"
            width: Math.max(34, badgeLabel.implicitWidth + 16)
            height: 24
            border.width: root.badgeEmphasis && !root.highlighted ? 1 : 0
            border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.16)

            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text: root.badgeText
                color: root.highlighted || root.badgeEmphasis ? "#edd4ae" : "#9ea7b1"
                font.family: "Menlo"
                font.pixelSize: 10
            }
        }
    }
}
