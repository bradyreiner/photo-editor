pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    implicitWidth: theme.scaled(300)
    Layout.minimumWidth: 0
    required property var bridge
    readonly property var editor: bridge ? bridge.editor : null
    readonly property var layers: editor ? editor.photoLayers : []
    signal layerToolsRequested()
    spacing: theme.space6
    Theme { id: theme }

    // Keep live controls and their gestures alive when C++ publishes new values.
    ListModel { id: effectLayers; dynamicRoles: true }
    function syncLayers() {
        if (!root.editor) return
        const rows = []
        for (let i = 0; i < root.layers.length; ++i) {
            const layer = root.layers[i]
            if (root.editor.photoLayerEffects(i).length > 0)
                rows.push({layerId: String(layer.id), layerIndex: i, layerData: layer})
        }
        let same = effectLayers.count === rows.length
        for (let i = 0; same && i < rows.length; ++i)
            same = effectLayers.get(i).layerId === rows[i].layerId
        if (!same) {
            effectLayers.clear()
            for (let i = 0; i < rows.length; ++i) effectLayers.append(rows[i])
        } else {
            for (let i = 0; i < rows.length; ++i) {
                effectLayers.setProperty(i, "layerIndex", rows[i].layerIndex)
                effectLayers.setProperty(i, "layerData", rows[i].layerData)
            }
        }
    }
    onLayersChanged: Qt.callLater(root.syncLayers)
    Component.onCompleted: root.syncLayers()

    Label {
        Layout.fillWidth: true
        visible: !root.bridge.ready || root.bridge.error.length > 0
        text: root.bridge.loading ? qsTr("Loading effect controls…")
              : root.bridge.error.startsWith("Still-photo grading currently supports")
                ? qsTr("This effect needs a video or adjustment clip. The photo clip is unchanged.")
              : root.bridge.error.length > 0 ? root.bridge.error
              : qsTr("Select a photo, video, or adjustment clip.")
        color: root.bridge.error.length > 0 ? theme.error : theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }
    Repeater {
        model: effectLayers
        delegate: ColumnLayout {
            id: effectLayer
            required property string layerId
            required property int layerIndex
            required property var layerData
            property int observedEffectCount: 0
            Layout.fillWidth: true
            Layout.preferredWidth: root.width
            Layout.minimumWidth: 0
            spacing: theme.space4
            Label {
                visible: effectLayers.count > 1
                Layout.fillWidth: true
                Layout.preferredWidth: root.width
                text: String(effectLayer.layerData.name || qsTr("Layer"))
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                elide: Text.ElideRight
            }
            PhotoEffectStack {
                objectName: "timelineSharedLayerEffectStack"
                Layout.fillWidth: true
                Layout.preferredWidth: root.width
                controller: root.editor
                layerIndex: effectLayer.layerIndex
                layerData: effectLayer.layerData
                locked: !root.bridge.editable || effectLayer.layerData.locked === true
                compactCards: true
                timelineStyle: true
                headerVisible: false
                addButtonVisible: false
                onEffectMaskEditRequested: root.layerToolsRequested()
                onEffectsChanged: {
                    const added = effects.length > effectLayer.observedEffectCount
                    effectLayer.observedEffectCount = effects.length
                    if (added) Qt.callLater(function() {
                        if (effects.length > 0)
                            expandedEffectId = String(effects[effects.length - 1].id)
                    })
                }
            }
        }
    }
    ActionButton {
        objectName: "timelineSharedEffectLayerTools"
        text: qsTr("Layers and masks")
        iconName: "layers"
        compact: true
        enabled: root.bridge.ready
        tooltipText: qsTr("Open the full layer and mask tools for this clip.")
        onClicked: root.layerToolsRequested()
    }
}
