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
    property string tooltip: tooltipText
    property string disabledReason: ""
    property bool compact: false
    property bool showText: true
    property bool allowLabelWrap: false
    property bool embedded: false
    property bool square: !showText && (iconName.length > 0 || iconText.length > 0)
    property string role: square ? "icon" : tone
    property bool slashAccent: true
    property bool showFullTextTooltip: true
    property int minimumReadableWidth: 0
    property string shape: "auto" // auto, standard, clipped
    readonly property color resolvedAccent: root.tone === "danger" ? theme.error : root.accent
    readonly property bool primaryTone: root.tone === "primary"
    readonly property bool dangerTone: root.tone === "danger"
    readonly property bool clippedShape: root.shape === "clipped" ||
                                         (root.shape === "auto" && !root.embedded &&
                                          (root.primaryTone || root.dangerTone))
    readonly property bool accentEdgeVisible: root.enabled && root.clippedShape && root.slashAccent &&
                                             !root.embedded && (root.primaryTone || root.dangerTone || root.active)
    readonly property bool accentBorderVisible: false
    readonly property string resolvedTooltipText: !root.enabled && root.disabledReason.length > 0 ? root.disabledReason : root.tooltipText
    readonly property string actionButtonContract: "action-button:v4|shared-component:true|hierarchy:primary-accent-secondary-neutral-tertiary-quiet|monochrome-default:true|mint-primary:true|slash-primary-only:true|rounded-secondary:true|centered-icons:true|hover-fill-not-border:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|primary-fit:true|optional-wrap:true|tooltip-full:true|minimum-px:9"
    readonly property string disabledReasonContract: "disabled-reason:v1|tooltip-when-disabled:true|accessible-description:true|fallback-tooltip:true"

    Theme { id: theme }

    implicitHeight: !showText && (iconName.length > 0 || iconText.length > 0)
                    ? (compact ? theme.controlHeightDense : theme.controlHeight)
                    : (allowLabelWrap
                       ? Math.max(compact ? theme.controlHeightDense : theme.controlHeight,
                                  contentRow.implicitHeight + topPadding + bottomPadding)
                       : (compact ? theme.controlHeightDense : theme.controlHeight))
    implicitWidth: !showText && (iconName.length > 0 || iconText.length > 0)
                   ? implicitHeight
                   : Math.max(compact ? 64 : 70,
                              minimumReadableWidth,
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

    Accessible.description: root.resolvedTooltipText
    ToolTip.visible: root.hovered && (root.resolvedTooltipText.length > 0 || (root.showFullTextTooltip && contentLabel.visible && (contentLabel.truncated || contentLabel.contentWidth > contentLabel.width + 1)))
    ToolTip.delay: 200
    ToolTip.timeout: 2500
    ToolTip.text: root.resolvedTooltipText.length > 0 ? root.resolvedTooltipText : root.text

    background: Item {
        Rectangle {
            anchors.fill: parent
            visible: !root.clippedShape
            radius: root.square ? Math.round(Math.min(width, height) / 2) : (root.embedded ? theme.radius4 : theme.radiusControl)
            color: !root.enabled
                   ? (root.embedded ? "transparent" : theme.alpha(theme.glassBase, 0.38))
                   : root.embedded
                     ? (root.down
                        ? theme.alpha(theme.ink700, 0.8)
                        : root.active
                          ? theme.alpha(root.resolvedAccent, 0.16)
                          : (root.hovered ? theme.alpha(theme.glassLift, 0.38) : "transparent"))
                   : root.down
                     ? (root.dangerTone ? theme.alpha(root.resolvedAccent, 0.18) : theme.alpha(theme.ink750, 0.64))
                   : root.dangerTone
                       ? theme.alpha(root.resolvedAccent, root.active ? 0.18 : (root.hovered ? 0.1 : 0.06))
                   : root.active
                         ? theme.alpha(theme.glassLift, 0.34)
                         : (root.hovered ? theme.alpha(theme.glassLift, 0.42) : theme.alpha(theme.glassBase, 0.42))
            border.width: root.visualFocus ? 2 : 0
            border.color: !root.enabled
                          ? theme.alpha(theme.lineSoft, 0.1)
                          : root.visualFocus
                            ? theme.alpha(theme.focusRing, 0.9)
                          : root.embedded
                            ? "transparent"
                          : root.dangerTone
                            ? theme.alpha(root.resolvedAccent, root.hovered ? 0.42 : (root.active ? 0.32 : 0.2))
                          : root.active
                            ? theme.alpha(theme.glassEdge, 0.36)
                            : (root.hovered ? theme.alpha(theme.glassEdge, 0.30) : theme.alpha(theme.glassEdge, 0.16))
        }

        CommandButtonSurface {
            anchors.fill: parent
            visible: root.clippedShape
            cut: root.embedded ? 4 : (root.compact ? 10 : 12)
            cornerRadius: root.shape === "clipped" ||
                          (root.shape === "auto" && !root.embedded && (root.primaryTone || root.dangerTone))
                          ? 0
                          : (root.square ? Math.round(Math.min(width, height) / 2) : theme.radiusControl)
            borderWidth: root.visualFocus ? 2 : 0
            borderColor: !root.enabled
                          ? theme.alpha(theme.lineSoft, 0.1)
                         : root.visualFocus
                           ? theme.alpha(theme.focusRing, 0.9)
                         : root.embedded
                           ? "transparent"
                          : root.primaryTone
                            ? theme.alpha(root.resolvedAccent, root.hovered ? 0.70 : 0.52)
                          : root.dangerTone
                            ? theme.alpha(root.resolvedAccent, root.hovered ? 0.64 : (root.active ? 0.54 : 0.34))
                          : root.active
                            ? theme.alpha(root.resolvedAccent, root.accentEdgeVisible ? 0.28 : 0.42)
                            : (root.hovered ? theme.alpha(theme.glassEdge, 0.38) : theme.alpha(theme.glassEdge, 0.22))
            fillColor: !root.enabled
                        ? (root.embedded ? "transparent" : theme.alpha(theme.glassBase, 0.38))
                        : root.embedded
                         ? (root.down
                            ? theme.alpha(theme.ink700, 0.8)
                              : root.primaryTone
                              ? theme.alpha(root.resolvedAccent, root.hovered ? 0.2 : 0.14)
                              : root.active
                                ? theme.alpha(root.resolvedAccent, 0.16)
                                : (root.hovered ? theme.alpha(theme.glassLift, 0.38) : "transparent"))
                        : root.down
                          ? (root.dangerTone
                             ? theme.alpha(root.resolvedAccent, 0.24)
                             : (root.primaryTone
                                ? theme.alpha(root.resolvedAccent, 0.24)
                                : theme.alpha(theme.ink750, 0.64)))
                          : root.primaryTone
                            ? theme.alpha(root.resolvedAccent, root.hovered ? 0.22 : 0.14)
                            : root.dangerTone
                              ? theme.alpha(root.resolvedAccent, root.hovered || root.active ? 0.18 : 0.09)
                            : root.active
                              ? theme.alpha(theme.glassLift, 0.34)
                              : (root.hovered ? theme.alpha(theme.glassLift, 0.38) : theme.alpha(theme.ink950, 0.48))
            accentColor: root.resolvedAccent
            accentWidth: root.compact ? 3 : 4
            accentOpacity: root.accentEdgeVisible ? 1.0 : 0
            topLineOpacity: root.enabled && !root.embedded ? (root.hovered || root.active ? 0.28 : 0.12) : 0
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            height: 1
            color: theme.alpha(theme.glassEdge, root.hovered || root.active ? 0.18 : 0.08)
            visible: root.enabled && !root.embedded
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: root.square ? Math.round(Math.min(width, height) / 2) : Math.max(1, theme.radiusControl - 1)
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, root.embedded ? 0.28 : 0.2)
        }

    }

    contentItem: Item {
        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            width: Math.min(implicitWidth, Math.max(0, parent.width - root.leftPadding - root.rightPadding))
            height: implicitHeight
            spacing: (root.iconName.length > 0 || root.iconText.length > 0) && root.showText ? theme.gapDense : 0

            AppIcon {
                visible: root.iconName.length > 0
                name: root.iconName
                color: root.tone === "primary"
                       ? theme.textStrong
                       : (root.tone === "danger" && root.active
                          ? root.resolvedAccent
                          : (root.enabled ? theme.textMain : theme.alpha(theme.textMuted, 0.62)))
                Layout.preferredWidth: root.compact ? 13 : 15
                Layout.preferredHeight: root.compact ? 13 : 15
                Layout.alignment: Qt.AlignCenter
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
                Layout.alignment: Qt.AlignCenter
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
                wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                elide: Text.ElideNone
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 9
                maximumLineCount: root.allowLabelWrap ? 2 : 1
                Layout.alignment: Qt.AlignCenter
            }
        }
    }
}
