pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../workspaces"

ColumnLayout {
    id: root
    required property var bridge
    required property var timelineDocument
    required property var catalogModel
    property string requestedTab: "adjust"
    property string deliveryWarning: ""
    readonly property bool mixerSampling: inspectorLoader.item !== null && inspectorLoader.item.colorMixerImageDragActive
    property bool mixerPending: false
    property bool mixerDragging: false
    property point mixerStart: Qt.point(0, 0)
    property point mixerPointer: Qt.point(0, 0)
    property real mixerStartHue: 0
    property real mixerStartSaturation: 0
    onMixerSamplingChanged: root.bridge.colorMixerSampling = mixerSampling
    Component.onDestruction: root.bridge.colorMixerSampling = false
    signal videoInputRequested()
    signal comparisonRequested(string mode)
    spacing: theme.space6
    Theme { id: theme }

    function activateTab(tab) {
        root.requestedTab = String(tab || "adjust")
        if (inspectorLoader.item)
            inspectorLoader.item.activateEditInspectorTab(root.requestedTab)
    }

    function samplePressed(x, y, pointerX, pointerY) {
        if (root.bridge.samplingTarget === "color-mixer") {
            mixerPending = true
            mixerStart = Qt.point(pointerX, pointerY)
            mixerPointer = mixerStart
        }
        root.bridge.sampleProgramAt(x, y)
    }
    function sampleMoved(x, y, width, height) {
        mixerPointer = Qt.point(x, y)
        if (!mixerDragging || !inspectorLoader.item) return
        inspectorLoader.item.setPrimaryAdjustmentValue("hueAdjustment",
            Math.max(-100, Math.min(100, mixerStartHue + (x - mixerStart.x) * 200 / Math.max(1, width))))
        inspectorLoader.item.setPrimaryAdjustmentValue("saturationAdjustment",
            Math.max(-100, Math.min(100, mixerStartSaturation - (y - mixerStart.y) * 200 / Math.max(1, height))))
    }
    function sampleReleased() {
        if (mixerPending || mixerDragging) root.bridge.finishColorMixerGesture()
        mixerPending = false
        mixerDragging = false
    }
    Connections {
        target: root.bridge.editor
        function onColorMixerSampleCompleted(success) {
            if (!root.mixerPending) return
            root.mixerPending = false
            root.mixerDragging = success
            if (success && inspectorLoader.item) {
                inspectorLoader.item.setPrimaryStringAdjustmentValue("hslChannel", root.bridge.editor.hslChannel)
                root.mixerStartHue = inspectorLoader.item.primaryColorMixerChannelValue("hue", root.bridge.editor.hueAdjustment)
                root.mixerStartSaturation = inspectorLoader.item.primaryColorMixerChannelValue("saturation", root.bridge.editor.saturationAdjustment)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: root.bridge.targetLabel
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeBody
            wrapMode: Text.Wrap
        }
        MenuButton {
            text: "More"
            primaryOpensMenu: true
            compact: true
            menuItems: [
                {key: "input", label: "Video input and finishing LUT", icon: "swatch"},
                {key: "wipe", label: "Compare with original · Wipe", icon: "compare"},
                {key: "side-by-side", label: "Compare side by side", icon: "compare"}
            ]
            onMenuItemTriggered: function(key) {
                if (key === "input") root.videoInputRequested()
                else root.comparisonRequested(key)
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: root.bridge.samplingTarget.length > 0
        text: "Click Program to sample a color. Press Escape to cancel."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }

    Label {
        Layout.fillWidth: true
        visible: root.deliveryWarning.length > 0
        text: root.deliveryWarning
        color: theme.warning
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }

    Label {
        Layout.fillWidth: true
        visible: !root.bridge.ready
        text: root.bridge.loading ? "Preparing clip color controls…"
              : root.bridge.error.length > 0 ? root.bridge.error
              : "Select one photo, video, or adjustment clip to edit its colors and effects."
        color: root.bridge.error.length > 0 ? theme.error : theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeBody
        wrapMode: Text.Wrap
    }

    Loader {
        id: inspectorLoader
        Layout.fillWidth: true
        Layout.fillHeight: true
        active: root.bridge.ready
        visible: active
        onLoaded: Qt.callLater(function() { root.activateTab(root.requestedTab) })
        sourceComponent: StudioWorkspace {
            id: inspector
            objectName: "timelineSharedEditInspector"
            page: "edit"
            inspectorOnly: true
            inspectorReadOnly: !root.bridge.editable
            workspaceActive: false
            catalogAssets: root.catalogModel
            editorControllerOverride: root.bridge.editor
            historyShortcutsHandledByHost: true
            undoEditHandler: function() { root.bridge.undo() }
            redoEditHandler: function() { root.bridge.redo() }
            resetEditHandler: function() { root.bridge.resetGrade() }
            canUndoEdit: root.timelineDocument.canUndo
            canRedoEdit: root.timelineDocument.canRedo
            enabled: root.bridge.editable
            onEditInspectorTabChanged: {
                if (editInspectorTab === "filters") {
                    const index = selectedEditTargetScope() !== "base-image" && selectedEditTargetLayerIndex() >= 0
                            ? selectedEditTargetLayerIndex() : root.bridge.ensureEffectsLayer()
                    if (index >= 0) setActivePhotoLayerSelection(index, false)
                }
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: root.bridge.ready && (root.bridge.error.length > 0 || !root.bridge.editable)
        text: root.bridge.error.length > 0 ? root.bridge.error : "Unlock this clip's track to edit its colors and effects."
        color: theme.error
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }
}
