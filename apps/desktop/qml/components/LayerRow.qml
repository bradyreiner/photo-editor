import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    property string title: "Layer"
    property string subtitle: ""
    property string detail: ""
    property string metadata: ""
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
    property bool showLockButton: true
    property bool showMaskState: false
    property bool showOpacityReadout: opacityPercent >= 0
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
    property int actionZoneWidth: showLockButton || showMaskState || showOpacityReadout || showVisibilityButton || showMenuButton ? 184 : 0

    signal clicked()
    signal lockClicked()
    signal visibilityClicked()
    signal menuItemTriggered(string key)

    Theme { id: theme }

    readonly property bool hovered: rowHover.containsMouse
    readonly property bool thumbnailReady: thumbnailAvailable && thumbnailSource.length > 0
    readonly property int rowHeight: compact ? 58 : 74
    readonly property color activeMaskAccent: theme.accentCool400
    readonly property color rowAccent: selected ? accent
                                       : activeMask ? activeMaskAccent
                                       : solo ? theme.warning
                                       : changed ? accent
                                       : theme.textMuted
    readonly property color rowFill: !enabled ? theme.alpha(theme.glassBase, 0.26)
                                 : selected ? theme.alpha(rowAccent, 0.11)
                                 : hovered ? theme.alpha(theme.glassLift, 0.30)
                                 : theme.alpha(theme.panelColor, 0.48)
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
    readonly property string rowContract: "layer-row:v1|fixed-zones|no-elide|state-priority|menu-actions"
    readonly property string zoneContract: "layer-row-zones:v1|accent-edge|thumbnail|name-state|metadata|state-badges|actions:visibility-mask-opacity-more|overflow-menu"
    readonly property string premiumLayerContract: "premium-layer-row:v2|stable-height:true|thumbnail-zone:fixed|action-zone:fixed|soft-selected-fill:true|single-left-accent:true|active-mask-secondary:true|rename-stable:true|delete-separated:true"
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
    readonly property real contextualActionOpacity: contextualActionsVisible ? 1.0 : 0.54

    Layout.fillWidth: true
    Layout.preferredHeight: rowHeight
    radius: theme.radius8
    color: rowFill
    border.width: activeFocus ? 2 : (selected || activeMask || hovered ? theme.chromeBorderWidth : 0)
    border.color: activeFocus ? theme.focusRing
                              : selected || activeMask ? theme.alpha(rowAccent, 0.64)
                                                       : theme.alpha(theme.textMuted, 0.26)
    clip: true
    activeFocusOnTab: true
    Accessible.name: root.title
    Accessible.description: root.subtitle.length > 0 ? root.subtitle : root.stateSummary
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

        onClicked: root.clicked()
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        visible: root.selected || root.activeMask || root.solo || root.changed
        color: root.rowAccent
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
        anchors.topMargin: 7
        anchors.bottomMargin: 7
        spacing: 8
        z: 2

        Rectangle {
            objectName: "layerRowReorderHandle"
            Layout.preferredWidth: 8
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
            border.color: root.selected ? theme.alpha(root.thumbnailAccent, 0.52)
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
                cache: false
            }

            AppIcon {
                objectName: root.iconObjectName
                anchors.centerIn: parent
                visible: !root.thumbnailReady
                name: root.iconName
                width: root.compact ? 13 : 15
                height: width
                color: root.selected ? root.thumbnailAccent : theme.textMain
            }

            Rectangle {
                objectName: root.lockBadgeObjectName
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 13
                radius: 2
                visible: root.showLockBadge || root.locked
                color: theme.alpha(root.locked ? theme.textMuted : root.thumbnailAccent, 0.82)

                Label {
                    anchors.centerIn: parent
                    text: root.lockLabel
                    color: "#06100E"
                    font.family: theme.fontMono
                    font.pixelSize: 7
                    font.weight: Font.DemiBold
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    objectName: root.titleObjectName
                    Layout.fillWidth: true
                    text: root.title
                    color: root.selected ? root.accent : theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }

                Repeater {
                    model: root.badges

                    delegate: Rectangle {
	                        required property var modelData
	                        readonly property string badgeLabel: String(modelData.label || modelData)
	                        readonly property string text: badgeLabel
	                        readonly property bool badgeActive: modelData.active === true
                        readonly property color badgeAccent: modelData.accent || root.accent

                        objectName: String(modelData.objectName || "")
                        Layout.preferredWidth: Math.min(88, Math.max(38, badgeText.implicitWidth + 12))
                        Layout.preferredHeight: 18
                        radius: 3
                        color: badgeActive ? theme.alpha(badgeAccent, 0.16)
                                           : theme.alpha(theme.panelLift, 0.40)
                        border.width: theme.chromeBorderWidth
                        border.color: badgeActive ? theme.alpha(badgeAccent, 0.48)
                                                  : theme.alpha(theme.textMuted, 0.22)

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
                            text: badgeLabel
                            color: badgeActive ? badgeAccent : theme.textMuted
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
                }
            }

            Label {
                objectName: root.subtitleObjectName
                Layout.fillWidth: true
                text: root.subtitle.length > 0 ? root.subtitle : root.stateSummary
                color: root.selected || root.changed ? root.accent : theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 8
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                visible: text.length > 0
            }

            Label {
                objectName: root.metadataObjectName
                Layout.fillWidth: true
                text: root.metadata.length > 0 ? root.metadata : root.detail
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 8
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
                       : theme.alpha(theme.panelLift, 0.34)
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
                iconName: root.hidden ? "delete" : "media"
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
                width: implicitWidth
                height: implicitHeight
                enabled: root.menuItems.length > 0
                tooltipText: root.menuTooltip
                menuItems: root.menuItems
                onMenuItemTriggered: function(key) { root.menuItemTriggered(key) }
            }
        }
    }
}
