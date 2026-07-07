import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    property bool active: false
    property color accent: theme.accentWarm400
    property color lineColor: "#22292D"
    property color hairline: theme.alpha(lineColor, 0.58)
    property string iconName: ""
    property color iconColor: "transparent"
    property string tooltip: ""
    property string disabledReason: ""
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
    readonly property bool slashEdge: commandShape && (root.primary || root.destructive)
    readonly property bool accentEdgeVisible: root.enabled && root.slashEdge &&
                                             (root.primary || root.destructive || root.active)
    readonly property bool accentBorderVisible: false
    readonly property int commandCut: slashEdge ? 10 : 0
    readonly property real availableContentWidth: Math.max(0, width - 16)
    readonly property real labelAvailableWidth: Math.max(0, availableContentWidth - (iconName.length > 0 && !visualIconOnly ? 21 : 0))
    readonly property bool contentFitsControl: !visible || buttonContent.width <= availableContentWidth + 1
    readonly property bool labelFitsControl: !visible || visualIconOnly || label.width <= labelAvailableWidth + 1
    readonly property bool labelOverflowing: label.visible && label.contentWidth > label.width + 1
    readonly property real readableImplicitWidth: label.implicitWidth + (iconName.length > 0 ? 42 : 24)
    readonly property string resolvedTooltipText: !root.enabled && root.disabledReason.length > 0 ? root.disabledReason : root.tooltip
    readonly property string miniButtonContract: "mini-button:v4|shared-component:true|active-state:true|roles:primary-secondary-segmented-ghost-icon-destructive|hierarchy:primary-accent-secondary-neutral-tertiary-quiet|monochrome-default:true|mint-primary:true|slash-primary-only:true|rounded-secondary:true|centered-icons:true|hover-fill-not-border:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|compact-fit:true|optional-wrap:true|tooltip-full:true|a11y-full:true|minimum-px:9|readable-width:true"
    readonly property string disabledReasonContract: "disabled-reason:v1|tooltip-when-disabled:true|accessible-description:true|fallback-tooltip:true"
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
        const helpText = String(resolvedTooltipText || "").trim()
        if (helpText.length > 0)
            return helpText
        return String(iconName || "").trim()
    }
    readonly property bool accessibilityReady: resolvedAccessibleName.length > 0

    Theme { id: theme }

    implicitHeight: visualIconOnly
                    ? theme.iconButtonSizeCompact
                    : (allowLabelWrap
                       ? Math.max(theme.controlHeight, buttonContent.implicitHeight + 14)
                       : theme.controlHeight)
    implicitWidth: visualIconOnly
                   ? theme.iconButtonSizeCompact
                   : Math.max(62, minimumReadableWidth,
                              Math.min(maximumReadableWidth, readableImplicitWidth))
    clip: true
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: resolvedAccessibleName
    Accessible.description: resolvedTooltipText
    ToolTip.visible: hovered && (resolvedTooltipText.length > 0 || (!visualIconOnly && labelOverflowing))
    ToolTip.delay: 300
    ToolTip.timeout: 4200
    ToolTip.text: resolvedTooltipText.length > 0 ? resolvedTooltipText : text

    background: Item {
        anchors.fill: parent

        CommandButtonSurface {
            anchors.fill: parent
            visible: !root.ghost
            cut: root.commandCut
            cornerRadius: root.slashEdge ? 0 : (root.iconOnly ? Math.round(width / 2) : theme.radiusControl)
            borderWidth: root.visualFocus ? 2 : 0
            fillColor: {
                if (!root.enabled)
                    return "#141719"
                if (root.role === "primary")
                    return theme.alpha(root.accent, root.down ? 0.20 : (root.hovered || root.active ? 0.14 : 0.08))
                if (root.destructive)
                    return root.hovered || root.active ? theme.alpha(theme.error, 0.10) : theme.alpha(theme.glassBase, 0.54)
                if (root.active)
                    return root.iconOnly ? theme.alpha(theme.glassLift, 0.40) : theme.alpha(theme.glassLift, 0.34)
                if (root.down)
                    return "#171D20"
                return root.hovered ? theme.alpha(theme.glassLift, 0.44) : theme.alpha(theme.glassBase, 0.48)
            }
            borderColor: {
                if (!root.enabled)
                    return theme.alpha(root.lineColor, 0.32)
                if (root.visualFocus)
                    return theme.alpha(theme.focusRing, 0.88)
                if (root.destructive)
                    return theme.alpha(theme.error, root.hovered || root.active ? 0.48 : 0.28)
                if (root.primary)
                    return theme.alpha(root.accent, root.hovered || root.active ? 0.48 : 0.30)
                if (root.active)
                    return theme.alpha(theme.glassEdge, 0.42)
                return root.hovered ? theme.alpha(root.lineColor, 0.44) : root.hairline
            }
            borderOpacity: 1
            accentColor: root.destructive ? theme.error : root.accent
            accentWidth: root.slashEdge ? 4 : 0
            accentOpacity: root.accentEdgeVisible ? 1.0 : 0
            topLineOpacity: root.slashEdge && root.enabled ? (root.hovered || root.active ? 0.18 : 0.08) : 0
        }

        Rectangle {
            visible: root.segmented && root.active
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: root.accent
        }

        Rectangle {
            visible: root.ghost && (root.hovered || root.active)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.destructive ? theme.error : root.accent
        }

        CommandButtonSurface {
            visible: root.active && !root.ghost && root.slashEdge
            anchors.fill: parent
            cut: root.commandCut
            cornerRadius: root.slashEdge ? 0 : (root.iconOnly ? Math.round(width / 2) : theme.radiusControl)
            fillColor: "transparent"
            borderWidth: 0
            borderColor: root.destructive ? theme.alpha(theme.error, 0.34) : theme.alpha(root.accent, 0.34)
            accentColor: root.destructive ? theme.error : root.accent
            accentWidth: root.slashEdge ? 4 : 0
            accentOpacity: root.accentEdgeVisible ? 1.0 : 0
            topLineOpacity: 0
        }
    }

    contentItem: Item {
        RowLayout {
            id: buttonContent
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.availableContentWidth)
            height: implicitHeight
            spacing: root.visualIconOnly ? 0 : 8

            AppIcon {
                visible: root.iconName.length > 0
                name: root.iconName
                color: root.customIconColor ? root.iconColor : (root.destructive ? theme.error : (root.active && root.primary ? root.accent : theme.textMain))
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                Layout.alignment: Qt.AlignCenter
            }

            Label {
                id: label
                visible: !root.visualIconOnly
                text: root.text
                color: root.destructive ? theme.error : (root.active ? theme.textStrong : theme.textMain)
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: root.active || root.role === "primary" ? Font.DemiBold : Font.Medium
                font.letterSpacing: 0
                Layout.fillWidth: true
                Layout.maximumWidth: root.labelAvailableWidth
                wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                maximumLineCount: root.allowLabelWrap ? 2 : 1
                elide: Text.ElideNone
                fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
                minimumPixelSize: 9
                clip: !root.allowLabelWrap
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
