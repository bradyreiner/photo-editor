pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string toolLabel: ""
    property string statusText: ""
    property string modeText: ""
    property string modeIconName: ""
    property var modeMenuItems: []
    property var inlineActions: []
    property string primaryText: "Done"
    property string primaryIconName: "check"
    property bool primaryEnabled: true
    property string primaryTooltip: ""
    property var moreMenuItems: []
    property color accent: theme.accentCool400
    property string statusObjectName: "toolCommandStatusLabel"
    property string modeButtonObjectName: "toolCommandModeMenuButton"
    property string primaryButtonObjectName: "toolCommandPrimaryButton"
    property string moreButtonObjectName: "toolCommandMoreMenuButton"
    property int adaptiveWidthThreshold: 640
    property bool modeChangeAnimationEnabled: false
    property bool modeChangeAnimationReady: false
    readonly property int commandHeight: 44
    readonly property bool adaptiveMenuMode: width > 0 && width < adaptiveWidthThreshold
    readonly property var adaptiveInlineMenuItems: adaptiveMenuMode ? inlineActionMenuItems() : []
    readonly property var effectiveMoreMenuItems: adaptiveInlineMenuItems.concat(moreMenuItems || [])
    readonly property int adaptiveInlineActionCount: adaptiveInlineMenuItems.length
    readonly property int effectiveMoreMenuItemCount: effectiveMoreMenuItems.length
    readonly property string commandBarContract: "tool-command-bar:v2|single-row:true|fixed-height:" +
                                                 String(commandHeight) +
                                                 "|mode-menu:true|inline-actions:bounded|primary-action:single|more-menu:true|no-wrap:true|secondary-actions:menu|adaptive-menu:true|narrow-inline-to-more:true|status-collapses:true"
    readonly property string adaptiveLayoutContract: "tool-command-bar-adaptive:v1|threshold:" +
                                                     String(adaptiveWidthThreshold) +
                                                     "|active:" +
                                                     String(adaptiveMenuMode) +
                                                     "|inline-to-more:" +
                                                     String(adaptiveInlineActionCount) +
                                                     "|more-count:" +
                                                     String(effectiveMoreMenuItemCount) +
                                                     "|status-visible:" +
                                                     String(!adaptiveMenuMode) +
                                                     "|single-row:true|height:" +
                                                     String(commandHeight)
    readonly property string motionContract: "tool-command-bar-motion:v2|duration:140|fade:true|stable-height:true|stable-layout:true|interruptible:true|mode-change:" +
                                             String(modeChangeAnimationEnabled)
    readonly property string modeChangeMotionContract: "mode-change-motion:v1|enabled:" +
                                                       String(modeChangeAnimationEnabled) +
                                                       "|duration:140|target:mode-menu|scale:0.98-1.035-1|layout-stable:true|interruptible:true"
    readonly property string primaryActionSemanticsContract: "tool-command-primary-semantics:v1|text:" +
                                                            root.primaryText +
                                                            "|meaning:" +
                                                            (root.primaryText === "Apply"
                                                             ? "commit-or-generate-result"
                                                             : root.primaryText === "Done"
                                                               ? "exit-tool-state"
                                                               : "custom-command") +
                                                            "|apply-only-commit:true|done-only-exit:true|cancel:not-primary|close:not-primary"

    signal modeClicked()
    signal modeMenuItemTriggered(string key)
    signal inlineActionTriggered(string key)
    signal primaryTriggered()
    signal moreClicked()
    signal moreMenuItemTriggered(string key)

    Theme { id: theme }

    Component.onCompleted: modeChangeAnimationReady = true

    onModeTextChanged: {
        if (modeChangeAnimationEnabled && modeChangeAnimationReady)
            modeChangeAnimation.restart()
    }

    implicitWidth: commandBarContent.implicitWidth + 18
    implicitHeight: commandHeight
    height: commandHeight
    radius: 0
    color: "transparent"
    border.width: 0
    border.color: "transparent"
    clip: false
    opacity: visible ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }

    function inlineActionMenuItems() {
        const actions = root.inlineActions || []
        const items = []
        for (let index = 0; index < actions.length; ++index) {
            const action = actions[index]
            if (!action || action.visible === false)
                continue
            items.push({
                "key": String(action.key || ""),
                "label": String(action.label || ""),
                "icon": String(action.icon || ""),
                "checked": action.checked === true || action.active === true,
                "enabled": action.enabled !== false,
                "disabledReason": String(action.disabledReason || ""),
                "tooltip": String(action.tooltip || ""),
                "adaptiveInline": true
            })
        }
        return items
    }

    function moreKeyIsAdaptiveInline(key) {
        const requested = String(key || "")
        const actions = root.inlineActions || []
        for (let index = 0; index < actions.length; ++index) {
            const action = actions[index]
            if (action && action.visible !== false && String(action.key || "") === requested)
                return true
        }
        return false
    }

    RowLayout {
        id: commandBarContent
        anchors.fill: parent
        anchors.leftMargin: 9
        anchors.rightMargin: 8
        spacing: 6

        Label {
            objectName: "toolCommandBarToolLabel"
            text: root.toolLabel
            color: root.accent
            font.family: theme.fontUi
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
            Layout.preferredWidth: Math.max(54, implicitWidth)
            Layout.maximumWidth: 86
        }

        MenuButton {
            id: modeMenuButton
            objectName: root.modeButtonObjectName
            Layout.preferredWidth: root.adaptiveMenuMode ? Math.max(112, implicitWidth) : Math.max(142, implicitWidth)
            text: root.modeText
            iconName: root.modeIconName
            compact: true
            active: true
            accent: root.accent
            transformOrigin: Item.Center
            menuWidth: 260
            menuItemMaxLines: 3
            menuItems: root.modeMenuItems
            primaryOpensMenu: true
            tooltipText: "Choose " + root.toolLabel.toLowerCase() + " mode."
            menuTooltipText: root.toolLabel + " modes"
            onClicked: root.modeClicked()
            onMenuItemTriggered: function(key) { root.modeMenuItemTriggered(key) }
        }

        SequentialAnimation {
            id: modeChangeAnimation
            NumberAnimation {
                target: modeMenuButton
                property: "scale"
                from: 0.98
                to: 1.035
                duration: 70
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: modeMenuButton
                property: "scale"
                from: 1.035
                to: 1.0
                duration: 70
                easing.type: Easing.OutCubic
            }
        }

        Repeater {
            model: root.inlineActions || []

            MiniButton {
                required property int index
                required property var modelData

                objectName: String(modelData.objectName || ("toolCommandInlineAction" + index))
                visible: !root.adaptiveMenuMode && modelData.visible !== false
                text: String(modelData.label || "")
                iconName: String(modelData.icon || "")
                role: String(modelData.role || "segmented")
                active: modelData.checked === true || modelData.active === true
                enabled: modelData.enabled !== false
                accent: modelData.accent !== undefined ? modelData.accent : root.accent
                tooltip: String(modelData.tooltip || "")
                minimumReadableWidth: Math.max(0, Number(modelData.minimumWidth || 0))
                onClicked: root.inlineActionTriggered(String(modelData.key || ""))
            }
        }

        Label {
            objectName: root.statusObjectName
            readonly property string readableStatusContract: "tool-command-status:v1|full:" + text +
                                                             "|fit-horizontal:true|tooltip-full:true"
            visible: !root.adaptiveMenuMode
            Layout.preferredWidth: root.adaptiveMenuMode ? 0 : 220
            Layout.maximumWidth: root.adaptiveMenuMode ? 0 : 260
            text: root.statusText
            color: theme.textMain
            font.family: theme.fontMono
            font.pixelSize: 10
            font.weight: Font.DemiBold
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            maximumLineCount: 1
            elide: Text.ElideNone
            ToolTip.visible: statusHover.hovered && contentWidth > width + 1
            ToolTip.delay: 300
            ToolTip.text: text

            HoverHandler { id: statusHover }
        }

        MiniButton {
            objectName: root.primaryButtonObjectName
            minimumReadableWidth: 82
            text: root.primaryText
            iconName: root.primaryIconName
            role: "primary"
            enabled: root.primaryEnabled
            accent: root.accent
            tooltip: root.primaryTooltip
            onClicked: root.primaryTriggered()
        }

        MenuButton {
            objectName: root.moreButtonObjectName
            Layout.preferredWidth: root.adaptiveMenuMode ? Math.max(74, implicitWidth) : Math.max(92, implicitWidth)
            text: "More"
            iconName: "menu"
            compact: true
            active: false
            accent: root.accent
            menuWidth: 286
            menuItemMaxLines: 3
            menuItems: root.effectiveMoreMenuItems
            primaryOpensMenu: true
            tooltipText: "Open secondary " + root.toolLabel.toLowerCase() + " actions."
            menuTooltipText: root.toolLabel + " actions"
            onClicked: root.moreClicked()
            onMenuItemTriggered: function(key) {
                if (root.adaptiveMenuMode && root.moreKeyIsAdaptiveInline(key))
                    root.inlineActionTriggered(key)
                else
                    root.moreMenuItemTriggered(key)
            }
        }
    }
}
