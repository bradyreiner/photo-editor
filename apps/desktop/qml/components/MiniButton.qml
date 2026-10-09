import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: root

    property bool active: false
    property color accent: theme.accentWarm400
    property color lineColor: theme.controlEdge
    property color hairline: theme.alpha(lineColor, 0.58)
    property string iconName: ""
    property color iconColor: "transparent"
    property string tooltip: ""
    property string disabledReason: ""
    property string disabledFallbackReason: "Unavailable in the current state."
    property string a11yName: ""
    property bool labelVisible: true
    property bool fitTextToWidth: true
    property bool allowLabelWrap: false
    property bool square: false
    property int minimumReadableWidth: 0
    property int maximumReadableWidth: theme.menuMaxWidth
    property string role: square ? "icon" : "secondary"
    readonly property bool primary: role === "primary"
    readonly property bool destructive: role === "destructive"
    readonly property bool ghost: role === "ghost"
    readonly property bool segmented: role === "segmented"
    readonly property bool iconOnly: role === "icon" || square
    readonly property bool visualIconOnly: iconOnly || (!labelVisible && iconName.length > 0)
    readonly property bool customIconColor: iconColor.a > 0
    readonly property bool commandShape: !iconOnly && !segmented && !ghost
    readonly property bool slashEdge: false
    readonly property bool accentEdgeVisible: root.enabled && root.slashEdge &&
                                             (root.primary || root.destructive || root.active)
    readonly property bool accentBorderVisible: false
    readonly property int commandCut: slashEdge ? 10 : 0
    readonly property real availableContentWidth: Math.max(0, width - theme.space12)
    readonly property real labelAvailableWidth: Math.max(0, availableContentWidth - (iconName.length > 0 && !visualIconOnly ? theme.scaled(20) : 0))
    readonly property bool contentFitsControl: !visible || buttonContent.width <= availableContentWidth + 1
    readonly property bool labelFitsControl: !visible || visualIconOnly || label.width <= labelAvailableWidth + 1
    readonly property bool labelOverflowing: label.visible && label.contentWidth > label.width + 1
    readonly property real readableImplicitWidth: label.implicitWidth + (iconName.length > 0 ? 42 : 24)
    readonly property string resolvedTooltipText: !root.enabled
                                                  ? (root.disabledReason.length > 0
                                                     ? root.disabledReason
                                                     : root.disabledFallbackReason)
                                                  : root.tooltip
    function humanizeAccessibleIdentifier(value) {
        return String(value || "")
            .replace(/((Button|Action|Control)+)$/i, "")
            .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
            .replace(/[_-]+/g, " ")
            .trim()
    }

    readonly property string miniButtonContract: "mini-button:v9|shared-component:true|active-state:true|roles:primary-secondary-segmented-ghost-icon-destructive|hierarchy:primary-graphite-secondary-neutral-tertiary-quiet|monochrome-default:true|neutral-primary:true|flat-surface:true|borderless-rest:true|centered-icons:true|hover-fill-not-border:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true|disabled-reason-keeps-action-name:true|density-token-heights:true"
    readonly property string readableTextContract: "readable-text:v3|ellipsis:none|compact-fit:true|optional-wrap:true|tooltip-full:true|a11y-full:true|minimum-px:10|readable-width:true"
    readonly property string disabledReasonContract: "disabled-reason:v2|tooltip-when-disabled:true|accessible-description:true|fallback-tooltip:true|fallback-reason:true"
    readonly property string disabledStateMotionContract: "disabled-state-motion:v1|duration:110|opacity:true|layout-stable:true|tooltip-preserved:true|interruptible:true"
    readonly property string toolbarButtonContract: "toolbar-button:v1|baseline-aligned:true|stable-height:true|role:" +
                                                    root.role +
                                                    "|icon-only:" +
                                                    String(root.visualIconOnly) +
                                                    "|segmented:" +
                                                    String(root.segmented) +
                                                    "|accent-default:false" +
                                                    "|subdued-chrome:true|hover-no-shift:true|focus-visible:true|media-subordinate:true"
    readonly property string toolbarGeometryContract: "toolbar-button-geometry:v1|height:" +
                                                      String(implicitHeight) +
                                                      "|icon-size:13|label-fit:true|minimum-readable-width:" +
                                                      String(minimumReadableWidth)
    readonly property string resolvedAccessibleName: {
        const explicitName = String(a11yName || "").trim()
        if (explicitName.length > 0)
            return explicitName
        const visibleText = String(text || "").trim()
        if (visibleText.length > 0)
            return visibleText
        const actionName = String(root.tooltip || "").trim()
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
    readonly property bool accessibilityReady: resolvedAccessibleName.length > 0

    Theme { id: theme }

    implicitHeight: primary
                    ? theme.controlHeightPrimary
                    : visualIconOnly
                    ? theme.iconButtonSizeCompact
                    : (allowLabelWrap
                       ? Math.max(theme.controlHeight, buttonContent.implicitHeight + theme.space8)
                       : theme.controlHeight)
    implicitWidth: visualIconOnly
                   ? theme.iconButtonSizeCompact
                   : Math.max(54, minimumReadableWidth,
                              Math.min(maximumReadableWidth, readableImplicitWidth))
    clip: true
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: root.enabled ? 1.0 : 0.74

    Behavior on opacity {
        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
    }

    Accessible.name: resolvedAccessibleName
    Accessible.description: !root.enabled
                             ? resolvedTooltipText
                             : (resolvedTooltipText !== resolvedAccessibleName
                                ? resolvedTooltipText : "")
    AppToolTip {
        id: appToolTip
        targetItem: root
        followPointer: true
        visible: root.hovered && (root.resolvedTooltipText.length > 0 || (!root.visualIconOnly && root.labelOverflowing))
        delay: 300
        timeout: 4200
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
        fillColor: theme.commandFill(root.destructive ? "danger" : root.primary ? "primary" : "secondary",
                                     root.active, root.hovered, root.down, root.enabled,
                                     root.ghost || root.segmented || root.visualIconOnly)
        borderWidth: theme.commandBorderWidth(root.visualFocus)
        borderColor: root.visualFocus ? theme.focusRing : theme.controlEdge
    }

    contentItem: Item {
        RowLayout {
            id: buttonContent
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.availableContentWidth)
            height: implicitHeight
            spacing: root.visualIconOnly ? 0 : theme.gapDense

            AppIcon {
                visible: root.iconName.length > 0
                name: root.iconName
                color: root.primary && !root.destructive && root.enabled ? theme.actionPrimaryText
                       : root.customIconColor ? root.iconColor
                       : root.destructive ? theme.error
                       : root.active ? theme.selectionAccent : theme.textMain
                Layout.preferredWidth: theme.scaled(14)
                Layout.preferredHeight: theme.scaled(14)
                Layout.alignment: Qt.AlignCenter
            }

            Label {
                id: label
                visible: !root.visualIconOnly
                text: root.text
                color: root.primary && !root.destructive && root.enabled ? theme.actionPrimaryText
                       : root.destructive ? theme.error : (root.active ? theme.textStrong : theme.textMain)
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: theme.weightControl
                font.letterSpacing: 0
                Layout.fillWidth: true
                Layout.maximumWidth: root.labelAvailableWidth
                wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                maximumLineCount: root.allowLabelWrap ? 2 : 1
                elide: Text.ElideNone
                fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
                minimumPixelSize: 10
                clip: !root.allowLabelWrap
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
