pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    property string title: "Layer"
    property string subtitle: ""
    property string detail: ""
    property string metadata: ""
    property string accessibleDescription: ""
    property string iconName: "media"
    property string thumbnailSource: ""
    property bool thumbnailAvailable: thumbnailSource.length > 0
    property int thumbnailSourceSize: 96
    property string targetAreaObjectName: "layerRowTargetArea"
    property string thumbnailObjectName: "layerRowThumbnail"
    property string thumbnailImageObjectName: "layerRowThumbnailImage"
    property string iconObjectName: "layerRowTypeIcon"
    property string titleObjectName: "layerRowNameLabel"
    property string subtitleObjectName: "layerRowStatusLabel"
    property string metadataObjectName: "layerRowMetadataLabel"
    property string lockBadgeObjectName: "layerRowLockBadge"
    property string lockButtonObjectName: "layerRowLockButton"
    property string maskStateObjectName: "layerRowMaskState"
    property string opacityReadoutObjectName: "layerRowOpacityReadout"
    property string visibilityButtonObjectName: "layerRowVisibilityButton"
    property string menuButtonObjectName: "layerRowMoreMenuButton"
    property string lockLabel: "LOCK"
    property string maskLabel: "No Mask"
    property int opacityPercent: -1
    property var badges: []
    property var menuItems: []
    property bool selected: false
    property bool activeMask: false
    property bool locked: false
    property bool hidden: false
    property bool solo: false
    property bool changed: false
    property bool compact: false
    property bool showLockBadge: false
    // Keep the row's always-visible controls to the two common actions. Lock,
    // mask state, and opacity can be opted into by focused property views.
    property bool showLockButton: false
    property bool showMaskState: false
    property bool showOpacityReadout: false
    property bool showVisibilityButton: true
    property bool showMenuButton: true
    property bool lockButtonEnabled: true
    property bool visibilityButtonEnabled: true
    property color accent: theme.accentWarm400
    property color thumbnailAccent: accent
    property string lockDisabledReason: locked ? "Layer is locked." : ""
    property string visibilityDisabledReason: ""
    property string menuTooltip: "Layer actions."
    property string clickRouteContract: ""
    property string reorderTooltip: "Reorder from the layer menu or move buttons."
    property int thumbnailZoneSize: compact ? 30 : 34
    readonly property int visibleActionCount: (showLockButton ? 1 : 0) +
                                               (showMaskState ? 1 : 0) +
                                               (showOpacityReadout ? 1 : 0) +
                                               (showVisibilityButton ? 1 : 0) +
                                               (showMenuButton ? 1 : 0)
    readonly property int actionZoneWidth: (showLockButton ? theme.iconButtonSizeCompact : 0) +
                                            (showMaskState ? 42 : 0) +
                                            (showOpacityReadout ? 34 : 0) +
                                            (showVisibilityButton ? theme.iconButtonSizeCompact : 0) +
                                            (showMenuButton ? theme.iconButtonSizeCompact : 0) +
                                            Math.max(0, visibleActionCount - 1) * theme.space4

    signal clicked()
    signal lockClicked()
    signal visibilityClicked()
    signal menuItemTriggered(string key)

    Theme { id: theme }

    readonly property bool hovered: rowHover.containsMouse
    readonly property bool thumbnailReady: thumbnailAvailable && thumbnailSource.length > 0
    readonly property int rowHeight: Math.max(compact ? 48 : 56,
                                               Math.max(layerTextColumn.implicitHeight + theme.space12,
                                                        thumbnailZoneSize + theme.space14))
    readonly property color activeMaskAccent: theme.accentCool400
    readonly property color rowAccent: activeMask ? activeMaskAccent
                                       : selected ? accent
                                       : solo ? theme.warning
                                       : changed ? accent
                                       : theme.textMuted
    readonly property color rowFill: !enabled ? theme.alpha(theme.surfaceBottom, 0.26)
                                 : selected ? theme.editorSelectionFill
                                 : hovered ? theme.editorControlHover
                                 : activeMask ? theme.alpha(activeMaskAccent, 0.08)
                                 : solo ? theme.alpha(theme.warning, 0.06)
                                 : changed ? theme.alpha(accent, 0.045)
                                 : "transparent"
    readonly property string stateSummary: {
        let parts = []
        if (locked)
            parts.push("Locked")
        if (hidden)
            parts.push("Hidden")
        if (solo)
            parts.push("Isolated")
        if (changed)
            parts.push("Edited")
        return parts.join(" / ")
    }
    readonly property string rowContract: "layer-row:v3|fixed-zones|content-height|no-elide|state-priority|actions:visibility-more|secondary-actions:overflow-menu|thumbnail-lock-badge:none"
    readonly property string zoneContract: "layer-row-zones:v3|thumbnail|name-state|metadata|state-badges|actions:visibility-more|secondary-actions:overflow-menu"
    readonly property string premiumLayerContract: "premium-layer-row:v3|content-height:true|thumbnail-zone:fixed|action-zone:fixed|selected-surface:true|left-accent:none|active-mask-secondary:true|rename-stable:true|delete-separated:true"
    readonly property string layerInteractionAcceptanceContract: "layer-interaction-acceptance:v1|select:true|rename:true|reorder:true|visibility:true|opacity:true|mask-select:true|transform:true|escape:true"
    readonly property string statePriorityContract: "layer-row-state-priority:v2|selected-layer-primary>active-mask-secondary>solo>changed>locked>hidden>hover"
    readonly property string activeMaskIndicatorContract: "layer-active-mask-indicator:v1|secondary-edge:right|accent:cool|visible:" +
                                                          String(root.activeMask) +
                                                          "|selected-does-not-hide:true"
    readonly property string interactionContract: "layer-row-interaction:v1|hover-actions:true|keyboard-actions:true|disabled-reasons:true|rename:direct-or-menu|reorder:move-buttons-or-menu|delete:separated"
    readonly property real thumbnailDevicePixelRatio: Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
    readonly property int screenAwareThumbnailSourceSize: Math.max(
                                                                    Math.max(1, root.thumbnailSourceSize),
                                                                    Math.round(Math.max(1, root.thumbnailZoneSize) * thumbnailDevicePixelRatio))
    readonly property string thumbnailDprContract: "layer-row-thumbnail-dpr:v1|source-size:screen-aware|base-image:true|display:" +
                                                   String(root.thumbnailZoneSize) +
                                                   "|decode:" + String(screenAwareThumbnailSourceSize) +
                                                   "|dpr:" + thumbnailDevicePixelRatio.toFixed(2)
    readonly property bool contextualActionsVisible: hovered || activeFocus || selected || activeMask
    readonly property real contextualActionOpacity: 1.0

    Layout.fillWidth: true
    Layout.preferredHeight: rowHeight
    radius: theme.radiusControl
    color: rowFill
    border.width: activeFocus ? theme.focusBorderWidth : selected || activeMask ? theme.hairlineWidth : 0
    border.color: activeFocus ? theme.focusRing
                  : selected ? theme.alpha(theme.editorSelectionEdge, 0.72)
                  : activeMask ? theme.alpha(activeMaskAccent, 0.56)
                  : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: root.title
    Accessible.description: root.accessibleDescription.length > 0 ? root.accessibleDescription
                               : root.subtitle.length > 0 ? root.subtitle : root.stateSummary
    Accessible.role: Accessible.ListItem
    Accessible.selected: root.selected || root.activeMask
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    MouseArea {
        id: rowHover
        objectName: root.targetAreaObjectName
        readonly property string clickRouteContract: root.clickRouteContract
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        function triggerBaseImageLayerRowClickForTest() {
            root.clicked()
            return true
        }

        onPressed: root.forceActiveFocus()
        onClicked: root.clicked()
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        visible: root.activeMask
        color: root.activeMaskAccent
        opacity: root.selected ? 0.92 : 1.0
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        spacing: 7
        z: 2

        Rectangle {
            objectName: "layerRowReorderHandle"
            Layout.preferredWidth: 10
            Layout.fillHeight: true
            radius: 3
            color: "transparent"
            opacity: root.contextualActionsVisible ? 0.82 : 0.28
            ToolTip.visible: reorderHandleHover.hovered
            ToolTip.delay: 350
            ToolTip.text: root.reorderTooltip

            HoverHandler { id: reorderHandleHover }

            Column {
                anchors.centerIn: parent
                spacing: 2

                Repeater {
                    model: 3

                    Rectangle {
                        width: 3
                        height: 3
                        radius: 1.5
                        color: theme.alpha(theme.textMuted, 0.82)
                    }
                }
            }
        }

        Rectangle {
            objectName: root.thumbnailObjectName
            Layout.minimumWidth: root.thumbnailZoneSize
            Layout.preferredWidth: root.thumbnailZoneSize
            Layout.preferredHeight: root.thumbnailZoneSize
            radius: 4
            color: theme.alpha(root.thumbnailAccent, 0.12)
            border.width: theme.chromeBorderWidth
            border.color: root.selected ? theme.controlEdge
                                        : theme.alpha(theme.textMain, 0.20)
            clip: true

            Image {
                objectName: root.thumbnailImageObjectName
                anchors.fill: parent
                anchors.margins: 1
                source: root.thumbnailSource
                sourceSize.width: root.screenAwareThumbnailSourceSize
                sourceSize.height: root.screenAwareThumbnailSourceSize
                visible: root.thumbnailReady
                fillMode: Image.PreserveAspectCrop
                smooth: true
                asynchronous: true
                cache: false
            }

            AppIcon {
                objectName: root.iconObjectName
                anchors.centerIn: parent
                visible: !root.thumbnailReady
                name: root.iconName
                width: root.compact ? 14 : 15
                height: width
                color: theme.textMain
            }

            Rectangle {
                objectName: root.lockBadgeObjectName
                visible: false
            }
        }

        ColumnLayout {
            id: layerTextColumn
            Layout.fillWidth: true
            spacing: theme.space2

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space4

                Label {
                    objectName: root.titleObjectName
                    Layout.fillWidth: true
                    text: root.title
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    font.weight: theme.weightControl
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }

                Repeater {
                    model: root.badges

                    delegate: Rectangle {
                        id: badgeDelegate
	                        required property var modelData
	                        readonly property string badgeLabel: String(modelData.label || modelData)
	                        readonly property string text: badgeLabel
	                        readonly property bool badgeActive: modelData.active === true
                        readonly property color badgeAccent: modelData.accent || root.accent

                        objectName: String(modelData.objectName || "")
                        Layout.preferredWidth: Math.min(100, badgeText.implicitWidth)
                        Layout.preferredHeight: badgeText.implicitHeight
                        radius: 0
                        color: "transparent"
                        border.width: 0
                        border.color: "transparent"

                        ToolTip.visible: badgeHover.containsMouse && badgeLabel.length > 0
                        ToolTip.delay: 300
                        ToolTip.text: badgeLabel

                        MouseArea {
                            id: badgeHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }

                        Label {
                            id: badgeText
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            anchors.rightMargin: 5
                            text: badgeDelegate.badgeLabel
                            color: badgeDelegate.badgeActive ? theme.alpha(badgeDelegate.badgeAccent, 0.88) : theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: theme.weightControl
                            verticalAlignment: Text.AlignVCenter
                            maximumLineCount: 1
                            elide: Text.ElideNone
                        }
                    }
                }
            }

            Label {
                objectName: root.subtitleObjectName
                Layout.fillWidth: true
                text: root.subtitle.length > 0 ? root.subtitle : root.stateSummary
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: theme.weightControl
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                visible: text.length > 0
            }

            Label {
                objectName: root.metadataObjectName
                Layout.fillWidth: true
                text: root.metadata.length > 0 ? root.metadata : root.detail
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                visible: text.length > 0
            }
        }

        RowLayout {
            Layout.minimumWidth: root.actionZoneWidth
            Layout.preferredWidth: root.actionZoneWidth
            spacing: 5
            opacity: root.contextualActionOpacity

            Components.ToolButton {
                objectName: root.lockButtonObjectName
                visible: root.showLockButton
                iconName: "lock"
                compact: true
                active: root.locked
                enabled: root.lockButtonEnabled
                label: root.locked ? "Unlock layer" : "Lock layer"
                disabledReason: root.lockDisabledReason
                onClicked: root.lockClicked()
            }

            Rectangle {
                objectName: root.maskStateObjectName
                visible: root.showMaskState
                Layout.preferredWidth: 42
                Layout.preferredHeight: 24
                radius: 3
                color: root.activeMask
                       ? theme.alpha(root.activeMaskAccent, 0.16)
                       : root.maskLabel !== "No Mask"
                       ? theme.alpha(root.accent, 0.13)
                       : theme.alpha(theme.surfaceTop, 0.34)
                border.width: theme.chromeBorderWidth
                border.color: root.activeMask
                              ? theme.alpha(root.activeMaskAccent, 0.58)
                              : root.maskLabel !== "No Mask"
                              ? theme.alpha(root.accent, 0.48)
                              : theme.alpha(theme.textMuted, 0.22)
                ToolTip.visible: maskStateHover.hovered
                ToolTip.delay: 300
                ToolTip.text: root.maskLabel

                HoverHandler { id: maskStateHover }

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 5
                    text: root.maskLabel === "No Mask" ? "NO" : "MASK"
                    color: root.activeMask ? root.activeMaskAccent : (root.maskLabel !== "No Mask" ? root.accent : theme.textMuted)
                    font.family: theme.fontMono
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 6
                    elide: Text.ElideNone
                }
            }

            Label {
                objectName: root.opacityReadoutObjectName
                visible: root.showOpacityReadout
                Layout.preferredWidth: 34
                text: Math.max(0, Math.min(100, root.opacityPercent)) + "%"
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 7
                elide: Text.ElideNone
            }

            Components.ToolButton {
                objectName: root.visibilityButtonObjectName
                visible: root.showVisibilityButton
                iconName: root.hidden ? "hidden" : "visible"
                compact: true
                active: !root.hidden
                enabled: root.visibilityButtonEnabled
                label: root.hidden ? "Show layer" : "Hide layer"
                disabledReason: root.visibilityDisabledReason
                onClicked: root.visibilityClicked()
            }

            MenuButton {
                objectName: root.menuButtonObjectName
                visible: root.showMenuButton
                text: "More"
                iconName: "menu"
                compact: true
                enabled: root.menuItems.length > 0
                tooltipText: root.menuTooltip
                menuItems: root.menuItems
                onMenuItemTriggered: function(key) { root.menuItemTriggered(key) }
            }
        }
    }
}
