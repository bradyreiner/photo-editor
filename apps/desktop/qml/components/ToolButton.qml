import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: root

    property string label: text
    property string iconName: ""
    property string iconText: ""
    property string shortcut: ""
    property string variantLabel: ""
    property string statusText: ""
    property string disabledReason: ""
    property string disabledFallbackReason: "Unavailable in the current state."
    property color accent: theme.accentWarm400
    property bool active: false
    property bool pending: false
    property bool warning: false
    property bool commandEnabled: true
    property bool compact: true
    property bool showLabel: false
    property bool showMenuIndicator: false
    property int toolIconSize: compact ? 14 : 16

    signal menuRequested()

    Theme { id: theme }

    readonly property color stateColor: warning ? theme.warning
                                       : pending ? theme.accentCool400
                                       : active ? accent
                                       : theme.textMuted
    readonly property string toolDiscoveryContract: "tool-button-discovery:v3|keyboard:true|focus-visible:true|accessible-name-role-state:true|pointer-target:32-dense-44-primary|family-label:" +
                                                    String(label || text) +
                                                    "|variant-visible:" +
                                                    String(variantLabel.length > 0) +
                                                    "|menu-indicator:" +
                                                    String(showMenuIndicator) +
                                                    "|menu-affordance:hover-chevron" +
                                                    "|status-dot:pending-warning" +
                                                    "|active-state:" +
                                                    String(active)
    readonly property string variantBadgeContract: "tool-variant-badge:v1|compact-symbol:true|no-raw-slice:true|tooltip-full:true|accessibility-full:true|badge:" +
                                                   variantBadgeLabel()
    readonly property string menuAffordanceContract: "tool-menu-affordance:v2|hover-chevron:true|corner:bottom-right|right-click:true|press-hold:true|keyboard:alt-down-alt-space|tooltip-full:true"
    readonly property string resolvedTooltip: {
        if (!enabled || !commandEnabled)
            return disabledReason.length > 0 ? disabledReason : disabledFallbackReason
        let parts = []
        const base = label.length > 0 ? label : text
        if (base.length > 0)
            parts.push(base)
        if (variantLabel.length > 0)
            parts.push(variantLabel)
        if (statusText.length > 0 && statusText !== variantLabel)
            parts.push(statusText)
        if (shortcut.length > 0)
            parts.push(shortcut)
        return parts.join("  ")
    }

    function variantBadgeLabel() {
        const normalized = String(root.variantLabel || "").trim()
        if (normalized.length === 0)
            return ""
        const lower = normalized.toLowerCase()
        if (lower === "add" || lower === "plus")
            return "+"
        if (lower === "subtract" || lower === "erase" || lower === "minus")
            return "-"
        if (lower === "intersect")
            return "i"
        if (lower === "replace")
            return "R"
        if (lower === "before")
            return "B"
        if (lower === "after")
            return "A"
        const words = normalized.split(/\s+/)
        if (words.length > 1)
            return words.map(function(word) { return word.charAt(0).toUpperCase() }).join("")
        return normalized.charAt(0).toUpperCase()
    }

    implicitWidth: showLabel ? Math.max(compact ? 58 : 68, toolContent.implicitWidth + 16)
                             : (compact ? theme.iconButtonSizeCompact : theme.railButtonSize)
    implicitHeight: compact ? theme.iconButtonSizeCompact : theme.railButtonSize
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: label.length > 0 ? label : text
    Accessible.description: !enabled || !commandEnabled ? resolvedTooltip : variantLabel
    Accessible.role: Accessible.Button
    Accessible.checked: root.checkable && root.checked

    AppToolTip {
        targetItem: root
        visible: root.hovered && root.resolvedTooltip.length > 0
        delay: 300
        timeout: 3000
        text: root.resolvedTooltip
    }

    onPressAndHold: if (showMenuIndicator) menuRequested()

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: root.showMenuIndicator
        onTapped: root.menuRequested()
    }

    background: CommandButtonSurface {
        cornerRadius: theme.radius6
        fillColor: !root.enabled || !root.commandEnabled ? theme.alpha(theme.surfaceInset, 0.35)
                   : root.down ? theme.surfaceInset
                   : root.active ? theme.interactionFill
                   : root.hovered ? theme.surfaceTop : "transparent"
        borderWidth: root.visualFocus ? theme.focusBorderWidth : 0
        borderColor: root.visualFocus ? theme.focusRing
                     : root.active || root.hovered ? theme.controlEdge : "transparent"
        accentWidth: 2
        accentColor: root.stateColor
        accentOpacity: root.active && root.enabled && root.commandEnabled ? 0.65 : 0
        topLineOpacity: root.hovered || root.active ? 0.08 : 0
    }

    contentItem: Item {
        id: toolContent

        implicitWidth: root.showLabel ? contentRow.implicitWidth : 16
        implicitHeight: root.showLabel ? contentRow.implicitHeight : 16

        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            spacing: root.showLabel ? theme.space6 : 0

            AppIcon {
                visible: root.iconName.length > 0
                Layout.preferredWidth: root.toolIconSize
                Layout.preferredHeight: root.toolIconSize
                Layout.alignment: Qt.AlignCenter
                name: root.iconName
                color: !root.enabled || !root.commandEnabled ? theme.alpha(theme.textMuted, 0.82)
                                      : (root.active || root.pending || root.warning ? root.stateColor : theme.textMain)
            }

            Text {
                visible: root.iconName.length === 0 && root.iconText.length > 0
                text: root.iconText
                color: !root.enabled || !root.commandEnabled ? theme.alpha(theme.textMuted, 0.82)
                                      : (root.active || root.pending || root.warning ? root.stateColor : theme.textMain)
                font.family: theme.fontMono
                font.pixelSize: root.compact ? theme.typeLabel : theme.typeControl
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                visible: root.showLabel
                text: root.label
                color: !root.enabled || !root.commandEnabled ? theme.alpha(theme.textMuted, 0.84)
                                      : (root.active ? theme.textStrong : theme.textMain)
                font.family: theme.fontUi
                font.pixelSize: root.compact ? theme.typeLabel : theme.typeControl
                font.weight: root.active ? Font.DemiBold : Font.Medium
                font.letterSpacing: 0
                maximumLineCount: 1
                elide: Text.ElideNone
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 10
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 3
            anchors.bottomMargin: 3
            width: root.pending || root.warning ? 5 : 0
            height: width
            radius: Math.max(1, width / 2)
            visible: !root.showMenuIndicator && width > 0
            color: root.stateColor
        }

        AppIcon {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 2
            anchors.bottomMargin: 2
            width: theme.scaled(10)
            height: theme.scaled(10)
            visible: root.showMenuIndicator && (root.hovered || root.active || root.visualFocus)
            name: "chevron-right"
            rotation: 90
            color: root.active || root.pending || root.warning
                   ? root.stateColor
                   : theme.alpha(theme.textMuted, root.hovered ? 0.92 : 0.70)
        }

        Text {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 3
            anchors.topMargin: 2
            visible: root.variantLabel.length > 0 && !root.showLabel && (root.hovered || root.active || root.visualFocus)
            text: root.variantBadgeLabel()
            color: root.stateColor
            font.family: theme.fontMono
            font.pixelSize: theme.typeMicro
            font.weight: Font.DemiBold
        }
    }
}
