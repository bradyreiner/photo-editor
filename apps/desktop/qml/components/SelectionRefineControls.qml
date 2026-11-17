pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var controller
    property bool brushActive: false
    property var beginSelectionEdit: null
    property bool coalescing: false
    property Item advancedHost: null
    readonly property string disabledReason: edges.locked === true
        ? qsTr("Unlock the edge target before refining it.") : String(controller.activeMaskEditDisabledReason || "")
    readonly property var edges: controller.selectionEdgeControls
    property bool edgeAware: false
    readonly property bool currentEdgeAware: brushActive
        ? controller.activeMaskBrushAutoMask && controller.activeMaskBrushProtectEdges : edgeAware
    signal brushRequested(bool subtract, bool edgeAware)
    signal inspectRequested()
    Theme { id: theme }
    spacing: theme.space6

    function ensureSession() {
        return beginSelectionEdit ? beginSelectionEdit() : controller.beginSelectionEditSession()
    }
    function beginInteraction() {
        if (!coalescing && ensureSession()) {
            controller.beginCoalescedEdit()
            coalescing = true
        }
    }
    function finishInteraction() {
        if (coalescing) {
            coalescing = false
            controller.endCoalescedEdit()
        }
    }
    function writeValue(key, value) {
        if (ensureSession()) {
            if (["outline", "marching-ants", "hidden"].indexOf(controller.maskPreviewDisplayMode) >= 0)
                controller.maskPreviewMode = "overlay"
            controller.maskRefinementPreviewMode = "refined"
            controller.setSelectionEdgeAdjustment(key, value)
        }
    }
    onVisibleChanged: if (!visible) finishInteraction()
    Component.onDestruction: if (controller) finishInteraction()

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.space4
        MiniButton {
            objectName: "selectionRefineAddBrush"
            Layout.fillWidth: true
            text: qsTr("Add")
            iconName: "brush-tool"
            showLeadingIcon: true
            role: "segmented"
            active: root.brushActive && root.controller.activeMaskOperation !== "subtract"
            enabled: root.disabledReason.length === 0
            disabledReason: root.disabledReason
            tooltip: qsTr("Paint to include an area.")
            onClicked: root.brushRequested(false, root.currentEdgeAware)
        }
        MiniButton {
            objectName: "selectionRefineRemoveBrush"
            Layout.fillWidth: true
            text: qsTr("Remove")
            iconName: "brush-subtract"
            showLeadingIcon: true
            role: "segmented"
            active: root.brushActive && root.controller.activeMaskOperation === "subtract"
            enabled: root.disabledReason.length === 0
            disabledReason: root.disabledReason
            tooltip: qsTr("Paint to remove an area.")
            onClicked: root.brushRequested(true, root.currentEdgeAware)
        }
        MenuButton {
            objectName: "selectionRefineViewMenu"
            text: qsTr("View")
            compact: true
            primaryOpensMenu: true
            menuItems: [
                {key: "view:overlay", label: qsTr("Overlay"), checked: root.controller.maskPreviewMode === "overlay"},
                {key: "view:outline", label: qsTr("Outline"), checked: ["outline", "marching-ants"].indexOf(root.controller.maskPreviewMode) >= 0},
                {key: "view:white-on-black", label: qsTr("Mask"), checked: root.controller.maskPreviewMode === "white-on-black"},
                {key: "view:hidden", label: qsTr("Hide preview"), checked: root.controller.maskPreviewDisplayMode === "hidden"},
                {key: "view:on-white", label: qsTr("On light background"), checked: root.controller.maskPreviewMode === "on-white"},
                {key: "view:on-black", label: qsTr("On dark background"), checked: root.controller.maskPreviewMode === "on-black"},
                {key: "inspect", label: qsTr("Inspect edges at 100%")},
                {key: "edge-aware", label: qsTr("Edge-aware brush"), checked: root.currentEdgeAware, separatorBefore: true},
                {key: "original", label: qsTr("Show original edges"), checked: root.controller.maskRefinementPreviewMode === "original", separatorBefore: true},
                {key: "quality", label: qsTr("High quality preview"), checked: root.controller.maskRefinementHighQualityPreview},
                {key: "full", label: qsTr("Full resolution preview"), checked: root.controller.maskFullResolutionPreview},
                {key: "reset", label: qsTr("Reset edges"), icon: "reset", enabled: root.disabledReason.length === 0, disabledReason: root.disabledReason, separatorBefore: true}
            ]
            onMenuItemTriggered: function(key) {
                if (key.indexOf("view:") === 0) root.controller.maskPreviewMode = key.slice(5)
                else if (key === "inspect") root.inspectRequested()
                else if (key === "edge-aware") {
                    root.edgeAware = !root.currentEdgeAware
                    if (root.brushActive) root.brushRequested(root.controller.activeMaskOperation === "subtract", root.edgeAware)
                }
                else if (key === "original") root.controller.maskRefinementPreviewMode = root.controller.maskRefinementPreviewMode === "original" ? "refined" : "original"
                else if (key === "quality") root.controller.maskRefinementHighQualityPreview = !root.controller.maskRefinementHighQualityPreview
                else if (key === "full") root.controller.maskFullResolutionPreview = !root.controller.maskFullResolutionPreview
                else if (key === "reset") root.writeValue("reset", 0)
            }
        }
    }
    InspectorSlider {
        objectName: "selectionQuickSoftness"
        Layout.fillWidth: true
        label: qsTr("Feather")
        readonly property bool pixels: Number(root.edges.featherPixelScale || 0) > 0
        from: 0; to: pixels ? Number(root.edges.featherPixelsMax) : 0.5
        stepSize: pixels ? 0.25 : 0.005; defaultValue: 0
        value: Number(pixels ? root.edges.featherPixels : root.edges.feather || 0)
        valueText: pixels ? value.toFixed(1) + " px" : Math.round(value * 100) + "%"
        Accessible.description: String(root.edges.featherPixelDescription || "")
        enabled: root.disabledReason.length === 0
        disabledReason: root.disabledReason
        onInteractionStarted: root.beginInteraction()
        onInteractionFinished: root.finishInteraction()
        onValueDragged: function(value) { root.writeValue(pixels ? "featherPixels" : "feather", value) }
        onResetRequested: root.writeValue("feather", 0)
    }
    ColumnLayout {
        parent: root.advancedHost || root
        visible: root.advancedHost !== null
        Layout.fillWidth: true
        spacing: theme.space6
        Text {
            Layout.fillWidth: true
            text: qsTr("Edges: %1").arg(String(root.edges.name || qsTr("Selection")))
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            elide: Text.ElideRight
        }
        InspectorSlider {
            objectName: "selectionQuickEdgeShift"
            Layout.fillWidth: true
            label: qsTr("Shift edge")
            from: -100; to: 100; stepSize: 1; defaultValue: 0
            value: Number(root.edges.shift || 0)
            valueText: Math.round(value) + "%"
            enabled: root.disabledReason.length === 0
            disabledReason: root.disabledReason
            onInteractionStarted: root.beginInteraction()
            onInteractionFinished: root.finishInteraction()
            onValueDragged: function(value) { root.writeValue("shift", value) }
            onResetRequested: root.writeValue("shift", 0)
        }
    }
}
