pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root
    objectName: "agentModelSetupDialog"
    required property var controller
    property var plan: ({})
    property string actionError: ""
    property Item focusReturnItem: null
    property Item focusFallbackItem: null
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
    readonly property var existingOllamaModel: {
        const models = controller ? controller.availableModels || [] : []
        return models.find(function(model) {
            return model.backend === "ollama" && model.runtime === "external" &&
                   String(model.model || "") === String(selectedModel.model || "")
        }) || ({})
    }
    readonly property bool externalModelMissing: selectedModel.runtime === "external" &&
        (String(selectedModel.statusReason || "").toLowerCase().indexOf("not found") >= 0 ||
         String(selectedModel.statusReason || "").toLowerCase().indexOf("does not exist") >= 0)
    modal: true
    focus: true
    anchors.centerIn: parent
    width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(520), parent ? parent.width - 40 : theme.scaled(520))))
    height: Math.min(popupAvailableHeight, Math.max(1, Math.min(contentItem.implicitHeight + padding * 2, parent ? parent.height - 40 : 640)))
    padding: standardPopupPadding
    closePolicy: Popup.CloseOnEscape
    Theme { id: theme }
    background: PopupSurface {
        Accessible.name: qsTr("Local AI setup")
        Accessible.description: qsTr("Review local model storage and setup status.")
        Accessible.role: Accessible.Dialog
        Accessible.ignored: false
    }
    function refreshPlan() {
        plan = controller ? controller.prepareSelectedModelInstall() : ({})
    }
    function present() {
        actionError = ""
        refreshPlan()
        open()
    }
    function focusForKeyboard(item) {
        if (!item || !item.visible || !item.enabled)
            return
        if (item.activeFocus && item["visualFocus"] === false)
            item.focus = false
        item.forceActiveFocus(Qt.TabFocusReason)
    }
    onAboutToShow: {
        const popupWindow = root.parent ? root.parent.Window.window : null
        if (!root.focusReturnItem && popupWindow)
            root.focusReturnItem = popupWindow.activeFocusItem
        if (!root.focusFallbackItem && popupWindow)
            root.focusFallbackItem = popupWindow.contentItem
    }
    onOpened: {
        refreshPlan()
        focusForKeyboard(closeButton)
    }
    onClosed: focusRestoreTimer.restart()
    Timer {
        id: focusRestoreTimer
        interval: 1
        repeat: false
        onTriggered: {
            if (root.focusReturnItem && root.focusReturnItem.visible && root.focusReturnItem.enabled) {
                root.focusForKeyboard(root.focusReturnItem)
            } else if (root.focusFallbackItem && root.focusFallbackItem.visible && root.focusFallbackItem.enabled) {
                root.focusForKeyboard(root.focusFallbackItem)
            }
            root.focusReturnItem = null
        }
    }
    Connections {
        target: root.controller
        function onAvailableModelsChanged() { if (root.visible) root.refreshPlan() }
        function onModelInstallChanged() { if (root.visible) root.refreshPlan() }
    }
    contentItem: ColumnLayout {
        spacing: theme.gapUnrelated
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "Local AI setup"
                font.family: theme.fontUi
                font.pixelSize: theme.typeDialogTitle
                font.weight: Font.DemiBold
                color: theme.textStrong
                wrapMode: Text.WordWrap
            }
            ActionButton {
                id: closeButton
                objectName: "modelSetupCloseButton"
                text: "Close"
                compact: true
                onClicked: root.close()
            }
        }
        ScrollView {
            objectName: "modelSetupDetailsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: modelDetails.implicitHeight
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: modelDetails
                width: parent.width
                spacing: theme.gapRelated
                Label {
                    Layout.fillWidth: true
                    text: String(root.selectedModel.name || "Selected vision model")
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeSection
                    font.weight: Font.DemiBold
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: theme.textStrong
                }
                Label {
                    objectName: "modelSetupStorageDetail"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    textFormat: Text.PlainText
                    text: root.plan.runtime === "managed"
                        ? (Number(root.plan.artifactBytes || 0) / 1e9).toFixed(1) + " GB model · " + String(root.plan.license || "License unavailable")
                          + (Number(root.plan.estimatedCachedBytes || 0) > 0
                              ? "\n" + (Number(root.plan.estimatedCachedBytes) / 1e9).toFixed(1) + " GB cached; checked before reuse."
                              : "")
                          + "\nApproximately " + (Number(root.plan.requiredFreeBytes || 0) / 1e9).toFixed(2) + " GB free needed for remaining data and headroom."
                          + "\nStored on this Mac. Images stay local. Every model file is checked before use."
                        : root.selectedModel.available ? "Using your existing local Ollama server. No additional model download is needed."
                        : "Connect to your existing local Ollama server. Its model storage is managed by Ollama."
                    color: theme.textMain
                }
                Label {
                    objectName: "modelSetupStatusDetail"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    textFormat: Text.PlainText
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
            }
        }
        ProgressStrip {
            Layout.fillWidth: true
            visible: root.downloading || root.verifying
            progress: root.verifying || Number(root.installProgress.progress || 0) === 0
                ? -1 : Number(root.installProgress.progress || 0)
        }
        Flow {
            objectName: "modelSetupActions"
            Layout.fillWidth: true
            spacing: theme.gapControl
            visible: root.downloading || !root.selectedModel.available
                     || (root.selectedModel.runtime === "managed" && root.selectedModel.runtimeState !== "ready")
            ActionButton {
                objectName: "modelSetupUseExistingOllamaButton"
                visible: root.selectedModel.runtime === "managed" && Boolean(root.existingOllamaModel.id)
                text: "Use existing Ollama"
                tone: "primary"
                enabled: !root.downloading && !root.checkingModel
                onClicked: {
                    root.actionError = ""
                    root.controller.selectedModelId = root.existingOllamaModel.id
                    root.controller.probeSelectedModelAsync(true)
                }
            }
            ActionButton {
                visible: root.selectedModel.runtime === "managed" && root.selectedModel.runtimeState !== "ready"
                text: "Retry runtime"
                iconName: "retry"
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
                tone: "primary"
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
                tone: "primary"
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
