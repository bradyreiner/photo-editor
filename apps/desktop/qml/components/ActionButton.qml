import QtQuick
import QtQuick.Controls.Basic
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
    property string disabledFallbackReason: "Unavailable in the current state."
    property bool compact: false
    property bool showText: true
    property bool allowLabelWrap: false
    property bool embedded: false
    property bool square: !showText && (iconName.length > 0 || iconText.length > 0)
    property string role: square ? "icon" : tone
    property bool slashAccent: false
    property bool showFullTextTooltip: true
    property int minimumReadableWidth: 0
    property string shape: "auto" // auto, standard, clipped
    readonly property color resolvedAccent: root.tone === "danger" ? theme.error : root.accent
    readonly property bool primaryTone: root.tone === "primary"
    readonly property bool dangerTone: root.tone === "danger"
    readonly property int minimumPointerHeight: root.primaryTone || root.dangerTone
                                                ? theme.controlHeightPrimary
                                                : (root.compact
                                                   ? Math.max(theme.controlHeightDense, theme.scaled(32))
                                                   : Math.max(theme.controlHeight, theme.scaled(36)))
    readonly property bool clippedShape: root.shape === "clipped"
    readonly property bool accentEdgeVisible: root.enabled && root.clippedShape && root.slashAccent &&
                                             !root.embedded && (root.primaryTone || root.dangerTone || root.active)
    readonly property bool accentBorderVisible: false
    readonly property string resolvedTooltipText: !root.enabled
                                                  ? (root.disabledReason.length > 0
                                                     ? root.disabledReason
                                                     : root.disabledFallbackReason)
                                                  : root.tooltipText
    readonly property string actionButtonContract: "action-button:v8|shared-component:true|hierarchy:primary-accent-secondary-neutral-tertiary-quiet|flat-surface:true|accent-restrained:true|unified-shape:true|borderless-rest:true|hover-fill-not-border:true|keyboard:true|focus-visible:true|accessible-name-role-state:true|pointer-target:32-dense-44-primary|tooltip-full:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v3|ellipsis:none|primary-fit:true|optional-wrap:true|tooltip-full:true|minimum-px:10"
    readonly property string disabledReasonContract: "disabled-reason:v2|tooltip-when-disabled:true|accessible-description:true|fallback-tooltip:true|fallback-reason:true"
    readonly property string disabledStateMotionContract: "disabled-state-motion:v1|duration:110|opacity:true|layout-stable:true|tooltip-preserved:true|interruptible:true"
    readonly property string interactionStateContract: theme.interactionStateContract

    Theme { id: theme }

    implicitHeight: !showText && (iconName.length > 0 || iconText.length > 0)
                    ? minimumPointerHeight
                    : (allowLabelWrap
                       ? Math.max(minimumPointerHeight,
                                  contentRow.implicitHeight + topPadding + bottomPadding)
                       : minimumPointerHeight)
    implicitWidth: !showText && (iconName.length > 0 || iconText.length > 0)
                   ? implicitHeight
                   : Math.max(compact ? 64 : 70,
                              minimumReadableWidth,
                              contentRow.implicitWidth + (compact ? theme.space14 : theme.space18)
                              + (root.slashAccent && root.clippedShape && !root.embedded ? theme.space8 : 0))
    font.family: theme.fontUi
    font.pixelSize: compact ? theme.typeBody : theme.typeControl
    hoverEnabled: true
    activeFocusOnTab: true
    leftPadding: root.embedded ? theme.space6 : (compact ? theme.space6 + 1 : theme.space8 + 1)
    rightPadding: root.embedded ? theme.space6 : (compact ? theme.space6 + 1 : theme.space8 + 1)
    topPadding: compact ? theme.space4 : theme.space4 + 1
    bottomPadding: compact ? theme.space4 : theme.space4 + 1
    opacity: root.enabled ? 1.0 : 0.74

    Behavior on opacity {
        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
    }

    Accessible.name: root.text.length > 0 ? root.text : root.resolvedTooltipText
    Accessible.description: root.resolvedTooltipText
    Accessible.role: Accessible.Button
    Accessible.checked: root.checkable && root.checked
    ToolTip.visible: root.hovered && (root.resolvedTooltipText.length > 0 || (root.showFullTextTooltip && contentLabel.visible && (contentLabel.truncated || contentLabel.contentWidth > contentLabel.width + 1)))
    ToolTip.delay: 200
    ToolTip.timeout: 2500
    ToolTip.text: root.resolvedTooltipText.length > 0 ? root.resolvedTooltipText : root.text

    background: CommandButtonSurface {
        cornerRadius: theme.radiusControl
        fillColor: !root.enabled ? theme.alpha(theme.surfaceInset, 0.52)
                   : root.embedded ? (root.down || root.hovered ? theme.editorControlHover : "transparent")
                   : root.primaryTone ? (root.down ? theme.actionPrimaryPressed
                                         : root.hovered ? theme.actionPrimaryHover : theme.actionPrimaryFill)
                   : root.down ? theme.surfaceInset
                   : root.dangerTone ? theme.alpha(root.resolvedAccent, root.hovered ? 0.18 : 0.08)
                   : root.active ? (root.hovered ? theme.ink700 : theme.ink750)
                   : root.hovered ? theme.editorControlHover : "transparent"
        borderWidth: root.visualFocus ? theme.focusBorderWidth : (root.dangerTone ? 1 : 0)
        borderColor: root.visualFocus ? theme.focusRing
                     : root.dangerTone ? theme.alpha(theme.error, 0.45)
                     : root.primaryTone || root.active ? theme.alpha(root.resolvedAccent, 0.48)
                     : theme.controlEdge
        borderOpacity: root.enabled ? 1 : 0.5
        accentColor: root.resolvedAccent
        accentWidth: root.primaryTone || root.active ? 2 : 0
        accentOpacity: root.enabled ? (root.hovered || root.active ? 0.85 : 0.38) : 0
        topLineOpacity: root.hovered ? 0.12 : 0.06
    }

    contentItem: Item {
        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            width: Math.min(implicitWidth, Math.max(0, parent.width))
            height: implicitHeight
            spacing: (root.iconName.length > 0 || root.iconText.length > 0) && root.showText ? theme.gapDense : 0

            AppIcon {
                visible: root.iconName.length > 0
                name: root.iconName
                color: root.tone === "primary"
                       ? (!root.enabled ? theme.textMuted
                          : root.embedded ? theme.textStrong : theme.actionPrimaryText)
                       : (root.tone === "danger" && root.active
                          ? root.resolvedAccent
                          : (root.enabled ? theme.textMain : theme.alpha(theme.textMuted, 0.84)))
                Layout.preferredWidth: root.compact ? theme.scaled(14) : theme.scaled(16)
                Layout.preferredHeight: root.compact ? theme.scaled(14) : theme.scaled(16)
                Layout.alignment: Qt.AlignCenter
            }

            Text {
                visible: root.iconName.length === 0 && root.iconText.length > 0
                text: root.iconText
                color: root.tone === "primary"
                       ? (!root.enabled ? theme.textMuted
                          : root.embedded ? theme.textStrong : theme.actionPrimaryText)
                       : (root.tone === "danger" && root.active
                          ? root.resolvedAccent
                          : (root.enabled ? theme.textMain : theme.alpha(theme.textMuted, 0.84)))
                font.family: theme.fontMono
                font.pixelSize: root.compact ? theme.typeLabel : theme.typeControl
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
                       ? (!root.enabled ? theme.textMuted
                          : root.embedded ? theme.textStrong : theme.actionPrimaryText)
                       : (root.tone === "danger" && root.active
                          ? root.resolvedAccent
                          : (root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.88)))
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: root.font.family
                font.pixelSize: root.font.pixelSize
                font.weight: root.tone === "primary" || root.active ? Font.Bold : Font.Medium
                font.letterSpacing: 0
                wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                elide: Text.ElideNone
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 10
                maximumLineCount: root.allowLabelWrap ? 2 : 1
                Layout.alignment: Qt.AlignCenter
            }
        }
    }
}
