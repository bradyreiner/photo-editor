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
    property string accessibleName: ""
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
    readonly property bool quietTone: root.embedded || root.tone === "quiet" || root.tone === "tertiary"
    readonly property int minimumPointerHeight: root.primaryTone || root.dangerTone
                                                ? theme.controlHeightPrimary
                                                : (root.compact
                                                   ? theme.controlHeightDense
                                                   : theme.controlHeight)
    readonly property bool clippedShape: root.shape === "clipped"
    readonly property bool accentEdgeVisible: root.enabled && root.clippedShape && root.slashAccent &&
                                             !root.embedded && (root.primaryTone || root.dangerTone || root.active)
    readonly property bool accentBorderVisible: false
    readonly property string resolvedTooltipText: !root.enabled
                                                  ? (root.disabledReason.length > 0
                                                     ? root.disabledReason
                                                     : root.disabledFallbackReason)
                                                  : root.tooltipText
    function humanizeAccessibleIdentifier(value) {
        return String(value || "")
            .replace(/((Button|Action|Control)+)$/i, "")
            .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
            .replace(/[_-]+/g, " ")
            .trim()
    }

    readonly property string resolvedAccessibleName: {
        const explicitName = String(root.accessibleName || "").trim()
        if (explicitName.length > 0)
            return explicitName
        const visibleText = String(root.text || "").trim()
        if (visibleText.length > 0)
            return visibleText
        const actionName = String(root.tooltipText || "").trim()
        if (actionName.length > 0)
            return actionName
        const objectIdentifier = humanizeAccessibleIdentifier(root.objectName)
        const identifier = objectIdentifier.length > 0
                ? objectIdentifier
                : humanizeAccessibleIdentifier(root.iconName)
        return identifier.length > 0
                ? identifier.charAt(0).toUpperCase() + identifier.slice(1)
                : "Action"
    }
    readonly property string actionButtonContract: "action-button:v11|shared-component:true|hierarchy:primary-graphite-secondary-neutral-tertiary-quiet|flat-surface:true|accent-restrained:true|unified-shape:true|borderless-rest:true|hover-fill-not-border:true|keyboard:true|focus-visible:true|accessible-name-role-state:true|density-token-heights:true|tooltip-full:true|readable-text:true|no-elide:true|explicit-name-override:true|disabled-reason-keeps-action-name:true"
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
                   : Math.max(compact ? 54 : 58,
                              minimumReadableWidth,
                              contentRow.implicitWidth + leftPadding + rightPadding
                              + (root.slashAccent && root.clippedShape && !root.embedded ? theme.space8 : 0))
    font.family: theme.fontUi
    font.pixelSize: compact ? theme.typeBody : theme.typeControl
    font.weight: theme.weightControl
    hoverEnabled: true
    activeFocusOnTab: true
    leftPadding: root.embedded ? theme.space6 : (compact ? theme.space6 : theme.space8)
    rightPadding: root.embedded ? theme.space6 : (compact ? theme.space6 : theme.space8)
    topPadding: compact ? theme.space4 : theme.space4 + 1
    bottomPadding: compact ? theme.space4 : theme.space4 + 1
    opacity: root.enabled ? 1.0 : 0.74

    Behavior on opacity {
        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
    }

    Accessible.name: root.resolvedAccessibleName
    Accessible.description: !root.enabled
                             ? root.resolvedTooltipText
                             : (root.resolvedTooltipText !== root.resolvedAccessibleName
                                ? root.resolvedTooltipText : "")
    Accessible.role: Accessible.Button
    Accessible.checked: root.checkable && root.checked
    AppToolTip {
        id: appToolTip
        targetItem: root
        followPointer: true
        visible: root.hovered && (root.resolvedTooltipText.length > 0 || (root.showFullTextTooltip && contentLabel.visible && (contentLabel.truncated || contentLabel.contentWidth > contentLabel.width + 1)))
        delay: 200
        timeout: 2500
        text: root.resolvedTooltipText.length > 0 ? root.resolvedTooltipText : root.text
    }

    HoverHandler {
        onPointChanged: {
            if (appToolTip.parent)
                appToolTip.pointerPosition = root.mapToItem(appToolTip.parent,
                                                            point.position.x, point.position.y)
        }
    }

    background: CommandButtonSurface {
        cornerRadius: theme.radiusControl
        hovered: root.hovered
        pressed: root.down
        selected: root.active
        interactiveEnabled: root.enabled
        fillColor: theme.commandFill(root.tone, root.active, root.hovered, root.down, root.enabled, root.quietTone)
        borderWidth: theme.commandBorderWidth(root.visualFocus)
        borderColor: root.visualFocus ? theme.focusRing : theme.controlEdge
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
                       : (root.tone === "danger" && root.enabled
                          ? root.resolvedAccent
                          : (root.enabled ? (root.active ? root.accent : theme.textMain) : theme.alpha(theme.textMuted, 0.84)))
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
                       : (root.tone === "danger" && root.enabled
                          ? root.resolvedAccent
                          : (root.enabled ? (root.active ? root.accent : theme.textMain) : theme.alpha(theme.textMuted, 0.84)))
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
                       : (root.tone === "danger" && root.enabled
                          ? root.resolvedAccent
                          : (root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.88)))
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: root.font.family
                font.pixelSize: root.font.pixelSize
                font.weight: root.font.weight
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
