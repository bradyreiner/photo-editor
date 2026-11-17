import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore

Item {
    id: root
    property var currentSettings: ({})
    property string presetFamily: "paint"
    property string selectedName: ""
    property string selectedSignature: ""
    readonly property bool canSaveSettings: currentSettings.textureMaterial !== "photo_fragment" || !!currentSettings.tipPresetName
    readonly property var savedPresets: {
        try {
            const entries = JSON.parse(preferences.savedBrushes)
            return Array.isArray(entries) ? entries.filter(p => p && typeof p.name === "string" && p.settings).slice(0, 12) : []
        } catch (_) { return [] }
    }
    property alias previewVisible: preferences.previewVisible
    signal presetSelected(var settings)
    Theme { id: theme }
    Settings {
        id: preferences
        category: "BrushTools/" + root.presetFamily
        property string savedBrushes: "[]"
        property bool previewVisible: true
    }
    implicitWidth: theme.scaled(130)
    implicitHeight: picker.implicitHeight

    function choose(name, settings) {
        root.presetSelected(settings)
        root.selectedName = name
        root.selectedSignature = JSON.stringify(root.currentSettings)
        presetPopup.closeTransient(true)
    }
    function savePreset(name) {
        const cleanName = String(name).trim().slice(0, 32)
        if (!cleanName || !canSaveSettings) return false
        const next = savedPresets.filter(p => p.name.toLowerCase() !== cleanName.toLowerCase())
        if (next.length >= 12) return false
        next.push({name: cleanName, settings: JSON.parse(JSON.stringify(currentSettings))})
        preferences.savedBrushes = JSON.stringify(next)
        selectedName = cleanName
        selectedSignature = JSON.stringify(currentSettings)
        return true
    }
    function deletePreset(name) {
        preferences.savedBrushes = JSON.stringify(savedPresets.filter(p => p.name !== name))
        if (selectedName === name) selectedName = ""
    }
    ActionButton {
        id: picker
        objectName: "brushPresetPickerButton"
        anchors.fill: parent
        text: root.selectedSignature === JSON.stringify(root.currentSettings) && root.selectedName
              ? root.selectedName : qsTr("Presets")
        iconName: "chevron-down"
        showLeadingIcon: true
        allowLabelWrap: true
        active: presetPopup.visible
        Accessible.name: qsTr("Brush presets")
        onClicked: presetPopup.toggleTransient(picker)
    }
    AppPopup {
        id: presetPopup
        objectName: "brushPresetPopup"
        parent: Overlay.overlay
        transientMenu: true
        focus: true
        width: Math.min(popupAvailableWidth, theme.scaled(320))
        x: parent ? Math.max(theme.space8, Math.min(parent.width - width - theme.space8, picker.mapToItem(parent, 0, 0).x)) : 0
        y: parent ? Math.max(theme.space8, Math.min(parent.height - height - theme.space8, picker.mapToItem(parent, 0, picker.height).y + theme.space4)) : 0
        onOpened: presetName.text = ""
        contentItem: ScrollView {
            implicitHeight: presetContents.implicitHeight
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: presetContents
                width: presetPopup.availableWidth
                spacing: theme.space6
                Label {
                    text: qsTr("Brush presets")
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    font.weight: theme.weightControl
                }
                Repeater {
                    model: [
                        {name: qsTr("Hard round"), detail: qsTr("Crisp, opaque marks"), hardness: 1, flow: 1, opacity: 1},
                        {name: qsTr("Soft round"), detail: qsTr("Gentle edges and buildup"), hardness: 0, flow: 0.35, opacity: 1},
                        {name: qsTr("Soft shading"), detail: qsTr("Subtle, layered coverage"), hardness: 0.1, flow: 0.12, opacity: 0.65}
                    ]
                    delegate: ActionButton {
                        required property var modelData
                        required property int index
                        objectName: "brushBuiltinPreset" + index
                        Layout.fillWidth: true
                        text: modelData.name
                        allowLabelWrap: true
                        role: "quiet"
                        Accessible.description: modelData.detail
                        onClicked: root.choose(modelData.name, {hardness: modelData.hardness,
                            flow: modelData.flow, brushOpacity: modelData.opacity, brushSpacing: 0.1,
                            textureMaterial: "solid", pressureSize: true, pressureFlow: false})
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Your brushes · %1/12").arg(root.savedPresets.length)
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                }
                Repeater {
                    model: root.savedPresets
                    delegate: RowLayout {
                        id: savedRow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: theme.space2
                        ActionButton {
                            objectName: "brushSavedPreset" + savedRow.index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: savedRow.modelData.name
                            allowLabelWrap: true
                            role: "quiet"
                            onClicked: root.choose(savedRow.modelData.name, savedRow.modelData.settings)
                        }
                        MiniButton {
                            objectName: "brushDeletePreset" + savedRow.index
                            square: true
                            iconName: "delete"
                            role: "quiet"
                            Accessible.name: qsTr("Delete brush %1").arg(savedRow.modelData.name)
                            tooltip: Accessible.name
                            onClicked: root.deletePreset(savedRow.modelData.name)
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.savedPresets.length === 0
                    text: qsTr("Save your current tip and stroke settings to use again.")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    ToneTextField {
                        id: presetName
                        objectName: "brushPresetName"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        placeholderText: qsTr("Name this brush")
                        maximumLength: 32
                        Accessible.name: qsTr("New brush preset name")
                        onAccepted: if (root.savePreset(text)) { text = ""; presetPopup.closeTransient(true) }
                    }
                    MiniButton {
                        objectName: "brushSavePreset"
                        text: qsTr("Save")
                        role: "primary"
                        enabled: root.canSaveSettings && presetName.text.trim().length > 0 && (root.savedPresets.length < 12 ||
                            root.savedPresets.some(p => p.name.toLowerCase() === presetName.text.trim().toLowerCase()))
                        onClicked: if (root.savePreset(presetName.text)) { presetName.text = ""; presetPopup.closeTransient(true) }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: !root.canSaveSettings ? qsTr("Save this photo fragment as a reusable tip in More settings first.")
                        : root.presetFamily === "selection" ? qsTr("Saved on this device. Add/erase mode stays as you chose it.")
                        : root.presetFamily === "retouch" ? qsTr("Saved on this device. Retouch tool and source stay as you chose them.")
                        : qsTr("Saved on this device. Color and paint/erase mode stay as you chose them.")
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
