import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var searchSetup: null
    property var agent: null
    property var preview: null
    property var installPlan: ({})
    property string actionError: ""
    readonly property var models: agent ? agent.availableModels || [] : []
    readonly property var selectedModel: models.find(function(m) { return m.id === root.agent.selectedModelId }) || ({})
    spacing: theme.space16
    Theme { id: theme }
    function refreshPlan() { installPlan = agent ? agent.prepareSelectedModelInstall() : ({}) }
    Component.onCompleted: refreshPlan()
    Connections {
        target: root.agent
        function onSelectedModelChanged() { root.actionError = ""; root.refreshPlan() }
        function onAvailableModelsChanged() { root.refreshPlan() }
        function onModelInstallChanged() { root.refreshPlan() }
    }
    FolderDialog {
        id: searchImport
        title: "Choose the SigLIP 2 model folder"
        onAccepted: if (root.searchSetup) root.searchSetup.importFolder(selectedFolder)
    }
    component ModelLabel: Label {
        Layout.fillWidth: true
        color: theme.textMuted
        font.pixelSize: theme.typeBody
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    ModelLabel { text: "Photo search"; color: theme.textStrong; font.weight: Font.DemiBold }
    ModelLabel { text: "SigLIP 2 · matches descriptions to photos in the main search bar." }
    ModelLabel {
        objectName: "settingsSearchModelStatus"
        text: root.searchSetup ? root.searchSetup.status : "Search runtime unavailable in this build."
        color: root.searchSetup && root.searchSetup.ready ? theme.success : theme.textMuted
    }
    ProgressBar {
        Layout.fillWidth: true
        visible: root.searchSetup && root.searchSetup.running
        value: root.searchSetup ? root.searchSetup.progress : 0
        indeterminate: visible && value <= 0
    }
    Flow {
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: theme.space8
        ActionButton {
            objectName: "settingsSearchModelDownload"
            text: "Download · 1.54 GB"
            enabled: root.searchSetup && root.searchSetup.available && !root.searchSetup.running
            onClicked: root.searchSetup.download(true)
        }
        ActionButton {
            objectName: "settingsSearchModelImport"
            text: "Use local copy…"
            enabled: root.searchSetup && root.searchSetup.available && !root.searchSetup.running
            onClicked: searchImport.open()
        }
        ActionButton {
            objectName: "settingsSearchModelVerify"
            text: "Check files"
            enabled: root.searchSetup && root.searchSetup.available && !root.searchSetup.running
            onClicked: root.searchSetup.verify()
        }
        ActionButton {
            text: "Pause"
            visible: root.searchSetup && root.searchSetup.running
            onClicked: root.searchSetup.cancel()
        }
    }
    ModelLabel {
        text: root.searchSetup ? root.searchSetup.location : ""
        font.pixelSize: theme.typeCaption
        wrapMode: Text.WrapAnywhere
    }
    ModelLabel { text: "AI editing and assistant"; color: theme.textStrong; font.weight: Font.DemiBold }
    ToneComboBox {
        objectName: "settingsAgentModelPicker"
        Layout.fillWidth: true
        accessibleLabel: "AI editing and assistant model"
        model: root.models
        textRole: "name"
        currentIndex: Math.max(0, root.models.findIndex(function(m) { return m.id === root.agent.selectedModelId }))
        enabled: root.agent && !root.agent.running && !root.agent.modelInstallRunning
        onActivated: {
            root.agent.selectedModelId = root.models[currentIndex].id
            root.agent.probeSelectedModelAsync(true)
        }
    }
    ModelLabel {
        objectName: "settingsAgentModelStatus"
        text: root.actionError || (root.agent && root.agent.modelInstallRunning ? root.agent.modelInstallStatus
              : root.agent && root.agent.modelProbeRunning ? "Checking model…"
              : root.selectedModel.available ? "Ready · " + String(root.selectedModel.name || "")
              : String(root.selectedModel.statusReason || "Choose a model and check its connection."))
    }
    ModelLabel {
        visible: Boolean(root.installPlan.installable) && !root.selectedModel.available
        text: Number(root.installPlan.artifactBytes || 0) > 0
              ? "Download: " + (Number(root.installPlan.artifactBytes) / 1e9).toFixed(2) + " GB"
                + " · " + String(root.installPlan.license || "Local Ollama model")
              : "Downloaded by your local Ollama server."
    }
    ProgressBar {
        Layout.fillWidth: true
        visible: root.agent && root.agent.modelInstallRunning
        value: root.agent ? Number((root.agent.modelInstallProgress || {}).progress || 0) : 0
        indeterminate: visible && value <= 0
    }
    Flow {
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: theme.space8
        ActionButton {
            objectName: "settingsAgentModelCheck"
            text: "Check connection"
            enabled: root.agent && !root.agent.running && !root.agent.modelProbeRunning && !root.agent.modelInstallRunning
            onClicked: { root.agent.reloadModels(); root.agent.probeSelectedModelAsync(true) }
        }
        ActionButton {
            objectName: "settingsAgentModelDownload"
            text: "Download selected model"
            visible: Boolean(root.installPlan.installable) && !root.selectedModel.available
            enabled: root.agent && !root.agent.running && !root.agent.modelInstallRunning && !root.agent.modelProbeRunning
            onClicked: {
                if (root.installPlan.modelId !== root.agent.selectedModelId) { root.refreshPlan(); return }
                const result = root.agent.installSelectedModel(true)
                root.actionError = (result.errors || []).join("\n")
                root.refreshPlan()
            }
        }
        ActionButton {
            text: "Pause download"
            visible: root.agent && root.agent.modelInstallRunning
            onClicked: root.agent.cancelModelInstall()
        }
        ActionButton {
            text: "Retry runtime"
            visible: root.selectedModel.runtime === "managed" && root.selectedModel.runtimeState !== "ready"
            enabled: root.agent && !root.agent.running && !root.agent.modelInstallRunning
            onClicked: root.agent.retryLocalRuntime()
        }
    }
    ModelLabel { text: "Selection and object removal"; color: theme.textStrong; font.weight: Font.DemiBold }
    ModelLabel {
        objectName: "settingsSelectionModelStatus"
        text: root.preview ? root.preview.smartSelectionRuntimeLabel + " · "
              + (root.preview.aiSmartSelectionAvailable ? "Ready" : "Unavailable") : "Selection model unavailable."
    }
    ModelLabel {
        objectName: "settingsInpaintModelStatus"
        text: root.preview ? root.preview.inpaintRuntimeLabel + " · "
              + (root.preview.aiInpaintAvailable ? "Ready" : "Unavailable") : "Object removal model unavailable."
    }
    ModelLabel {
        text: "Photo search, selection, and object removal use the compatible models supplied for those tools. Assistant models can be changed above."
        font.pixelSize: theme.typeCaption
    }
}
