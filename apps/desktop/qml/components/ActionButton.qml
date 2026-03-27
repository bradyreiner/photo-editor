import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    property string tone: "secondary"
    property bool active: false
    property color accent: "#c99a52"
    property string iconName: ""
    property string iconText: ""
    property string tooltipText: ""
    property bool compact: false
    property bool showText: true

    Theme { id: theme }

    implicitHeight: compact ? theme.controlHeightDense : theme.controlHeight
    implicitWidth: !showText && (iconName.length > 0 || iconText.length > 0)
                   ? implicitHeight
                   : Math.max(compact ? theme.controlHeightDense : 72, contentRow.implicitWidth + (compact ? theme.space14 : theme.space18))
    font.family: theme.fontUi
    font.pixelSize: compact ? 10 : 11
    hoverEnabled: true
    leftPadding: compact ? theme.space6 + 1 : theme.space8 + 1
    rightPadding: compact ? theme.space6 + 1 : theme.space8 + 1
    topPadding: compact ? theme.space4 : theme.space4 + 1
    bottomPadding: compact ? theme.space4 : theme.space4 + 1

    ToolTip.visible: root.hovered && root.tooltipText.length > 0
    ToolTip.delay: 200
    ToolTip.timeout: 2500
    ToolTip.text: root.tooltipText

    background: Rectangle {
        radius: theme.radiusControl
        border.width: 1
        border.color: !root.enabled
                      ? theme.lineSoft
                      : root.tone === "primary"
                        ? theme.alpha(root.accent, root.hovered ? 0.54 : 0.34)
                        : root.active
                          ? theme.alpha(root.accent, 0.4)
                          : (root.hovered ? theme.alpha(theme.lineStrong, 0.88) : theme.lineSoft)
        color: !root.enabled
               ? theme.ink900
               : root.down
                 ? (root.tone === "primary" ? theme.alpha(root.accent, 0.82) : theme.ink750)
                 : root.tone === "primary"
                   ? (root.hovered ? theme.accentWarm400 : root.accent)
                   : root.active
                     ? theme.alpha(root.accent, 0.16)
                     : (root.hovered ? theme.ink800 : theme.ink850)

        Behavior on color {
            ColorAnimation {
                duration: theme.durationFast
            }
        }

        Behavior on border.color {
            ColorAnimation {
                duration: theme.durationFast
            }
        }
    }

    contentItem: RowLayout {
        id: contentRow
        spacing: (root.iconName.length > 0 || root.iconText.length > 0) && root.showText ? theme.gapDense : 0

        AppIcon {
            visible: root.iconName.length > 0
            name: root.iconName
            color: root.tone === "primary" ? theme.ink950 : (root.enabled ? theme.textMain : theme.textFaint)
            implicitWidth: root.compact ? 12 : 14
            implicitHeight: implicitWidth
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            visible: root.iconName.length === 0 && root.iconText.length > 0
            text: root.iconText
            color: root.tone === "primary" ? theme.ink950 : (root.enabled ? theme.textMain : theme.textFaint)
            font.family: theme.fontMono
            font.pixelSize: root.compact ? 11 : 12
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            id: contentLabel
            visible: root.showText
            text: root.text
            color: root.tone === "primary" ? theme.ink950 : (root.enabled ? theme.textStrong : theme.textFaint)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: root.font.family
            font.pixelSize: root.font.pixelSize
            font.weight: Font.Medium
            font.letterSpacing: 0.15
            elide: Text.ElideRight
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
