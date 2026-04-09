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
    property bool embedded: false
    readonly property color resolvedAccent: root.tone === "danger" ? theme.error : root.accent

    Theme { id: theme }

    implicitHeight: compact ? theme.controlHeightDense : theme.controlHeight
    implicitWidth: !showText && (iconName.length > 0 || iconText.length > 0)
                   ? implicitHeight
                   : Math.max(compact ? theme.controlHeightDense : 72, contentRow.implicitWidth + (compact ? theme.space14 : theme.space18))
    font.family: theme.fontUi
    font.pixelSize: compact ? 10 : 11
    hoverEnabled: true
    activeFocusOnTab: true
    leftPadding: root.embedded ? theme.space6 : (compact ? theme.space6 + 1 : theme.space8 + 1)
    rightPadding: root.embedded ? theme.space6 : (compact ? theme.space6 + 1 : theme.space8 + 1)
    topPadding: compact ? theme.space4 : theme.space4 + 1
    bottomPadding: compact ? theme.space4 : theme.space4 + 1

    ToolTip.visible: root.hovered && root.tooltipText.length > 0
    ToolTip.delay: 200
    ToolTip.timeout: 2500
    ToolTip.text: root.tooltipText

    background: Rectangle {
        radius: root.embedded ? theme.radius6 : theme.radiusControl
        border.width: root.visualFocus ? 2 : 1
        border.color: !root.enabled
                      ? theme.alpha(theme.lineSoft, 0.1)
                      : root.visualFocus
                        ? theme.alpha(theme.focusRing, 0.9)
                      : root.embedded
                        ? "transparent"
                      : root.tone === "primary"
                        ? theme.alpha(root.resolvedAccent, root.hovered ? 0.42 : 0.24)
                        : root.tone === "danger"
                          ? theme.alpha(root.resolvedAccent, root.hovered ? 0.34 : (root.active ? 0.26 : 0.18))
                        : root.active
                          ? theme.alpha(root.resolvedAccent, 0.22)
                          : (root.hovered ? theme.alpha(theme.lineStrong, 0.22) : "transparent")
        color: !root.enabled
               ? (root.embedded ? "transparent" : theme.alpha(theme.ink900, 0.58))
               : root.embedded
                 ? (root.down
                    ? theme.alpha(theme.ink700, 0.92)
                    : root.tone === "primary"
                      ? theme.alpha(root.resolvedAccent, root.hovered ? 0.92 : 0.84)
                      : root.active
                        ? theme.alpha(root.resolvedAccent, 0.18)
                        : (root.hovered ? theme.alpha(theme.ink800, 0.86) : "transparent"))
               : root.down
                 ? (root.tone === "primary"
                    ? theme.alpha(root.resolvedAccent, 0.82)
                    : (root.tone === "danger" ? theme.alpha(root.resolvedAccent, 0.26) : theme.ink750))
                 : root.tone === "primary"
                   ? (root.hovered ? theme.accentWarm400 : root.resolvedAccent)
                   : root.tone === "danger"
                     ? theme.alpha(root.resolvedAccent, root.active ? 0.2 : (root.hovered ? 0.14 : 0.08))
                   : root.active
                     ? theme.alpha(root.resolvedAccent, 0.16)
                     : (root.hovered ? theme.alpha(theme.ink800, 0.92) : theme.alpha(theme.ink850, 0.84))

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, parent.radius - 1)
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, root.embedded ? 0.32 : 0.26)
        }

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
            color: root.tone === "primary"
                   ? theme.ink950
                   : (root.tone === "danger" && root.active
                      ? root.resolvedAccent
                      : (root.enabled ? theme.textMain : theme.alpha(theme.textMuted, 0.62)))
            implicitWidth: root.compact ? 12 : 14
            implicitHeight: implicitWidth
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            visible: root.iconName.length === 0 && root.iconText.length > 0
            text: root.iconText
            color: root.tone === "primary"
                   ? theme.ink950
                   : (root.tone === "danger" && root.active
                      ? root.resolvedAccent
                      : (root.enabled ? theme.textMain : theme.alpha(theme.textMuted, 0.62)))
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
            color: root.tone === "primary"
                   ? theme.ink950
                   : (root.tone === "danger" && root.active
                      ? root.resolvedAccent
                      : (root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.68)))
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
