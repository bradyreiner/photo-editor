pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    required property var previewController
    property string toolLabel: "Retouch"
    property var modeMenuItems: []
    property string statusText: ""
    property string statusObjectName: "retouchHudStatusLabel"
    property string modeButtonObjectName: "retouchHudModeMenuButton"
    property string visibilityContract: ""
    property string commandPlacementContract: ""
    property string primaryText: "Apply"
    property string primaryTooltip: ""
    property string primaryIconName: "check"
    property bool primaryEnabled: true
    property string primaryButtonObjectName: "retouchHudApplyButton"
    property string moreButtonObjectName: "retouchHudMoreMenuButton"
    property bool cloneSourcePickArmed: false
    property var inlineActions: []
    property var moreMenuItems: []
    property color accent: "#d98962"
    readonly property real contentWidth: Math.max(0, width - theme.scaled(18))
    readonly property real responsiveWidth: Math.max(560, theme.scaled(360))
    readonly property bool narrowLayout: width > 0 && width < responsiveWidth
    readonly property int modeGridColumns: width < theme.scaled(300) ? 1 : (narrowLayout ? 2 : 4)
    readonly property real availableHeight: parent ? Math.max(1, parent.height - y - theme.scaled(12)) : idealHeight
    readonly property real idealHeight: headerFlow.implicitHeight + retouchHudScrollableContent.implicitHeight + theme.scaled(24)
    readonly property string activeMode: String(previewController.activeMaskRetouchMode || "remove")
    readonly property string activeModeLabel: activeMode === "dust" ? "Dust Cleanup" : activeMode === "red-eye" ? "Red Eye" : ""
    property bool cloneSourcePickAllowed: activeMode === "clone" && !previewController.activeMaskCommitted && !previewController.activeMaskLocked && !previewController.inpaintProgressActive && !previewController.compareMode
    readonly property string retouchHudContract: "retouch-tool-hud:v3|modes:remove-heal-patch-clone|labels-and-icons:true|contextual-guidance:true|quick-brush:size-presets|source:clone-picker-one-click-no-paint|source-cancel:escape-tool-mode-transient-clear|patch:canvas-guide|review:before-after|advanced:menus-and-inspector|responsive:grid:1-2-4|viewport:height-bounded-scroll:true"
    readonly property string hudContract: "specialized-tool-hud:v4|tool:retouch|component:RetouchToolHud|visible-modes:remove-heal-patch-clone|contextual-guidance:true|brush-size-presets:true|primary-action:single|review:before-after|secondary-actions:menus|no-overlap:true"
    readonly property string guidanceText: {
        if (previewController.inpaintProgressActive)
            return String(previewController.inpaintProgressLabel || "Working on the repair…") + ". You can cancel without losing the previous result.";
        if (previewController.activeMaskCommitted)
            return "Repair applied. Compare Before and After, then choose Done when it looks right.";
        if (activeMode === "dust")
            return "Dust Cleanup checks for small spots. Review the result, then choose Done when it looks right.";
        if (activeMode === "red-eye")
            return "Red Eye checks for red-eye in the photo. Review the result, then choose Done when it looks right.";
        if (activeMode === "clone") {
            if (root.cloneSourcePickArmed)
                return "Click a clean source point on the photo. This pick will not paint a stroke.";
            return previewController.activeMaskCloneSourceSet ? "Paint over the target. Drag the blue source marker to match nearby texture." : "Pick a source point first, then paint over the area you want to copy into.";
        }
        if (activeMode === "patch")
            return "Paint over the repair area. If the source outline is visible, drag it to matching nearby texture.";
        if (activeMode === "heal")
            return "Brush over a blemish or seam. Heal blends the repair into nearby color and texture.";
        return "Paint over unwanted content to reconstruct it from nearby image texture.";
    }

    signal modeSelected(string mode)
    signal secondaryModeItemTriggered(string key)
    signal inlineActionTriggered(string key)
    signal primaryTriggered
    signal moreMenuItemTriggered(string key)
    signal cloneSourceRequested

    Theme {
        id: theme
    }

    implicitWidth: 820
    implicitHeight: Math.min(idealHeight, availableHeight)
    height: implicitHeight
    radius: theme.radiusSurface
    color: theme.alpha(theme.ink950, 0.94)
    border.width: theme.chromeBorderWidth
    border.color: theme.alpha(theme.lineSoft, 0.82)
    clip: false

    ColumnLayout {
        id: hudContent
        anchors.fill: parent
        anchors.margins: 9
        spacing: 6

        Flow {
            id: headerFlow
            objectName: "retouchHudHeader"
            Layout.fillWidth: true
            spacing: theme.scaled(6)

            RowLayout {
                width: Math.min(theme.scaled(86), root.contentWidth)
                height: theme.controlHeightDense
                spacing: theme.scaled(5)

                Components.AppIcon {
                    name: "retouch"
                    color: root.accent
                    Layout.preferredWidth: theme.scaled(15)
                    Layout.preferredHeight: theme.scaled(15)
                }

                Label {
                    Layout.fillWidth: true
                    text: root.toolLabel.toUpperCase()
                    color: root.accent
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: Font.DemiBold
                }
            }

            Label {
                objectName: "retouchHudActiveModeLabel"
                visible: root.activeModeLabel.length > 0
                width: Math.min(theme.scaled(132), root.contentWidth)
                text: root.activeModeLabel
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: Font.Medium
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }

            Label {
                objectName: root.statusObjectName
                readonly property string readableStatusContract: "retouch-hud-status:v1|fit-horizontal:true|tooltip-full:true"
                width: Math.min(theme.scaled(220), root.contentWidth)
                height: theme.controlHeightDense
                visible: !root.narrowLayout
                text: root.statusText
                color: theme.textMuted
                font.family: theme.fontNumeric
                font.features: theme.numericFontFeatures
                font.pixelSize: theme.typeMicro
                maximumLineCount: 1
                elide: Text.ElideRight
            }

            Components.MenuButton {
                objectName: root.modeButtonObjectName
                visible: (root.modeMenuItems || []).length > 0
                width: Math.min(theme.scaled(root.narrowLayout ? 70 : 104), root.contentWidth)
                height: theme.controlHeightDense
                text: root.narrowLayout ? "Tools" : "Other tools"
                iconName: "brush-tool"
                compact: true
                active: false
                accent: root.accent
                menuWidth: 250
                menuItemMaxLines: 2
                menuItems: root.modeMenuItems
                tooltipText: "Choose a focused retouch tool."
                menuTooltipText: "Other retouch tools"
                onMenuItemTriggered: function (key) {
                    root.secondaryModeItemTriggered(key);
                }
            }

            Components.MenuButton {
                objectName: root.moreButtonObjectName
                width: Math.min(theme.scaled(root.narrowLayout ? 72 : 84), root.contentWidth)
                height: theme.controlHeightDense
                text: root.narrowLayout ? "More" : "Options"
                iconName: "menu"
                compact: true
                active: false
                accent: root.accent
                menuWidth: 300
                menuItemMaxLines: 3
                menuItems: root.moreMenuItems
                tooltipText: "Compare the repair and open advanced retouch options."
                menuTooltipText: "Review and retouch options"
                onMenuItemTriggered: function (key) {
                    root.moreMenuItemTriggered(key);
                }
            }

            Components.MiniButton {
                objectName: root.primaryText === "Cancel" ? "retouchHudCancelButton" : (root.primaryText === "Done" ? "retouchHudDoneButton" : root.primaryButtonObjectName)
                width: Math.min(Math.max(theme.scaled(82), implicitWidth), root.contentWidth)
                height: theme.controlHeightDense
                text: root.primaryText
                iconName: root.primaryIconName
                showLeadingIcon: true
                role: "primary"
                enabled: root.primaryEnabled
                accent: root.accent
                tooltip: root.primaryTooltip
                onClicked: root.primaryTriggered()
            }
        }

        Flickable {
            id: retouchHudScroll
            objectName: "retouchHudScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: width
            contentHeight: retouchHudScrollableContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

            ColumnLayout {
                id: retouchHudScrollableContent
                width: retouchHudScroll.width
                spacing: 6

                GridLayout {
                    id: modeGrid
                    objectName: "retouchHudModeGrid"
                    Layout.fillWidth: true
                    columns: root.modeGridColumns
                    columnSpacing: 5
                    rowSpacing: 5

                    Components.MiniButton {
                        objectName: "retouchHudRemoveButton"
                        Layout.fillWidth: true
                        text: "Remove"
                        iconName: "erase-tool"
                        showLeadingIcon: true
                        allowLabelWrap: false
                        role: "segmented"
                        active: root.activeMode === "remove"
                        accent: "#d98962"
                        tooltip: "Reconstruct unwanted content from nearby image texture."
                        onClicked: root.modeSelected("remove")
                    }

                    Components.MiniButton {
                        objectName: "retouchHudHealButton"
                        Layout.fillWidth: true
                        text: "Heal"
                        iconName: "brush-tool"
                        showLeadingIcon: true
                        allowLabelWrap: false
                        role: "segmented"
                        active: root.activeMode === "heal"
                        accent: "#72cbb2"
                        tooltip: "Blend a blemish or seam into nearby color and texture."
                        onClicked: root.modeSelected("heal")
                    }

                    Components.MiniButton {
                        objectName: "retouchHudPatchButton"
                        Layout.fillWidth: true
                        text: "Patch"
                        iconName: "geometry"
                        showLeadingIcon: true
                        allowLabelWrap: false
                        role: "segmented"
                        active: root.activeMode === "patch"
                        accent: "#72cbb2"
                        tooltip: "Paint a repair area and match it to a movable source region."
                        onClicked: root.modeSelected("patch")
                    }

                    Components.MiniButton {
                        objectName: "retouchHudCloneButton"
                        Layout.fillWidth: true
                        text: "Clone"
                        iconName: "source"
                        showLeadingIcon: true
                        allowLabelWrap: false
                        role: "segmented"
                        active: root.activeMode === "clone"
                        accent: "#7cb5ff"
                        tooltip: "Copy pixels from a sampled source point."
                        onClicked: root.modeSelected("clone")
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 7

                    ColumnLayout {
                        width: Math.min(root.contentWidth, Math.max(theme.scaled(190), Math.min(theme.scaled(270), root.width * 0.34)))
                        spacing: 1

                        Label {
                            text: "PURPOSE"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.35
                        }

                        Label {
                            objectName: "retouchHudGuidanceLabel"
                            Layout.fillWidth: true
                            text: root.guidanceText
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                    }

                    RowLayout {
                        objectName: "retouchHudRefineActions"
                        width: visible ? Math.min(theme.scaled(132), root.contentWidth) : 0
                        height: visible ? implicitHeight : 0
                        visible: root.activeMode === "remove" && (root.inlineActions || []).length > 0
                        spacing: 3

                        Repeater {
                            model: root.inlineActions || []

                            Components.MiniButton {
                                required property int index
                                required property var modelData
                                objectName: String(modelData.objectName || ("retouchHudInlineAction" + index))
                                Layout.fillWidth: true
                                text: String(modelData.label || "")
                                iconName: String(modelData.icon || "")
                                showLeadingIcon: true
                                allowLabelWrap: true
                                role: String(modelData.role || "segmented")
                                active: modelData.checked === true || modelData.active === true
                                accent: modelData.accent !== undefined ? modelData.accent : root.accent
                                enabled: modelData.enabled !== false
                                tooltip: String(modelData.tooltip || "")
                                onClicked: root.inlineActionTriggered(String(modelData.key || ""))
                            }
                        }
                    }

                    Components.MiniButton {
                        objectName: "retouchHudClonePickSourceButton"
                        visible: root.activeMode === "clone"
                        width: Math.min(theme.scaled(112), root.contentWidth)
                        text: root.cloneSourcePickArmed ? "Picking source" : (root.previewController.activeMaskCloneSourceSet ? "Change source" : "Pick source")
                        iconName: "target"
                        showLeadingIcon: true
                        allowLabelWrap: true
                        role: root.cloneSourcePickArmed ? "primary" : "secondary"
                        active: root.cloneSourcePickArmed
                        enabled: root.cloneSourcePickArmed || root.cloneSourcePickAllowed
                        accent: "#7cb5ff"
                        disabledReason: root.previewController.inpaintProgressActive ? "Wait for the current retouch operation to finish." : root.previewController.compareMode ? "Close comparison before choosing a source point." : root.previewController.activeMaskLocked ? "Unlock this repair before choosing a source point." : "Reopen this repair before choosing a source point."
                        tooltip: root.cloneSourcePickArmed ? "Click a clean source point on the photo." : "Choose the photo point Clone will copy from."
                        onClicked: root.cloneSourceRequested()
                    }

                    Components.BrushPresetPicker {
                        objectName: "retouchHudBrushPresets"
                        width: Math.min(theme.scaled(112), root.contentWidth)
                        height: theme.controlHeightDense
                        presetFamily: "retouch"
                        currentSettings: ({
                                sizePercent: Math.round(root.previewController.activeMaskBrushSize * 100),
                                hardness: root.previewController.activeMaskBrushHardness,
                                flow: root.previewController.activeMaskBrushFlow
                            })
                        onPresetSelected: function (settings) {
                            if (settings.sizePercent !== undefined && isFinite(Number(settings.sizePercent)))
                                root.previewController.activeMaskBrushSize = Math.max(0.01, Math.min(0.35, Number(settings.sizePercent) / 100));
                            if (settings.hardness !== undefined && isFinite(Number(settings.hardness)))
                                root.previewController.activeMaskBrushHardness = Math.max(0, Math.min(1, Number(settings.hardness)));
                            if (settings.flow !== undefined && isFinite(Number(settings.flow)))
                                root.previewController.activeMaskBrushFlow = Math.max(0.05, Math.min(1, Number(settings.flow)));
                        }
                    }

                    Components.InspectorSlider {
                        objectName: "retouchHudBrushSizeSlider"
                        width: Math.min(theme.scaled(188), root.contentWidth)
                        controlHeight: theme.controlHeightDense
                        forceCompact: true
                        styleVariant: "compact-hud"
                        showResetButton: false
                        valueInputEnabled: false
                        labelColumnWidth: theme.scaled(42)
                        accent: root.accent
                        label: "Brush size"
                        from: 0.01
                        to: 0.35
                        stepSize: 0.01
                        value: root.previewController.activeMaskBrushSize
                        valueText: Math.round(root.previewController.activeMaskBrushSize * 100) + "%"
                        onValueDragged: function (value) {
                            root.previewController.activeMaskBrushSize = value;
                        }
                    }

                    ColumnLayout {
                        objectName: "retouchHudReviewControls"
                        width: Math.min(theme.scaled(180), root.contentWidth)
                        spacing: 1
                        visible: root.previewController.retouchCount > 0

                        Label {
                            text: "REVIEW"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.35
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: root.narrowLayout ? 1 : 2
                            columnSpacing: 3
                            rowSpacing: 3

                            Components.MiniButton {
                                objectName: "retouchHudReviewBeforeButton"
                                Layout.fillWidth: true
                                text: "Before"
                                iconName: "compare"
                                showLeadingIcon: true
                                allowLabelWrap: false
                                role: "segmented"
                                active: root.previewController.retouchBeforeAfterEnabled
                                accent: root.accent
                                tooltip: "Show the image before retouch."
                                onClicked: root.previewController.retouchBeforeAfterEnabled = true
                            }

                            Components.MiniButton {
                                objectName: "retouchHudReviewAfterButton"
                                Layout.fillWidth: true
                                text: "After"
                                iconName: "compare"
                                showLeadingIcon: true
                                allowLabelWrap: false
                                role: "segmented"
                                active: !root.previewController.retouchBeforeAfterEnabled
                                accent: root.accent
                                tooltip: "Show the image after retouch."
                                onClicked: root.previewController.retouchBeforeAfterEnabled = false
                            }
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: retouchHudScroll.contentHeight > retouchHudScroll.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
        }
    }
}
