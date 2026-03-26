import QtQuick
import QtQuick.Controls

Button {
    id: root

    property string tone: "secondary"
    property bool active: false
    property color accent: "#c99a52"

    Theme { id: theme }

    implicitHeight: 32
    implicitWidth: Math.max(88, contentLabel.implicitWidth + 24)
    font.family: theme.fontUi
    font.pixelSize: 11
    hoverEnabled: true

    background: Rectangle {
        radius: theme.radius8
        border.width: 1
        border.color: !root.enabled
                      ? theme.lineSoft
                      : root.tone === "primary"
                        ? theme.alpha(root.accent, root.hovered ? 0.42 : 0.28)
                        : root.active
                          ? theme.alpha(root.accent, 0.34)
                          : (root.hovered ? theme.lineStrong : theme.lineSoft)
        color: !root.enabled
               ? theme.ink900
               : root.down
                 ? (root.tone === "primary" ? theme.alpha(root.accent, 0.82) : theme.ink750)
                 : root.tone === "primary"
                   ? (root.hovered ? theme.accentWarm400 : root.accent)
                   : root.active
                     ? theme.alpha(root.accent, 0.14)
                     : (root.hovered ? theme.ink800 : theme.ink850)

        Behavior on color {
            ColorAnimation {
                duration: theme.durationFast
            }
        }
    }

    contentItem: Text {
        id: contentLabel
        text: root.text
        color: root.tone === "primary" ? theme.ink950 : (root.enabled ? theme.textStrong : theme.textFaint)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        font.weight: Font.Medium
        font.letterSpacing: 0.2
        elide: Text.ElideRight
    }
}
