import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property bool selected: false
    property bool activeMask: false
    property bool locked: false
    property bool hidden: false
    property bool solo: false
    property bool changed: false
    property bool compact: false
    property bool accentVisible: selected || activeMask || solo || changed
    property color accent: theme.accentWarm400
    property color mutedAccent: theme.textMuted
    property string rowKind: "layer"
    property string targetKind: "layer"
    property bool revealAnimated: true
    property string thumbnailSource: ""

    Theme { id: theme }

    readonly property bool hovered: rowHover.hovered
    readonly property int motionDuration: Math.max(120, theme.durationFast)
    readonly property int chromeRowHeight: compact ? 58 : 74
    readonly property color activeMaskAccent: theme.accentCool400
    readonly property color rowAccent: selected ? accent
                                       : activeMask ? activeMaskAccent
                                       : solo ? theme.warning
                                       : changed ? accent
                                       : mutedAccent
    readonly property color rowFill: !enabled ? theme.alpha(theme.glassBase, 0.26)
                                 : hidden ? theme.alpha(theme.panelColor, 0.46)
                                 : selected || activeMask ? theme.alpha(rowAccent, 0.11)
                                 : solo ? theme.alpha(rowAccent, 0.09)
                                 : changed ? theme.alpha(rowAccent, 0.075)
                                 : hovered ? theme.alpha(theme.glassLift, 0.30)
                                 : theme.alpha(theme.panelLift, 0.44)
    readonly property string rowStateSummary: {
        let parts = []
        if (locked)
            parts.push("locked")
        if (hidden)
            parts.push("hidden")
        if (solo)
            parts.push("solo")
        if (changed)
            parts.push("changed")
        if (activeMask)
            parts.push("mask")
        if (selected)
            parts.push("selected")
        return parts.join("|")
    }
    readonly property string rowChromeContract: "layer-row-chrome:v1|kind:" +
                                                rowKind +
                                                "|target:" +
                                                targetKind +
                                                "|states:" +
                                                rowStateSummary +
                                                "|hover:" +
                                                String(hovered)
    readonly property string zoneContract: "layer-row-chrome-zones:v1|accent-edge|thumbnail|content|state-summary|row-actions|nested-children"
    readonly property real thumbnailDevicePixelRatio: Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
    readonly property int screenAwareThumbnailSourceSize: Math.round(Math.max(1, (compact ? 34 : 42) * thumbnailDevicePixelRatio))
    readonly property string thumbnailDprContract: "layer-row-thumbnail-dpr:v1|source-size:screen-aware|base-image:true|display:" +
                                                   String(compact ? 34 : 42) +
                                                   "|source:" +
                                                   String(screenAwareThumbnailSourceSize)
    readonly property string premiumLayerContract: "premium-layer-chrome:v2|stable-height:true|nested-mask-relation:true|soft-selected-fill:true|single-left-accent:true|active-mask-secondary:true|quiet-hover:true|focus-ring:true"
    readonly property string layerInteractionAcceptanceContract: "layer-interaction-acceptance:v1|select:true|rename:true|reorder:true|visibility:true|opacity:true|mask-select:true|transform:true|escape:true"
    readonly property string statePriorityContract: "layer-row-state-priority:v2|selected-layer-primary>active-mask-secondary>solo>changed>locked>hidden>hover"
    readonly property string activeMaskIndicatorContract: "layer-active-mask-indicator:v1|secondary-edge:right|accent:cool|visible:" +
                                                          String(root.activeMask) +
                                                          "|selected-does-not-hide:true"
    readonly property string interactionContract: "layer-row-interaction:v1|hover-actions:true|keyboard-actions:true|disabled-reasons:true|rename:properties|reorder:move-buttons-or-menu|delete:separated"
    readonly property string motionContract: "layer-row-motion:v1|duration:" + String(motionDuration) +
                                             "|reveal:subtle|hover-fill:true|accent-fade:true|height-stable:true|layout-shift:false"
    readonly property bool contextualActionsVisible: hovered || selected || activeMask

    Layout.fillWidth: true
    Layout.preferredHeight: chromeRowHeight
    radius: theme.radius6
    color: rowFill
    border.width: selected || activeMask || solo || hovered || locked ? theme.chromeBorderWidth : 0
    border.color: solo ? theme.alpha(rowAccent, 0.78)
                       : selected || activeMask || hovered ? theme.alpha(rowAccent, 0.56)
                       : locked ? theme.alpha(theme.textMuted, 0.42)
                       : theme.alpha(theme.textMuted, 0.24)
    clip: true
    opacity: enabled ? 1.0 : 0.58
    activeFocusOnTab: true
    Accessible.name: rowKind + " " + rowStateSummary
    Accessible.description: targetKind
    Accessible.role: Accessible.ListItem
    Accessible.selected: root.selected || root.activeMask

    HoverHandler {
        id: rowHover
    }

    Image {
        id: thumbnailDprProbe
        visible: false
        source: root.thumbnailSource
        sourceSize.width: root.screenAwareThumbnailSourceSize
        sourceSize.height: root.screenAwareThumbnailSourceSize
        asynchronous: true
        cache: false
    }

    Behavior on color {
        ColorAnimation { duration: root.motionDuration; easing.type: Easing.OutCubic }
    }

    Behavior on border.color {
        ColorAnimation { duration: root.motionDuration; easing.type: Easing.OutCubic }
    }

    Behavior on opacity {
        NumberAnimation { duration: root.motionDuration; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        radius: 2
        visible: root.accentVisible
        color: root.rowAccent
        z: 1
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        visible: root.activeMask
        color: root.activeMaskAccent
        opacity: root.selected ? 0.92 : 1.0
        z: 1
    }
}
