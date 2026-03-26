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

    implicitHeight: compact ? 50 : 64
    font.family: "Avenir Next"
    hoverEnabled: true

    ToolTip.visible: root.compact && root.hovered && root.tooltipText.length > 0
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.tooltipText

    background: Rectangle {
        radius: compact ? 14 : 15
        color: root.down
               ? "#1c232a"
               : root.highlighted
                 ? "#1a2128"
                 : (root.hovered ? "#151c22" : "transparent")
        border.width: root.highlighted ? 1 : 0
        border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.14)

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 6
            width: 3
            height: compact ? 18 : parent.height - 20
            radius: 2
            visible: root.highlighted
            color: root.accent
        }

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }

    contentItem: Item {
        anchors.fill: parent

        Item {
            id: iconGlyph
            width: 16
            height: 16
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: root.compact ? undefined : parent.left
            anchors.leftMargin: 18
            anchors.horizontalCenter: root.compact ? parent.horizontalCenter : undefined

            Repeater {
                model: root.kind === "library" ? 4 : 0

                delegate: Rectangle {
                    required property int index

                    width: 5
                    height: 5
                    radius: 1
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                    x: (index % 2) * 8
                    y: Math.floor(index / 2) * 8
                }
            }

            Repeater {
                model: root.kind === "edit" ? 3 : 0

                delegate: Rectangle {
                    required property int index

                    width: index === 1 ? 12 : 8
                    height: 2
                    radius: 1
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                    x: index === 1 ? 0 : 4
                    y: index * 6 + 1
                }
            }

            Item {
                visible: root.kind === "import"
                anchors.fill: parent

                Rectangle {
                    width: 12
                    height: 8
                    radius: 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    color: "transparent"
                    border.width: 1
                    border.color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                }

                Rectangle {
                    width: 2
                    height: 9
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 1
                    radius: 1
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                }

                Rectangle {
                    width: 7
                    height: 2
                    radius: 1
                    rotation: 45
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 4
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                }

                Rectangle {
                    width: 7
                    height: 2
                    radius: 1
                    rotation: -45
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 4
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                }
            }

            Rectangle {
                visible: root.kind === "mask"
                anchors.centerIn: parent
                width: 12
                height: 12
                radius: 6
                color: "transparent"
                border.width: 1
                border.color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")

                Rectangle {
                    anchors.centerIn: parent
                    width: 4
                    height: 4
                    radius: 2
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
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
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                }

                Rectangle {
                    width: 10
                    height: 2
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    radius: 1
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                }

                Rectangle {
                    width: 7
                    height: 2
                    radius: 1
                    rotation: 45
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 2
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                }

                Rectangle {
                    width: 7
                    height: 2
                    radius: 1
                    rotation: -45
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 2
                    color: root.highlighted ? root.accent : (root.hovered ? "#dbe2e8" : "#aab3bc")
                }
            }
        }

        Row {
            visible: !root.compact
            anchors.fill: parent
            anchors.leftMargin: 50
            anchors.rightMargin: 14
            spacing: 12

            Column {
                width: parent.width - (badgeBubble.visible ? badgeBubble.width + 12 : 0)
                anchors.verticalCenter: parent.verticalCenter
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
                visible: root.badgeText.length > 0
                anchors.verticalCenter: parent.verticalCenter
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
}
