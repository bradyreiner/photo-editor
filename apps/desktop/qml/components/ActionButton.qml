import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    property string tone: "secondary"
    property bool active: false
    property color accent: theme.accentWarm400
    property string iconName: ""
    property string iconText: ""
    property string tooltipText: ""
    property bool compact: false
    property bool showText: true
    property bool embedded: false
    property bool slashAccent: true
    property string shape: "auto" // auto, standard, clipped
    readonly property color resolvedAccent: root.tone === "danger" ? theme.error : root.accent
    readonly property bool clippedShape: root.shape === "clipped" || (root.shape === "auto" && !root.embedded && (root.tone === "primary" || root.tone === "danger"))

    Theme { id: theme }

    implicitHeight: compact ? theme.controlHeightDense : theme.controlHeight
    implicitWidth: !showText && (iconName.length > 0 || iconText.length > 0)
                   ? implicitHeight
                   : Math.max(compact ? 64 : 70,
                              contentRow.implicitWidth + (compact ? theme.space14 : theme.space18)
                              + (root.slashAccent && root.clippedShape && !root.embedded ? theme.space8 : 0))
    font.family: theme.fontUi
    font.pixelSize: compact ? 11 : 12
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

    background: Item {
        Rectangle {
            anchors.fill: parent
            visible: !root.clippedShape
            radius: root.embedded ? theme.radius4 : theme.radiusControl
            color: !root.enabled
                   ? (root.embedded ? "transparent" : theme.alpha(theme.glassBase, 0.38))
                   : root.embedded
                     ? (root.down
                        ? theme.alpha(theme.ink700, 0.8)
                        : root.active
                          ? theme.alpha(root.resolvedAccent, 0.16)
                          : (root.hovered ? theme.alpha(theme.glassLift, 0.38) : "transparent"))
                   : root.down
                     ? (root.tone === "danger" ? theme.alpha(root.resolvedAccent, 0.18) : theme.alpha(theme.ink750, 0.64))
                     : root.tone === "danger"
                       ? theme.alpha(root.resolvedAccent, root.active ? 0.18 : (root.hovered ? 0.1 : 0.06))
                       : root.active
                         ? theme.alpha(root.resolvedAccent, 0.13)
                         : (root.hovered ? theme.alpha(theme.glassLift, 0.42) : theme.alpha(theme.glassBase, 0.42))
            border.width: root.visualFocus ? 2 : 1
            border.color: !root.enabled
                          ? theme.alpha(theme.lineSoft, 0.1)
                          : root.visualFocus
                            ? theme.alpha(theme.focusRing, 0.9)
                          : root.embedded
                            ? "transparent"
                          : root.tone === "danger"
                            ? theme.alpha(root.resolvedAccent, root.hovered ? 0.42 : (root.active ? 0.32 : 0.2))
                          : root.active
                            ? theme.alpha(root.resolvedAccent, 0.48)
                            : (root.hovered ? theme.alpha(theme.polar, 0.24) : theme.alpha(theme.glassEdge, 0.2))
        }

        CommandButtonSurface {
            anchors.fill: parent
            visible: root.clippedShape
            cut: root.embedded ? 4 : (root.compact ? 10 : 12)
            borderWidth: root.visualFocus ? 2 : 1
            borderColor: !root.enabled
                          ? theme.alpha(theme.lineSoft, 0.1)
                         : root.visualFocus
                           ? theme.alpha(theme.focusRing, 0.9)
                         : root.embedded
                           ? "transparent"
                          : root.tone === "primary"
                            ? theme.alpha(root.resolvedAccent, root.hovered ? 0.88 : 0.62)
                          : root.tone === "danger"
                            ? theme.alpha(root.resolvedAccent, root.hovered ? 0.78 : (root.active ? 0.62 : 0.38))
                          : root.active
                            ? theme.alpha(root.resolvedAccent, 0.62)
                            : (root.hovered ? theme.alpha(root.resolvedAccent, 0.46) : theme.alpha(root.resolvedAccent, 0.28))
            fillColor: !root.enabled
                        ? (root.embedded ? "transparent" : theme.alpha(theme.glassBase, 0.38))
                        : root.embedded
                         ? (root.down
                            ? theme.alpha(theme.ink700, 0.8)
                            : root.tone === "primary"
                              ? theme.alpha(root.resolvedAccent, root.hovered ? 0.2 : 0.14)
                              : root.active
                                ? theme.alpha(root.resolvedAccent, 0.16)
                                : (root.hovered ? theme.alpha(theme.glassLift, 0.38) : "transparent"))
                        : root.down
                          ? (root.tone === "danger"
                             ? theme.alpha(root.resolvedAccent, 0.24)
                             : theme.alpha(root.resolvedAccent, root.tone === "primary" ? 0.24 : 0.16))
                          : root.tone === "primary"
                            ? theme.alpha(root.resolvedAccent, root.hovered ? 0.22 : 0.14)
                            : root.tone === "danger"
                              ? theme.alpha(root.resolvedAccent, root.hovered || root.active ? 0.18 : 0.09)
                            : root.active
                              ? theme.alpha(root.resolvedAccent, 0.16)
                              : (root.hovered ? theme.alpha(root.resolvedAccent, 0.11) : theme.alpha(theme.ink950, 0.48))
            accentColor: root.resolvedAccent
            accentWidth: root.compact ? 4 : 5
            accentOpacity: !root.slashAccent || root.embedded ? 0 : (!root.enabled ? 0.24 : (root.tone === "primary" || root.active ? 1.0 : (root.hovered ? 0.82 : 0.58)))
            topLineOpacity: root.enabled && !root.embedded ? (root.hovered || root.active ? 0.28 : 0.12) : 0
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            height: 1
            color: theme.alpha(theme.polar, root.hovered || root.active ? 0.12 : 0.06)
            visible: root.enabled && !root.embedded
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 1
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, root.embedded ? 0.28 : 0.2)
        }

    }

    contentItem: RowLayout {
        id: contentRow
        spacing: (root.iconName.length > 0 || root.iconText.length > 0) && root.showText ? theme.gapDense : 0

        AppIcon {
            visible: root.iconName.length > 0
            name: root.iconName
            color: root.tone === "primary"
                   ? theme.textStrong
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
                   ? theme.textStrong
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
                   ? theme.textStrong
                   : (root.tone === "danger" && root.active
                      ? root.resolvedAccent
                      : (root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.68)))
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: root.font.family
            font.pixelSize: root.font.pixelSize
            font.weight: root.tone === "primary" || root.active ? Font.Bold : Font.Medium
            font.letterSpacing: 0
            elide: Text.ElideRight
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
