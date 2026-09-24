pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    objectName: "agentModelSetupDialog"
    required property var controller
    property var plan: ({})
    property string actionError: ""
    readonly property var selectedModel: {
        const models = controller ? (controller.availableModels || []) : []
        for (let i = 0; i < models.length; ++i)
            if (models[i].id === controller.selectedModelId) return models[i]
        return ({})
    }
    readonly property bool downloading: controller && controller.modelInstallRunning
    readonly property var installProgress: controller ? (controller.modelInstallProgress || {}) : ({})
    readonly property bool installForSelectedModel: String(installProgress.ollamaModel || "") === String(selectedModel.model || "")
    readonly property bool verifying: String((selectedModel.artifactVerification || {}).state || "") === "verifying"
    readonly property bool checkingModel: controller && controller.modelProbeRunning
    readonly property bool externalModelMissing: selectedModel.runtime === "external" &&
        (String(selectedModel.statusReason || "").toLowerCase().indexOf("not found") >= 0 ||
         String(selectedModel.statusReason || "").toLowerCase().indexOf("does not exist") >= 0)
    modal: true
    anchors.centerIn: parent
    width: Math.min(520, parent ? parent.width - 40 : 520)
    padding: 24
    closePolicy: Popup.CloseOnEscape
    Theme { id: theme }
    background: Rectangle { color: theme.ink900; radius: 24; border.color: theme.lineSoft }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 260; easing.type: Easing.OutCubic }
        }
    }
    function refreshPlan() {
        plan = controller ? controller.prepareSelectedModelInstall() : ({})
    }
    function present() {
        actionError = ""
        refreshPlan()
        open()
    }
    onOpened: refreshPlan()
    Connections {
        target: root.controller
        function onAvailableModelsChanged() { if (root.visible) root.refreshPlan() }
        function onModelInstallChanged() { if (root.visible) root.refreshPlan() }
    }
    contentItem: ColumnLayout {
        spacing: 18
        Label { text: "Local AI setup"; font.pixelSize: 24; color: theme.textStrong }
        Label {
            Layout.fillWidth: true
            text: String(root.selectedModel.name || "Selected vision model")
            font.pixelSize: 17
            color: theme.textStrong
        }
        Label {
            objectName: "modelSetupStorageDetail"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.plan.runtime === "managed"
                ? (Number(root.plan.artifactBytes || 0) / 1e9).toFixed(1) + " GB model · " + String(root.plan.license || "License unavailable")
                  + (Number(root.plan.estimatedCachedBytes || 0) > 0
                      ? "\n" + (Number(root.plan.estimatedCachedBytes) / 1e9).toFixed(1) + " GB cached; checked before reuse."
                      : "")
                  + "\nApproximately " + (Number(root.plan.requiredFreeBytes || 0) / 1e9).toFixed(2) + " GB free needed for remaining data and headroom."
                  + "\nStored on this Mac. Images stay local. Every model file is checked before use."
                : "Downloads into your selected local server. Its model storage and integrity are managed externally."
            color: theme.textMain
        }
        Label {
            objectName: "modelSetupStatusDetail"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.actionError || (root.selectedModel.available ? "Model ready."
                : root.checkingModel ? "Checking your existing Ollama model…"
                : root.downloading ? root.controller.modelInstallStatus
                : root.verifying ? "Verifying model files… You can keep editing."
                : root.installForSelectedModel && root.installProgress.status === "failed"
                    ? String(root.controller.modelInstallOutput || root.controller.modelInstallStatus || "Download failed. Retry when ready.")
                : root.installForSelectedModel && root.installProgress.status === "paused"
                    ? String(root.controller.modelInstallStatus || "Download paused; retry to resume")
                : root.selectedModel.runtime === "external" && !root.externalModelMissing
                    ? (String(root.selectedModel.statusReason || "").length > 0
                        ? String(root.selectedModel.statusReason) + "\nYour existing Ollama model will not be downloaded again."
                        : "Ollama is not responding. Start it locally; your existing model will not be downloaded again.")
                    : (root.plan.errors || []).join("\n") || String(root.selectedModel.statusReason || "Ready to download."))
            color: theme.textMain
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: root.downloading || root.verifying
            indeterminate: root.verifying || Number(root.installProgress.progress || 0) === 0
            value: Number(root.installProgress.progress || 0)
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { text: "Close"; onClicked: root.close() }
            Item { Layout.fillWidth: true }
            ActionButton {
                visible: root.selectedModel.runtime === "managed" && root.selectedModel.runtimeState !== "ready"
                text: "Retry runtime"
                enabled: !root.downloading
                onClicked: root.controller.retryLocalRuntime()
            }
            ActionButton {
                visible: root.downloading
                text: "Pause download"
                onClicked: root.controller.cancelModelInstall()
            }
            ActionButton {
                visible: root.selectedModel.runtime === "external" &&
                    !root.selectedModel.available && !root.externalModelMissing
                text: root.checkingModel ? "Checking…" : "Check Ollama"
                enabled: !root.checkingModel
                onClicked: {
                    root.actionError = ""
                    root.controller.probeSelectedModelAsync(true)
                }
            }
            ActionButton {
                objectName: "confirmModelDownloadButton"
                visible: !root.downloading && !root.selectedModel.available &&
                    (root.selectedModel.runtime === "managed" || root.externalModelMissing)
                text: "Download model"
                enabled: Boolean(root.plan.installable) && !root.verifying
                onClicked: {
                    if (root.plan.modelId !== root.controller.selectedModelId) {
                        root.refreshPlan()
                        return
                    }
                    const result = root.controller.installSelectedModel(true)
                    root.actionError = (result.errors || []).join("\n")
                    root.refreshPlan()
                }
            }
        }
    }
}
