import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import PhotoEditor
import "../components"

Rectangle {
    id: root
    objectName: "editWorkspace"

    color: "transparent"
    focus: true

    Theme { id: theme }

    property var catalogAssets: null
    property string editTool: "adjust"
    readonly property var fallbackAgentEditController: ({
        availableModels: [],
        availableModelNames: [],
        availableStyles: [],
        selectedModelId: "",
        selectedStyleId: "",
        userInstructions: "",
        recipeBaseMode: "current",
        running: false,
        modelInstallRunning: false,
        modelInstallStatus: "",
        draftActive: false,
        awaitingUserInput: false,
        pendingUserQuestions: [],
        safeStopReason: "",
        status: "Agent editing unavailable.",
        errorMessage: "",
        lastRunReport: ({}),
        runHistory: [],
        mcpEndpoint: "",
        mcpStatus: "",
        mcpEndpointKind: "local",
        mcpAvailable: false,
        mcpToolCount: 0,
        mcpTotalClientCount: 0,
        mcpConnectedClientCount: 0,
        mcpLastToolCallName: "",
        mcpLastToolCallStatus: "",
        aiEditWorkflowState: function() { return ({}) },
        analyzeSelected: function() {},
        answerPendingUserInput: function() {},
        applyEditForScope: function() {},
        applyEditForSelected: function() {},
        applySampleReviewToAll: function() {},
        acceptReviewIssue: function() {},
        cancelRun: function() {},
        commitDraft: function() {},
        createLearnedStyleProfileFromSelection: function() { return ({ ok: false, errors: ["Agent unavailable"] }) },
        createStyle: function() { return ({ ok: false, errors: ["Agent unavailable"] }) },
        dismissPendingAgentInput: function() {},
        exportMemoryVault: function() { return ({ ok: false, errors: ["Agent unavailable"] }) },
        exportPreflightForSelected: function() {},
        fixAllReviewIssues: function() {},
        fixReviewIssue: function() {},
        installSelectedModel: function() { return ({ ok: false, errors: ["Agent unavailable"] }) },
        memoryVaultStatus: function() { return ({}) },
        openMemoryVault: function() { return ({ ok: false, errors: ["Agent unavailable"] }) },
        openMemoryVaultInObsidian: function() { return ({ ok: false, errors: ["Agent unavailable"] }) },
        prepareSelectedModelInstall: function() { return ({}) },
        probeSelectedModel: function() { return ({ ok: false, errors: ["Agent unavailable"] }) },
        queueExportForSelected: function() {},
        resetMemoryVault: function() { return ({ ok: false, errors: ["Agent unavailable"] }) },
        reviseSampleReview: function() {},
        rollbackDraft: function() {},
        rollbackReviewIssue: function() {},
        runDraftForScope: function() {},
        runDraftOnSelected: function() {},
        setupLocalAi: function() {},
        validateBatchForSelected: function() {}
    })
    readonly property var agentEditController: typeof agentEdit !== "undefined" ? agentEdit : fallbackAgentEditController
    readonly property bool hasAgentEditController: agentEditController !== fallbackAgentEditController
    readonly property string editVisibilityContract: "edit-visibility:v1|always:tool-rail-image-stage-canvas-toolbar-inspector-filmstrip-target-context|contextual:active-tool-hud-active-variant-layer-mask-target-pending-apply-cancel-bottom-actions|one-expansion:tool-variants-presets-compare-view-modes-secondary-layer-actions-advanced-settings|hover-focus:row-actions-reorder-visibility-lock-quick-actions|diagnostic:preview-errors-agent-runtime-model-logs|preserve:functionality"
    readonly property string editSurfaceContract: "edit-surfaces:v1|stage:dominant-image|tools:visible-expandable-switchable|toolbar:view-and-actions|inspector:targeted-adjust-mask-layer|filmstrip:stable|bottom-actions:rating-flag-export|pending:apply-cancel-visible|no-elide:true|no-overlap:true"
    readonly property string editWorkflowPolishContract: "edit-workflow-polish:v1|first-glance:tool-rail-image-stage-inspector-filmstrip|close-inspection:active-tool-variant-target-hud-pending-actions|clutter:one-active-hud-one-primary-action-diagnostics-collapsed|interaction:select-expand-switch-commit-cancel-customize|text:no-elide-no-overlap|functionality:preserved"
    readonly property string editPageCompletionContract: "page-completion:v1|edit|rail:primary-tool-families|content:image-stage-toolbar-hud|inspector:active-tool-targeted-sections|filmstrip:stable-selection|bottom:rating-flag-export|states:no-photo-active-tool-pending-disabled-error"

    property real manualZoom: 1.0
    property bool fitToView: true
    property bool leftPanelCollapsed: true
    property bool filmstripCollapsed: false
    property bool inspectorCollapsed: false
    property bool inspectorWide: false
    property string transientMaskTool: ""
    property bool optionModifierActive: false
    property bool controlModifierActive: false
    property bool spacePanActive: false
    property bool maskColorRangeSampling: false
    property bool maskLuminanceRangeSampling: false
    property bool maskDepthRangeSampling: false
    property bool selectionBrushRestorePending: false
    property bool selectionBrushRestoreSubtract: false
    readonly property bool objectPickArmed: transientMaskTool === "smart" ||
                                            transientMaskTool === "smart-subtract" ||
                                            transientMaskTool === "smart-new" ||
                                            transientMaskTool === "remove" ||
                                            transientMaskTool === "remove-new"
    readonly property bool objectPickCreateNew: transientMaskTool === "smart-new" ||
                                                transientMaskTool === "remove-new"
    readonly property string objectPickMode: transientMaskTool === "smart-subtract" ||
                                             (optionModifierActive &&
                                              transientMaskTool !== "smart-new" &&
                                              transientMaskTool !== "remove" &&
                                              transientMaskTool !== "remove-new" &&
                                              transientMaskTool.indexOf("smart") === 0)
                                             ? "selection-subtract"
                                             : (transientMaskTool === "remove" || transientMaskTool === "remove-new"
                                                ? "remove"
                                                : "selection")
    readonly property bool polygonDrawArmed: transientMaskTool === "polygon"
    readonly property bool lassoDrawArmed: transientMaskTool === "lasso"
    readonly property bool cloneSourcePickArmed: transientMaskTool === "clone-source" ||
                                                  (optionModifierActive &&
                                                   !controlModifierActive &&
                                                   editorPreview.activeMaskKind === "brush" &&
                                                   editorPreview.activeMaskRetouchMode === "clone" &&
                                                   !objectPickArmed &&
                                                   !polygonDrawArmed &&
                                                   !lassoDrawArmed)
    property real selectionOutlinePhase: 0
    readonly property int selectionOutlineAnimationPointBudget: 1800
    readonly property string selectionOutlineAnimationContract: "visibility-gated|motion-budgeted|point-budgeted:v1"
    readonly property bool selectionOutlineAnimationEnabled: selectionOutlineAnimationShouldRun()
    property string cropGuideMode: "Thirds"
    property string compareViewMode: "side-by-side"
    property real compareWipePosition: 0.5
    property string editedDisplaySource: ""
    property string referenceDisplaySource: ""
    property string presetDraftName: ""
    property string presetDraftPack: "User Presets"
    property string selectedPresetName: ""
    property string agentModelSetupMessage: ""
    property string agentMemoryVaultMessage: ""
    property bool agentMemoryVaultResetArmed: false
    property bool histogramShowLuminance: true
    property bool histogramShowRgb: true
    property bool histogramLogScale: true
    property bool scopePanelsExpanded: true
    property var hslChannels: [
        {
            name: "Reds",
            color: "#d45a62"
        },
        {
            name: "Oranges",
            color: "#dd8a39"
        },
        {
            name: "Yellows",
            color: "#d7bd42"
        },
        {
            name: "Greens",
            color: "#61b56f"
        },
        {
            name: "Aquas",
            color: "#49b8c4"
        },
        {
            name: "Blues",
            color: "#4e86d9"
        },
        {
            name: "Purples",
            color: "#8b6be3"
        },
        {
            name: "Magentas",
            color: "#c85ac1"
        }
    ]
    property var adjustmentGroups: [
        { "key": "raw", "label": "RAW" },
        { "key": "tone", "label": "Tone" },
        { "key": "color", "label": "Color" },
        { "key": "crop", "label": "Crop" },
        { "key": "masks", "label": "Masks" },
        { "key": "detail", "label": "Detail" }
    ]
    property var selectedAdjustmentGroups: ["raw", "tone", "color", "crop", "masks", "detail"]
    property string inspectorMode: "adjust"
    property string inspectorTab: "adjust"
    property bool workspaceActive: true
    property alias editInspectorTab: root.inspectorTab
    property alias studioPresetDraftName: root.presetDraftName
    property alias studioPresetSearchText: root.presetDraftName
    readonly property bool editingChromeVisible: true
    readonly property bool editInspectorPanelVisible: !inspectorCollapsed
    readonly property int editInspectorPanelWidth: inspectorWide ? 420 : 356
    property bool batchToolsExpanded: false
    property bool agentStyleBuilderExpanded: false
    property bool agentDetailsExpanded: false
    property string agentExpandedReviewIssueAssetId: ""
    property string agentRunMode: "draft"
    property string agentStyleDraftName: ""
    property string agentStyleDraftGoal: ""
    property string agentStylePermissionMode: "balanced"
    property var agentStylePermissionModeOptions: [
        { "mode": "conservative", "label": "Conservative" },
        { "mode": "balanced", "label": "Balanced" },
        { "mode": "full_edit", "label": "Full edit" },
        { "mode": "full_edit_retouch", "label": "Full edit + retouch" }
    ]
    property bool agentStyleAllowRaw: true
    property bool agentStyleAllowGlobalColor: true
    property bool agentStyleAllowCurves: true
    property bool agentStyleAllowHsl: true
    property bool agentStyleAllowColorGrading: true
    property bool agentStyleAllowDetail: true
    property bool agentStyleAllowCrop: true
    property bool agentStyleAllowMasks: true
    property bool agentStyleAllowRetouch: false
    property bool agentStyleAllowLayersLuts: false
    property bool agentStyleAllowExport: false
    property string agentStyleMaxExposureShift: "1.2"
    property string agentStyleMaxTemperatureShift: "1800"
    property string agentStyleMaxSaturationShift: "18"
    property string agentStyleHighlightClipLimit: "0.5"
    property string agentStyleShadowClipLimit: "1.0"
    property string agentStyleSaveMessage: ""

    signal assetActivated(string destination)

    Loader {
        id: referenceImageDialogLoader
        active: false
        sourceComponent: referenceImageDialogComponent
    }

    Component {
        id: referenceImageDialogComponent

        FileDialog {
            title: "Choose Reference Image"
            nameFilters: ["Images (*.jpg *.jpeg *.png *.tif *.tiff *.heic *.heif *.webp *.dng *.raw)", "All files (*)"]
            onAccepted: {
                if (editorPreview.setReferenceImage(selectedFile))
                    editorPreview.compareMode = true
            }
        }
    }
    onInspectorModeChanged: {
        if (inspectorMode === "adjust" && inspectorTab !== "adjust" && inspectorTab !== "scopes" && inspectorTab !== "info")
            inspectorTab = "adjust"
        resetInspectorScroll()
        if (inspectorMode === "crop" && editorPreview.hasAsset)
            editorPreview.cropEditing = true
        else if (inspectorMode !== "crop" && editorPreview.cropEditing)
            editorPreview.cropEditing = false
    }
    onInspectorTabChanged: resetInspectorScroll()

    function clampZoom(value) {
        return Math.max(0.25, Math.min(4.0, value));
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function boundedAgentNumber(text, fallback, minimum, maximum) {
        const parsed = Number(String(text || "").trim())
        if (!isFinite(parsed))
            return fallback
        return clamp(parsed, minimum, maximum)
    }

    function agentSelectedModelIndex() {
        for (let index = 0; index < root.agentEditController.availableModels.length; ++index) {
            const modelInfo = root.agentEditController.availableModels[index]
            if (modelInfo.id === root.agentEditController.selectedModelId)
                return index
        }
        return root.agentEditController.availableModels.length > 0 ? 0 : -1
    }

    function activeAgentModel() {
        const selectedIndex = agentSelectedModelIndex()
        if (selectedIndex >= 0 && selectedIndex < root.agentEditController.availableModels.length)
            return root.agentEditController.availableModels[selectedIndex]
        return null
    }

    function openReferenceImageDialog() {
        referenceImageDialogLoader.active = true
        if (referenceImageDialogLoader.item)
            referenceImageDialogLoader.item.open()
    }

    function agentModelStatusText() {
        const modelInfo = activeAgentModel()
        if (!modelInfo)
            return "No local model"
        const name = modelInfo.name || modelInfo.id || "Local model"
        const status = modelInfo.status || (modelInfo.available ? "available" : "unavailable")
        const backend = modelInfo.backend || "local"
        return name + " · " + status + " · " + backend
    }

    function agentMcpStatusText() {
        if (!root.hasAgentEditController || String(root.agentEditController.mcpEndpoint || "").length === 0)
            return "MCP · unavailable"
        const status = String(root.agentEditController.mcpStatus || "Ready")
        const count = Number(root.agentEditController.mcpToolCount || 0)
        const kind = String(root.agentEditController.mcpEndpointKind || "local")
        return "MCP · " + status + " · " + Math.round(count).toLocaleString(Qt.locale(), "f", 0) + (count === 1 ? " tool" : " tools") + " · " + kind
    }

    function agentMcpStatusColor() {
        if (!root.hasAgentEditController || !root.agentEditController.mcpAvailable)
            return theme.error
        const status = String(root.agentEditController.mcpStatus || "").toLowerCase()
        return status === "failed" || status === "unavailable" ? theme.error : theme.success
    }

    function agentMcpTelemetryText() {
        if (!root.hasAgentEditController || !root.agentEditController.mcpAvailable)
            return ""
        const total = Number(root.agentEditController.mcpTotalClientCount || 0)
        const active = Number(root.agentEditController.mcpConnectedClientCount || 0)
        const lastName = String(root.agentEditController.mcpLastToolCallName || "")
        const lastStatus = String(root.agentEditController.mcpLastToolCallStatus || "")
        const parts = []
        if (total > 0)
            parts.push("clients " + Math.round(active).toLocaleString(Qt.locale(), "f", 0) + "/" +
                       Math.round(total).toLocaleString(Qt.locale(), "f", 0))
        if (lastName.length > 0)
            parts.push("last " + lastName + (lastStatus.length > 0 ? " · " + lastStatus : ""))
        return parts.join(" · ")
    }

    function agentModelStatusColor(modelInfo) {
        if (!modelInfo)
            return theme.error
        const status = String(modelInfo.status || (modelInfo.available ? "available" : "unavailable")).toLowerCase()
        if (status === "available")
            return theme.success
        if (status === "loading" || status === "busy")
            return theme.accentCool500
        if (status === "failed" || status === "unavailable")
            return theme.error
        return theme.textMuted
    }

    function agentModelStatusLabel(modelInfo) {
        if (!modelInfo)
            return "unavailable"
        return String(modelInfo.status || (modelInfo.available ? "available" : "unavailable")).toLowerCase()
    }

    function agentModelDetailText(modelInfo) {
        if (!modelInfo)
            return ""
        const backend = modelInfo.backend || "local"
        const configured = modelInfo.configured ? "configured" : "built-in"
        const reason = String(modelInfo.statusReason || "").trim()
        return reason.length > 0 ? (backend + " · " + configured + " · " + reason) : (backend + " · " + configured)
    }

    function activeAgentModelIsOllama() {
        const modelInfo = activeAgentModel()
        return modelInfo && String(modelInfo.backend || "") === "ollama"
    }

    function agentModelInstallCommandText() {
        if (!activeAgentModelIsOllama())
            return ""
        const setup = root.agentEditController.prepareSelectedModelInstall()
        return String(setup.command || "")
    }

    function updateAgentModelSetupMessage(result) {
        if (!result) {
            agentModelSetupMessage = ""
            return
        }
        const errors = result.errors || []
        if (errors.length > 0) {
            agentModelSetupMessage = String(errors[0])
            return
        }
        if (result.started)
            agentModelSetupMessage = String(root.agentEditController.modelInstallStatus || "Pulling model")
        else if (result.mode === "model-probe")
            agentModelSetupMessage = result.ok ? "Model available" : String(result.statusReason || "Model unavailable")
        else if (result.command)
            agentModelSetupMessage = String(result.command)
        else
            agentModelSetupMessage = String(result.reason || root.agentEditController.modelInstallStatus || "")
    }

    function agentRunArtifactPath(key) {
        const report = root.agentEditController.lastRunReport || ({})
        if (report[key] !== undefined && String(report[key]).length > 0)
            return String(report[key])
        const artifacts = report.artifacts || ({})
        if (artifacts[key] !== undefined && String(artifacts[key]).length > 0)
            return String(artifacts[key])
        return ""
    }

    function localImageUrl(path) {
        const value = String(path || "")
        if (value.length === 0)
            return ""
        if (value.indexOf("file:") === 0 || value.indexOf("qrc:") === 0 || value.indexOf("image:") === 0)
            return value
        return "file://" + value
    }

    function agentProofPath(role) {
        if (role === "before")
            return agentRunArtifactPath("beforeProofPath")
        if (role === "after")
            return agentRunArtifactPath("afterProofPath")
        const current = agentRunArtifactPath("currentProofPath")
        if (current.length > 0)
            return current
        const after = agentRunArtifactPath("afterProofPath")
        if (after.length > 0)
            return after
        return agentRunArtifactPath("beforeProofPath")
    }

    function agentProofSource(role) {
        return localImageUrl(agentProofPath(role))
    }

    function agentHasProofSurface() {
        return agentProofPath("current").length > 0 ||
               agentProofPath("before").length > 0 ||
               agentProofPath("after").length > 0
    }

    function agentRecipeDiff() {
        const report = root.agentEditController.lastRunReport || ({})
        if (report.recipeDiff !== undefined && report.recipeDiff !== null)
            return report.recipeDiff
        const validation = report.validationReport || ({})
        if (validation.recipeDiff !== undefined && validation.recipeDiff !== null)
            return validation.recipeDiff
        return ({})
    }

    function agentDiffGroupLabel(groupKey) {
        const labels = ({
            "globalTone": "Tone",
            "raw": "RAW",
            "whiteBalance": "White balance",
            "curves": "Curves",
            "color": "Color",
            "hsl": "HSL / mixer",
            "blackWhite": "Black and white",
            "detail": "Detail",
            "crop": "Crop / transform",
            "masks": "Masks",
            "maskStack": "Masks",
            "retouch": "Retouch",
            "layers": "Layers / LUTs",
            "export": "Export"
        })
        return labels[groupKey] || String(groupKey)
    }

    function agentFormatDiffValue(value) {
        if (value === undefined || value === null)
            return "none"
        if (typeof value === "number")
            return Math.abs(value) >= 10 ? value.toFixed(1) : value.toFixed(2)
        if (typeof value === "boolean")
            return value ? "on" : "off"
        if (Array.isArray(value))
            return value.length + (value.length === 1 ? " item" : " items")
        if (typeof value === "object")
            return "changed"
        const text = String(value)
        return text.length > 0 ? text : "none"
    }

    function agentDiffEntrySummary(entry) {
        if (!entry || typeof entry !== "object")
            return "changed"
        if (entry.before !== undefined || entry.after !== undefined)
            return agentFormatDiffValue(entry.before) + " -> " + agentFormatDiffValue(entry.after)
        if (entry.count !== undefined)
            return agentFormatDiffValue(entry.count)
        return "changed"
    }

    function agentRecipeDiffGroups() {
        const diff = agentRecipeDiff()
        const groups = []
        const keys = Object.keys(diff)
        for (let index = 0; index < keys.length; ++index) {
            const key = keys[index]
            const group = diff[key]
            const fields = group && typeof group === "object" ? Object.keys(group) : []
            if (fields.length === 0)
                continue
            const summaries = []
            const limit = Math.min(fields.length, 3)
            for (let fieldIndex = 0; fieldIndex < limit; ++fieldIndex) {
                const field = fields[fieldIndex]
                summaries.push(field + ": " + agentDiffEntrySummary(group[field]))
            }
            if (fields.length > limit)
                summaries.push("+" + (fields.length - limit) + " more")
            groups.push({
                "key": key,
                "label": agentDiffGroupLabel(key),
                "count": fields.length,
                "summary": summaries.join(" · ")
            })
        }
        return groups
    }

    function agentHasRecipeDiff() {
        return agentRecipeDiffGroups().length > 0
    }

    function agentAssetReports() {
        const report = root.agentEditController.lastRunReport || ({})
        const summary = report.batchSummary || ({})
        if (summary.reviewIssues !== undefined &&
                summary.reviewIssues !== null &&
                summary.reviewIssues.length > 0)
            return summary.reviewIssues
        if (report.assetReports !== undefined && report.assetReports !== null)
            return report.assetReports
        if (String(report.mode || "") === "commit" &&
                report.assetIds !== undefined &&
                report.assetIds !== null) {
            const committedReports = []
            for (let index = 0; index < report.assetIds.length; ++index)
                committedReports.push({ "assetId": report.assetIds[index], "status": "committed" })
            return committedReports
        }
        return []
    }

    function agentBatchReportWarningCount(report) {
        if (!report || report.warnings === undefined || report.warnings === null)
            return 0
        return report.warnings.length
    }

    function agentBatchReportErrorCount(report) {
        if (!report || report.errors === undefined || report.errors === null)
            return 0
        return report.errors.length
    }

    function agentBatchReportStatus(report) {
        const rawStatus = String((report && report.status) || "").toLowerCase()
        if (agentBatchReportErrorCount(report) > 0 || rawStatus === "failed" || (report && report.ok === false))
            return "failed"
        if (rawStatus === "needs_review")
            return "review"
        if (agentBatchReportWarningCount(report) > 0 && rawStatus !== "failed")
            return "warning"
        if (rawStatus.length > 0)
            return rawStatus
        return "pending"
    }

    function agentBatchStatusColor(status) {
        const value = String(status || "").toLowerCase()
        if (value === "drafted" || value === "validated" || value === "committed")
            return theme.success
        if (value === "review")
            return theme.warning
        if (value === "warning")
            return theme.warning
        if (value === "failed")
            return theme.error
        return theme.textMuted
    }

    function agentBatchStatusSummaryText() {
        const report = root.agentEditController.lastRunReport || ({})
        const summary = report.batchSummary || ({})
        const reviewSummary = summary.reviewSummary || ({})
        if (reviewSummary.editedCount !== undefined) {
            return Number(reviewSummary.editedCount || 0) + " edited · " +
                   Number(reviewSummary.passedCount || 0) + " passed · " +
                   Number(reviewSummary.needsReviewCount || 0) + " review · " +
                   Number(reviewSummary.failedCount || 0) + " failed"
        }
        if (summary.statusCounts !== undefined && summary.statusCounts !== null) {
            const summaryParts = []
            const summaryOrder = ["pending", "drafted", "validated", "warning", "failed", "committed"]
            for (let index = 0; index < summaryOrder.length; ++index) {
                const status = summaryOrder[index]
                const count = Number(summary.statusCounts[status] || 0)
                if (count > 0)
                    summaryParts.push(count + " " + status)
            }
            if (summaryParts.length > 0)
                return summaryParts.join(" · ")
            if (summary.assetCount !== undefined && summary.assetCount > 0)
                return summary.assetCount + (summary.assetCount === 1 ? " asset" : " assets")
        }
        const reports = agentAssetReports()
        const counts = ({ "pending": 0, "drafted": 0, "validated": 0, "warning": 0, "failed": 0, "committed": 0 })
        for (let index = 0; index < reports.length; ++index) {
            const status = agentBatchReportStatus(reports[index])
            if (counts[status] === undefined)
                counts[status] = 0
            counts[status] += 1
        }
        const labels = ({
            "pending": "pending",
            "drafted": "drafted",
            "validated": "validated",
            "warning": "warning",
            "failed": "failed",
            "committed": "committed"
        })
        const order = ["pending", "drafted", "validated", "warning", "failed", "committed"]
        const parts = []
        for (let index = 0; index < order.length; ++index) {
            const status = order[index]
            if (counts[status] > 0)
                parts.push(counts[status] + " " + labels[status])
        }
        if (parts.length > 0)
            return parts.join(" · ")
        if (root.agentEditController.running && catalogAssets.selectedAssetCount > 1)
            return catalogAssets.selectedAssetCount + " pending"
        return ""
    }

    function agentFixAllReviewIssuesAvailable() {
        const report = root.agentEditController.lastRunReport || ({})
        const summary = report.batchSummary || ({})
        const reviewSummary = summary.reviewSummary || ({})
        return Boolean(reviewSummary.fixAllOutliersAvailable)
    }

    function agentHasBatchStatus() {
        return agentAssetReports().length > 0 ||
               (root.agentEditController.running && catalogAssets.selectedAssetCount > 1)
    }

    function agentBatchReportAssetLabel(report, index) {
        const assetId = String((report && report.assetId) || "")
        return assetId.length > 0 ? assetId : "Asset " + (index + 1)
    }

    function agentBatchReportDetail(report) {
        if (report && report.reason !== undefined && String(report.reason || "").length > 0)
            return String(report.reason || "")
        const parts = []
        const warningCount = agentBatchReportWarningCount(report)
        const errorCount = agentBatchReportErrorCount(report)
        if (errorCount > 0)
            parts.push(errorCount + (errorCount === 1 ? " error" : " errors"))
        if (warningCount > 0)
            parts.push(warningCount + (warningCount === 1 ? " warning" : " warnings"))
        if (report && report.revisionCount !== undefined && report.revisionCount > 0)
            parts.push(report.revisionCount + (report.revisionCount === 1 ? " revision" : " revisions"))
        if (parts.length > 0)
            return parts.join(" · ")
        return agentBatchReportStatus(report) === "pending" ? "waiting for agent" : "ready"
    }

    function agentBatchReportSuggestedAction(report) {
        return String((report && report.suggestedAction) || "")
    }

    function agentBatchReportThumbnailSource(report) {
        if (!report)
            return ""
        const path = String(report.thumbnailPath || report.afterProofPath || report.currentProofPath || "")
        return localImageUrl(path)
    }

    function agentReviewIssueExpanded(report) {
        const assetId = String((report && report.assetId) || "")
        return assetId.length > 0 && agentExpandedReviewIssueAssetId === assetId
    }

    function toggleAgentReviewIssueDetails(report) {
        const assetId = String((report && report.assetId) || "")
        agentExpandedReviewIssueAssetId = agentExpandedReviewIssueAssetId === assetId ? "" : assetId
    }

    function agentReviewIssueDetailsText(report) {
        const issue = report || ({})
        const parts = []
        if (issue.styleMatchScorePercent !== undefined && issue.styleMatchScorePercent !== null &&
                String(issue.styleMatchScorePercent).length > 0)
            parts.push("style " + Number(issue.styleMatchScorePercent).toLocaleString(Qt.locale(), "f", 0) + "%")
        if (Number(issue.warningCount || 0) > 0)
            parts.push(Number(issue.warningCount || 0) + " warning" + (Number(issue.warningCount || 0) === 1 ? "" : "s"))
        if (Number(issue.errorCount || 0) > 0)
            parts.push(Number(issue.errorCount || 0) + " error" + (Number(issue.errorCount || 0) === 1 ? "" : "s"))
        if (String(issue.clusterId || "").length > 0)
            parts.push("cluster " + String(issue.clusterId || ""))
        if (Boolean(issue.outlierCorrected))
            parts.push("outlier corrected")
        const proofPath = String(issue.afterProofPath || issue.thumbnailPath || "")
        if (proofPath.length > 0)
            parts.push(proofPath)
        return parts.join(" · ")
    }

    function agentRunStages() {
        return [
            { "key": "analyze", "label": "Analyze" },
            { "key": "plan", "label": "Plan" },
            { "key": "draft-recipe", "label": "Draft recipe" },
            { "key": "render-proof", "label": "Render proof" },
            { "key": "validate", "label": "Validate" },
            { "key": "revise", "label": "Revise" },
            { "key": "batch-sync", "label": "Batch sync" },
            { "key": "outlier-correction", "label": "Outlier correction" },
            { "key": "export-preflight", "label": "Export preflight" },
            { "key": "ready-review", "label": "Ready for review" }
        ]
    }

    function agentToolCalls() {
        const report = root.agentEditController.lastRunReport || ({})
        return report.toolCalls !== undefined && report.toolCalls !== null ? report.toolCalls : []
    }

    function agentEditLoops() {
        const report = root.agentEditController.lastRunReport || ({})
        if (report.editLoops !== undefined && report.editLoops !== null)
            return report.editLoops
        const loops = []
        const reports = report.assetReports || []
        for (let index = 0; index < reports.length; ++index) {
            const loop = reports[index] && reports[index].editLoop !== undefined ? reports[index].editLoop : null
            if (loop)
                loops.push(loop)
        }
        return loops
    }

    function agentLoopAttemptRows() {
        const loops = agentEditLoops()
        const rows = []
        for (let loopIndex = 0; loopIndex < loops.length; ++loopIndex) {
            const loop = loops[loopIndex] || ({})
            const attempts = loop.attempts || []
            for (let attemptIndex = 0; attemptIndex < attempts.length && rows.length < 6; ++attemptIndex) {
                const attempt = attempts[attemptIndex] || ({})
                rows.push({
                    "label": "Attempt " + (Number(attempt.attemptIndex || attemptIndex) + 1),
                    "status": String(attempt.stopReasonCandidate || loop.finalStopReason || "checked"),
                    "score": Number(attempt.qualityScorePercent || Math.round(Number(attempt.qualityScore || 0) * 100)),
                    "warningCount": attempt.warnings !== undefined && attempt.warnings !== null ? attempt.warnings.length : 0,
                    "needsUserReview": Boolean(attempt.needsUserReview),
                    "assetId": String(loop.assetId || "")
                })
            }
        }
        return rows
    }

    function agentLoopSummaryText() {
        const loops = agentEditLoops()
        if (loops.length === 0)
            return ""
        const first = loops[0] || ({})
        const score = Number(first.finalScorePercent || Math.round(Number(first.finalScore || 0) * 100))
        const attemptCount = Number(first.attemptCount || (first.attempts ? first.attempts.length : 0))
        const review = first.needsUserReview ? "review" : "ready"
        return attemptCount + (attemptCount === 1 ? " attempt" : " attempts") +
               " · score " + score + "% · " +
               String(first.finalStopReason || review)
    }

    function agentLoopStatusColor(row) {
        const status = String((row && row.status) || "").toLowerCase()
        if (status.indexOf("failed") >= 0 || status.indexOf("invalid") >= 0)
            return theme.error
        if ((row && row.needsUserReview) || status.indexOf("warning") >= 0 ||
                status.indexOf("limit") >= 0 || status.indexOf("review") >= 0)
            return theme.warning
        if (status.indexOf("clean") >= 0 || status.indexOf("validated") >= 0)
            return theme.success
        return theme.textMuted
    }

    function agentToolCallStatusColor(call) {
        const status = String((call && call.status) || "").toLowerCase()
        if (status === "failed" || status === "error")
            return theme.error
        if (status === "warning")
            return theme.warning
        if (status === "ok" || status === "complete" || status === "completed" || status === "success")
            return theme.success
        return theme.textMuted
    }

    function agentToolCallTitle(call) {
        const name = String((call && call.name) || "tool")
        const status = String((call && call.status) || "recorded")
        return name + " · " + status
    }

    function agentToolCallDetail(call) {
        const outcome = call && call.outcome !== undefined ? call.outcome : ({})
        const parts = []
        const durationMs = Number((call && call.durationMs) || outcome.durationMs || 0)
        const warningCount = Number(outcome.warningCount || 0)
        const errorCount = Number(outcome.errorCount || 0)
        if (durationMs > 0)
            parts.push(durationMs + " ms")
        if (warningCount > 0)
            parts.push(warningCount + (warningCount === 1 ? " warning" : " warnings"))
        if (errorCount > 0)
            parts.push(errorCount + (errorCount === 1 ? " error" : " errors"))
        if (outcome.proofPath !== undefined && String(outcome.proofPath).length > 0)
            parts.push(String(outcome.proofPath))
        return parts.length > 0 ? parts.join(" · ") : "completed"
    }

    function agentRunWarnings() {
        const report = root.agentEditController.lastRunReport || ({})
        const validation = report.validationReport || ({})
        const warnings = []
        const addWarnings = function(items) {
            if (!items)
                return
            for (let index = 0; index < items.length; ++index) {
                const text = String(items[index] || "")
                if (text.length > 0 && warnings.indexOf(text) < 0)
                    warnings.push(text)
            }
        }
        addWarnings(report.warnings)
        addWarnings(validation.warnings)
        return warnings
    }

    function agentPendingQuestionText() {
        const questions = root.agentEditController.pendingUserQuestions || []
        const parts = []
        for (let index = 0; index < questions.length; ++index) {
            const question = questions[index]
            if (typeof question === "string") {
                if (question.length > 0)
                    parts.push(question)
            } else {
                const text = String((question && (question.question || question.text || question.prompt)) || "")
                if (text.length > 0)
                    parts.push(text)
            }
        }
        const report = root.agentEditController.lastRunReport || ({})
        if (parts.length === 0 && String(report.planSummary || "").length > 0)
            parts.push(String(report.planSummary || ""))
        return parts.join("\n")
    }

    function agentHasPendingInputSurface() {
        return root.agentEditController.awaitingUserInput ||
               String(root.agentEditController.safeStopReason || "").length > 0 ||
               String((root.agentEditController.lastRunReport || ({})).safeStopReason || "").length > 0
    }

    function agentRunArtifactSummary() {
        const parts = []
        const current = agentProofPath("current")
        const before = agentProofPath("before")
        const after = agentProofPath("after")
        const manifest = agentRunArtifactPath("manifestPath")
        const shareable = agentRunArtifactPath("shareableManifestPath")
        if (current.length > 0)
            parts.push("proof " + current)
        if (before.length > 0)
            parts.push("before " + before)
        if (after.length > 0)
            parts.push("after " + after)
        if (manifest.length > 0)
            parts.push("manifest " + manifest)
        if (shareable.length > 0)
            parts.push("shareable " + shareable)
        return parts.join(" · ")
    }

    function agentStyleCitationCount() {
        const report = root.agentEditController.lastRunReport || ({})
        const citations = report.styleConstraintCitations || []
        return citations.length || 0
    }

    function agentRunEvidenceSummary() {
        const report = root.agentEditController.lastRunReport || ({})
        const validation = report.validationReport || ({})
        const parts = []
        const toolCount = agentToolCalls().length
        const diffCount = agentRecipeDiffGroups().length
        const citationCount = agentStyleCitationCount()
        const warningCount = agentRunWarnings().length
        if (toolCount > 0)
            parts.push(toolCount + " tool call" + (toolCount === 1 ? "" : "s"))
        if (diffCount > 0)
            parts.push(diffCount + " recipe group" + (diffCount === 1 ? "" : "s"))
        if (validation.ok !== undefined)
            parts.push(validation.ok ? "validation passed" : "validation failed")
        if (warningCount > 0)
            parts.push(warningCount + " warning" + (warningCount === 1 ? "" : "s"))
        if (citationCount > 0)
            parts.push(citationCount + " style citation" + (citationCount === 1 ? "" : "s"))
        if (agentLoopSummaryText().length > 0)
            parts.push(agentLoopSummaryText())
        if (String(report.shareableManifestPath || "").length > 0)
            parts.push("shareable report ready")
        return parts.join(" · ")
    }

    function agentThinkingLines() {
        const report = root.agentEditController.lastRunReport || ({})
        const draftReport = report.draftReport || ({})
        const state = agentWorkflowState()
        const lines = []
        const addLine = function(text) {
            const value = String(text || "").trim()
            if (value.length > 0 && lines.indexOf(value) < 0)
                lines.push(value)
        }

        if (root.agentEditController.running)
            addLine("Working: " + String(root.agentEditController.status || state.statusLine || "planning the edit"))
        if (root.agentEditController.awaitingUserInput)
            addLine("Waiting on you: " + agentPendingQuestionText())

        addLine(report.planSummary)
        addLine(draftReport.planSummary)
        addLine(report.analysisSummary)

        const goals = report.editGoals || draftReport.editGoals || []
        for (let index = 0; index < goals.length && lines.length < 6; ++index) {
            const goal = goals[index]
            if (typeof goal === "string")
                addLine(goal)
            else
                addLine(goal.label || goal.goal || goal.summary || goal.reason)
        }

        const risk = report.riskAssessment || draftReport.riskAssessment || ({})
        const approvalTypes = risk.approvalTypes || []
        if (approvalTypes.length > 0)
            addLine("Needs care around " + approvalTypes.join(", ") + ".")
        addLine(report.reviewRequiredReason || draftReport.reviewRequiredReason)

        const evidence = agentRunEvidenceSummary()
        if (evidence.length > 0)
            addLine("Evidence: " + evidence)

        const calls = agentToolCalls()
        if (calls.length > 0) {
            const latest = calls[Math.min(calls.length - 1, 2)] || ({})
            addLine("Last checked: " + agentToolCallTitle(latest))
        }

        if (lines.length === 0) {
            const style = String((state.style || ({})).name || "")
            const scope = String(state.scopeLabel || "")
            addLine("Ready to reason from " + (style.length > 0 ? style : "the selected inspiration") +
                    (scope.length > 0 ? " across " + scope + "." : "."))
        }
        return lines.slice(0, 6)
    }

    function agentThinkingVisible() {
        return root.hasAgentEditController && agentThinkingLines().length > 0
    }

    function agentHasRunAudit() {
        return agentToolCalls().length > 0 ||
               agentEditLoops().length > 0 ||
               agentRunWarnings().length > 0 ||
               agentRunArtifactSummary().length > 0 ||
               agentRunEvidenceSummary().length > 0
    }

    function agentReportMode() {
        const report = root.agentEditController.lastRunReport || ({})
        return String(report.mode || agentRunMode || "")
    }

    function agentToolCallMatchesStage(toolName, stageKey) {
        const name = String(toolName || "")
        const mode = agentReportMode()
        if (stageKey === "analyze")
            return name === "render_current_proof" && mode === "analyze"
        if (stageKey === "plan")
            return name === "validate_recipe_patch"
        if (stageKey === "draft-recipe")
            return name === "validate_recipe_patch"
        if (stageKey === "render-proof")
            return name === "render_current_proof" || name === "render_proof_and_validate"
        if (stageKey === "validate")
            return name === "validate_recipe_patch" ||
                   name === "render_proof_and_validate" ||
                   name === "validate_batch_asset" ||
                   name === "export_preflight" ||
                   name === "render_current_proof"
        if (stageKey === "revise")
            return name === "revise_recipe_patch_from_validation"
        if (stageKey === "batch-sync")
            return name === "apply_recipe_patch_to_selected" || name === "validate_batch_asset"
        if (stageKey === "export-preflight")
            return name === "export_preflight" || name === "queue_batch_export"
        return false
    }

    function agentStageActiveKey() {
        const status = String(root.agentEditController.status || "").toLowerCase()
        if (status.indexOf("analyz") >= 0)
            return "analyze"
        if (status.indexOf("planning") >= 0)
            return "plan"
        if (status.indexOf("applying draft") >= 0)
            return "draft-recipe"
        if (status.indexOf("validating batch") >= 0)
            return "validate"
        if (status.indexOf("export preflight") >= 0 ||
                status.indexOf("preparing export") >= 0 ||
                status.indexOf("queueing export") >= 0)
            return "export-preflight"
        return ""
    }

    function agentStageHasToolCall(stageKey) {
        const calls = agentToolCalls()
        for (let index = 0; index < calls.length; ++index) {
            if (agentToolCallMatchesStage(calls[index].name, stageKey))
                return true
        }
        return false
    }

    function agentStageHasFailedToolCall(stageKey) {
        const calls = agentToolCalls()
        for (let index = 0; index < calls.length; ++index) {
            const call = calls[index]
            if (agentToolCallMatchesStage(call.name, stageKey) &&
                    String(call.status || "").toLowerCase() === "failed")
                return true
        }
        return false
    }

    function agentStageWarningCount(stageKey) {
        const calls = agentToolCalls()
        let warningCount = 0
        for (let index = 0; index < calls.length; ++index) {
            const call = calls[index]
            if (!agentToolCallMatchesStage(call.name, stageKey))
                continue
            const outcome = call.outcome || ({})
            warningCount += Number(outcome.warningCount || 0)
        }
        return warningCount
    }

    function agentStageCompletedFromReport(stageKey) {
        const report = root.agentEditController.lastRunReport || ({})
        const mode = agentReportMode()
        if (agentStageHasToolCall(stageKey))
            return true
        if (stageKey === "analyze")
            return mode === "analyze" && String(report.analysisSummary || "").length > 0
        if (stageKey === "render-proof")
            return agentHasProofSurface()
        if (stageKey === "validate")
            return report.validationReport !== undefined ||
                   Number(report.checkedCount || 0) > 0 ||
                   Number(report.errorCount || 0) > 0 ||
                   Number(report.warningCount || 0) > 0
        if (stageKey === "batch-sync")
            return agentAssetReports().length > 0 ||
                   Number(report.draftedCount || 0) > 0 ||
                   Number(report.checkedCount || 0) > 0
        if (stageKey === "export-preflight")
            return mode === "export-preflight" || mode === "queue-export"
        if (stageKey === "ready-review")
            return report.ok === true && !root.agentEditController.running
        return false
    }

    function agentStageStatus(stageKey) {
        if (root.agentEditController.running && agentStageActiveKey() === stageKey)
            return "active"
        if (agentStageHasFailedToolCall(stageKey))
            return "failed"
        if (agentStageWarningCount(stageKey) > 0)
            return "warning"
        if (agentStageCompletedFromReport(stageKey))
            return "complete"
        return "pending"
    }

    function agentStageStatusColor(status) {
        const value = String(status || "")
        if (value === "active")
            return theme.accentCool500
        if (value === "complete")
            return theme.success
        if (value === "warning")
            return theme.warning
        if (value === "failed")
            return theme.error
        return theme.textMuted
    }

    function agentStageStatusLabel(status) {
        if (status === "complete")
            return "done"
        return status
    }

    function agentHasStageProgress() {
        const report = root.agentEditController.lastRunReport || ({})
        return root.agentEditController.running ||
               String(report.mode || "").length > 0 ||
               agentToolCalls().length > 0
    }

    function agentScopeText() {
        const count = catalogAssets.selectedAssetCount
        if (count > 1)
            return "Selected assets · " + count
        if (count === 1)
            return "Current photo · " + catalogAssets.selectedAssetId
        return "No selected photos"
    }

    function agentRunHistory() {
        return root.agentEditController.runHistory || []
    }

    function agentRunHistoryStatusColor(entry) {
        if (!entry || entry.ok !== true)
            return theme.error
        const warningCount = Number(entry.warningCount || 0)
        if (warningCount > 0)
            return theme.warning
        return theme.success
    }

    function agentRunHistoryTitle(entry) {
        const mode = agentRunModeLabel(String(entry.mode || ""))
        const scope = String(entry.scope || "")
        return scope.length > 0 ? mode + " · " + scope : mode
    }

    function agentRunHistoryDetail(entry) {
        const assetCount = Number(entry.assetCount || 0)
        const durationMs = Number(entry.durationMs || 0)
        const queueWaitMs = Number(entry.queueWaitMs || 0)
        const warnings = Number(entry.warningCount || 0)
        const parts = []
        if (assetCount > 0)
            parts.push(assetCount + " assets")
        if (durationMs >= 0)
            parts.push(durationMs + " ms")
        if (queueWaitMs > 0)
            parts.push("queued " + queueWaitMs + " ms")
        if (warnings > 0)
            parts.push(warnings + " warnings")
        return parts.join(" · ")
    }

    function agentRunModeLabel(mode) {
        if (mode === "analyze")
            return "Analyze"
        if (mode === "validate-batch")
            return "Batch Check"
        if (mode === "export-preflight")
            return "Export Check"
        return "Draft"
    }

    function agentRunModeIcon(mode) {
        if (mode === "analyze")
            return "filter"
        if (mode === "validate-batch")
            return "check"
        if (mode === "export-preflight")
            return "export"
        return "adjust"
    }

    function agentRecipeBaseModeLabel(mode) {
        if (mode === "neutral")
            return "Neutral"
        return "Current"
    }

    function agentRecipeBaseModeIcon(mode) {
        if (mode === "neutral")
            return "reset"
        return "layers"
    }

    function agentCanRunMode(mode) {
        if (!editorPreview.hasAsset || root.agentEditController.running)
            return false
        if (mode === "analyze")
            return catalogAssets.selectedAssetCount === 1
        return catalogAssets.selectedAssetCount >= 1
    }

    function agentWorkflowState() {
        return root.hasAgentEditController ? root.agentEditController.aiEditWorkflowState("") || ({}) : ({})
    }

    function agentWorkflowPrimaryAction() {
        const state = agentWorkflowState()
        return state.primaryAction || ({})
    }

    function agentWorkflowPrimaryKey() {
        const action = agentWorkflowPrimaryAction()
        return String(action.key || "")
    }

    function agentSampleReviewWaiting() {
        const state = agentWorkflowState()
        return Boolean(state.sampleReviewWaiting)
    }

    function agentWorkflowPrimaryLabel() {
        const action = agentWorkflowPrimaryAction()
        return String(action.label || "AI Edit")
    }

    function agentWorkflowPrimaryIcon() {
        const key = agentWorkflowPrimaryKey()
        if (key === "set_up_local_ai")
            return "download"
        if (key === "learn_style")
            return "select"
        if (key === "apply_to_all")
            return "check"
        if (key === "commit")
            return "check"
        if (key === "cancel")
            return "close"
        if (key === "answer_question" || key === "review_issue")
            return "select"
        if (key === "choose_style")
            return "menu"
        return "adjust"
    }

    function agentWorkflowPrimaryEnabled() {
        if (!root.hasAgentEditController)
            return false
        const action = agentWorkflowPrimaryAction()
        const key = String(action.key || "")
        if (key === "create_draft" || key === "create_samples")
            return agentCanRunMode("draft")
        if (key === "apply_to_all")
            return root.agentEditController.draftActive && !root.agentEditController.running
        if (key === "commit")
            return root.agentEditController.draftActive && !root.agentEditController.running
        if (key === "cancel")
            return root.agentEditController.running
        if (key === "set_up_local_ai")
            return !root.agentEditController.running && !root.agentEditController.modelInstallRunning
        if (key === "learn_style")
            return !root.agentEditController.running
        return Boolean(action.enabled)
    }

    function agentWorkflowDetailText() {
        const state = agentWorkflowState()
        const reasons = state.blockingReasons || []
        if (reasons.length > 0)
            return String(reasons[0] || "")
        const style = state.style || ({})
        const styleName = String(style.name || "")
        const scope = String(state.scopeLabel || "")
        return [scope, styleName].filter(function(part) { return part.length > 0 }).join(" · ")
    }

    function agentWorkflowTasteCard() {
        const state = agentWorkflowState()
        return state.tasteCard || (state.style ? state.style.tasteCard || ({}) : ({}))
    }

    function agentWorkflowStyleLearning() {
        const state = agentWorkflowState()
        return state.styleLearning || ({})
    }

    function agentTasteCardVisible() {
        const card = agentWorkflowTasteCard()
        const learning = agentWorkflowStyleLearning()
        return Boolean(card.available) || Boolean(learning.available)
    }

    function agentTasteCardTitle() {
        const card = agentWorkflowTasteCard()
        if (Boolean(card.available))
            return String(card.name || "Active style")
        const learning = agentWorkflowStyleLearning()
        if (Boolean(learning.available))
            return "Ready to learn from selection"
        return ""
    }

    function agentTasteCardText() {
        const card = agentWorkflowTasteCard()
        if (Boolean(card.available)) {
            const confidence = String(card.confidence || "")
            const references = Number(card.referenceCount || 0)
            const summary = String(card.summary || "")
            const parts = []
            if (references > 0)
                parts.push(references + " reference" + (references === 1 ? "" : "s"))
            if (confidence.length > 0)
                parts.push(confidence + " confidence")
            if (summary.length > 0)
                parts.push(summary)
            return parts.join(" · ")
        }
        const learning = agentWorkflowStyleLearning()
        if (Boolean(learning.available)) {
            const usable = Number(learning.usableRecipeCount || 0)
            return usable + " usable edited reference" + (usable === 1 ? "" : "s")
        }
        return ""
    }

    function agentMemoryVaultStatus() {
        return root.hasAgentEditController ? root.agentEditController.memoryVaultStatus() || ({}) : ({})
    }

    function agentMemoryVaultText() {
        const status = agentMemoryVaultStatus()
        const total = Number(status.totalNoteCount || 0)
        const rootPath = String(status.rootPath || "")
        const activeStylePath = String(status.activeStyleNotePath || "")
        const parts = ["Memory", "local Markdown", "packets only", total + " notes"]
        if (activeStylePath.length > 0)
            parts.push("style note ready")
        if (Boolean(status.externalChangeDetected))
            parts.push(Number(status.externalChangeCount || 0) + " outside change(s)")
        if (rootPath.length > 0)
            parts.push(rootPath)
        return parts.join(" · ")
    }

    function agentLatestStyleChangeSummary() {
        const report = root.hasAgentEditController ? root.agentEditController.lastRunReport || ({}) : ({})
        const directSummary = String(report.styleChangeSummary || "")
        if (directSummary.length > 0)
            return directSummary
        const events = report.userVisibleMemoryEvents || []
        for (let index = events.length - 1; index >= 0; --index) {
            const event = events[index] || ({})
            const summary = String(event.styleChangeSummary || event.summary || "")
            if (summary.length > 0)
                return summary
        }
        return ""
    }

    function agentMemoryResultText(result, successText) {
        const report = result || ({})
        const errors = report.errors || []
        if (!Boolean(report.ok) && errors.length > 0)
            return String(errors[0])
        const path = String(report.exportPath || report.rootPath || "")
        if (Boolean(report.ok))
            return path.length > 0 ? successText + ": " + path : successText
        const warnings = report.warnings || []
        if (warnings.length > 0)
            return String(warnings[0])
        return "Memory action failed."
    }

    function openAgentMemoryVault() {
        if (!root.hasAgentEditController)
            return
        agentMemoryVaultResetArmed = false
        agentMemoryVaultMessage = agentMemoryResultText(root.agentEditController.openMemoryVault(), "Vault opened")
    }

    function openAgentMemoryVaultInObsidian() {
        if (!root.hasAgentEditController)
            return
        agentMemoryVaultResetArmed = false
        agentMemoryVaultMessage = agentMemoryResultText(
                    root.agentEditController.openMemoryVaultInObsidian(false), "Obsidian opened")
    }

    function exportAgentMemoryVault() {
        if (!root.hasAgentEditController)
            return
        agentMemoryVaultResetArmed = false
        agentMemoryVaultMessage = agentMemoryResultText(
                    root.agentEditController.exportMemoryVault("", true), "Redacted vault exported")
    }

    function resetAgentMemoryVaultFromDetails() {
        if (!root.hasAgentEditController)
            return
        if (!agentMemoryVaultResetArmed) {
            agentMemoryVaultResetArmed = true
            agentMemoryVaultMessage = "Click Confirm Reset to clear local agent memory."
            return
        }
        const result = root.agentEditController.resetMemoryVault(true)
        agentMemoryVaultResetArmed = false
        agentMemoryVaultMessage = agentMemoryResultText(result, "Memory vault reset")
    }

    function learnAgentStyleFromSelection() {
        if (!root.hasAgentEditController)
            return
        const learning = agentWorkflowStyleLearning()
        const count = Number(learning.selectedCount || 0)
        const label = count > 1 ? "Learned Project Style" : "Learned Photo Style"
        const result = root.agentEditController.createLearnedStyleProfileFromSelection(
                    label,
                    "Edit selected photos consistently in my demonstrated style.",
                    String(root.agentEditController.userInstructions || ""))
        if (Boolean(result.ok)) {
            agentStyleSaveMessage = "Learned style from " + count + " photo" + (count === 1 ? "" : "s")
            agentDetailsExpanded = false
        } else {
            const errors = result.errors || []
            agentStyleSaveMessage = errors.length > 0 ? errors.join("\n") : "Unable to learn this style"
            agentDetailsExpanded = true
        }
    }

    function runAgentWorkflowPrimaryAction() {
        if (!root.hasAgentEditController || !agentWorkflowPrimaryEnabled())
            return
        const key = agentWorkflowPrimaryKey()
        if (key === "create_draft" || key === "create_samples") {
            root.agentEditController.applyEditForScope("", root.agentEditController.userInstructions)
        } else if (key === "learn_style") {
            learnAgentStyleFromSelection()
        } else if (key === "choose_style") {
            agentDetailsExpanded = true
        } else if (key === "apply_to_all") {
            root.agentEditController.applySampleReviewToAll(true)
        } else if (key === "commit") {
            root.agentEditController.commitDraft(true)
        } else if (key === "cancel") {
            root.agentEditController.cancelRun("user_cancelled")
        } else if (key === "set_up_local_ai") {
            updateAgentModelSetupMessage(root.agentEditController.setupLocalAi(true))
        }
    }

    function runSelectedAgentMode() {
        if (!agentCanRunMode(agentRunMode))
            return
        if (agentRunMode === "analyze") {
            root.agentEditController.analyzeSelected()
        } else if (agentRunMode === "validate-batch") {
            root.agentEditController.validateBatchForSelected()
        } else if (agentRunMode === "export-preflight") {
            root.agentEditController.exportPreflightForSelected()
        } else {
            root.agentEditController.applyEditForSelected(root.agentEditController.userInstructions)
        }
    }

    function setAgentStylePermissionMode(mode) {
        agentStylePermissionMode = mode
        agentStyleAllowRaw = true
        agentStyleAllowGlobalColor = true
        agentStyleAllowCurves = true
        agentStyleAllowHsl = true
        agentStyleAllowDetail = true
        agentStyleAllowCrop = true
        agentStyleAllowColorGrading = mode !== "conservative"
        agentStyleAllowMasks = mode !== "conservative"
        agentStyleAllowLayersLuts = mode === "full_edit" || mode === "full_edit_retouch"
        agentStyleAllowRetouch = mode === "full_edit_retouch"
        agentStyleAllowExport = false
    }

    function agentStylePermissionSummary() {
        if (agentStylePermissionMode === "conservative")
            return "Can adjust tone, color, detail, and conservative crop. Masks and retouch stay off."
        if (agentStylePermissionMode === "full_edit")
            return "Can adjust color, tone, crop, local masks, layers, and LUTs. Retouch still requires approval."
        if (agentStylePermissionMode === "full_edit_retouch")
            return "Can use the full edit toolset including local masks, layers, LUTs, and minor retouch."
        return "Can adjust color, tone, crop, and local masks. Retouch requires approval."
    }

    function createAgentStyleFromDraft() {
        const result = root.agentEditController.createStyle(
                    agentStyleDraftName,
                    agentStyleDraftGoal,
                    {
                        "raw": agentStyleAllowRaw,
                        "global_color": agentStyleAllowGlobalColor,
                        "curves": agentStyleAllowCurves,
                        "hsl": agentStyleAllowHsl,
                        "color_grading": agentStyleAllowColorGrading,
                        "detail": agentStyleAllowDetail,
                        "crop": agentStyleAllowCrop ? "conservative" : false,
                        "masks": agentStyleAllowMasks,
                        "retouch": agentStyleAllowRetouch ? "minor_cleanup_only" : false,
                        "layers_luts": agentStyleAllowLayersLuts,
                        "export": agentStyleAllowExport
                    },
                    {
                        "avoid_clipped_highlights": true,
                        "avoid_crushed_blacks": true,
                        "max_exposure_shift": boundedAgentNumber(agentStyleMaxExposureShift, 1.2, 0, 5),
                        "max_temperature_shift": boundedAgentNumber(agentStyleMaxTemperatureShift, 1800, 0, 10000),
                        "max_saturation_shift": boundedAgentNumber(agentStyleMaxSaturationShift, 18, 0, 100)
                    },
                    {
                        "highlight_clip_limit_percent": boundedAgentNumber(agentStyleHighlightClipLimit, 0.5, 0, 10),
                        "shadow_clip_limit_percent": boundedAgentNumber(agentStyleShadowClipLimit, 1.0, 0, 10),
                        "require_before_after_proof": true
                    },
                    {
                        "cluster_by_lighting": true,
                        "maintain_sequence_consistency": true,
                        "correct_outliers_individually": true
                    })
        if (result.ok) {
            agentStyleSaveMessage = "Saved " + agentStyleDraftName.trim()
            agentStyleDraftName = ""
            agentStyleDraftGoal = ""
            agentStyleBuilderExpanded = false
        } else {
            agentStyleSaveMessage = result.errors !== undefined ? result.errors.join("\n") : "Unable to save style"
        }
    }

    function clearTransientTools() {
        transientMaskTool = ""
        maskColorRangeSampling = false
        maskLuminanceRangeSampling = false
        maskDepthRangeSampling = false
        editorPreview.clearSmartSelectPreview()
    }

    function setMaskColorRangeSampler(enabled) {
        maskColorRangeSampling = Boolean(enabled)
        maskLuminanceRangeSampling = false
        maskDepthRangeSampling = false
        return maskColorRangeSampling
    }

    function setMaskLuminanceRangeSampler(enabled) {
        maskLuminanceRangeSampling = Boolean(enabled)
        maskColorRangeSampling = false
        maskDepthRangeSampling = false
        return maskLuminanceRangeSampling
    }

    function setMaskDepthRangeSampler(enabled) {
        maskDepthRangeSampling = Boolean(enabled) && editorPreview.activeMaskDepthSamplingAvailable
        maskColorRangeSampling = false
        maskLuminanceRangeSampling = false
        return maskDepthRangeSampling
    }

    function resetInspectorScroll() {
        if (inspectorScroll && inspectorScroll.contentItem)
            inspectorScroll.contentItem.contentY = 0
    }

    function activeSelectionBrushCanTemporarilyInvert() {
        return editorPreview.hasActiveMask &&
               editorPreview.activeMaskKind === "brush" &&
               editorPreview.activeMaskToolRole !== "retouch" &&
               !controlModifierActive &&
               !objectPickArmed &&
               !polygonDrawArmed &&
               !lassoDrawArmed
    }

    function manualSelectionSubtractActive() {
        return optionModifierActive && !controlModifierActive
    }

    function activePolygonShortcutReady() {
        return inspectorMode === "masks" &&
               editorPreview.hasActiveMask &&
               editorPreview.activeMaskKind === "polygon"
    }

    function completeActivePolygonSelection() {
        if (!activePolygonShortcutReady() ||
                editorPreview.activeMaskPolygonPointCount < 3 ||
                editorPreview.activeMaskPolygonSelfIntersecting)
            return false
        clearTransientTools()
        inspectorTab = "masks"
        inspectorMode = "masks"
        return true
    }

    function activePolygonSnapClose(normalizedX, normalizedY, canvasWidth, canvasHeight) {
        if (!activePolygonShortcutReady() ||
                editorPreview.activeMaskPolygonPointCount < 3 ||
                editorPreview.activeMaskPolygonSelfIntersecting)
            return false
        const points = editorPreview.activeMaskPolygonPoints || []
        if (points.length < 3)
            return false
        const first = points[0]
        const dx = (Number(normalizedX) - Number(first.x || 0)) * Math.max(1, Number(canvasWidth || 1))
        const dy = (Number(normalizedY) - Number(first.y || 0)) * Math.max(1, Number(canvasHeight || 1))
        return Math.sqrt(dx * dx + dy * dy) <= 18
    }

    function completeActivePolygonIfSnappingToFirst(normalizedX, normalizedY, canvasWidth, canvasHeight) {
        if (!activePolygonSnapClose(normalizedX, normalizedY, canvasWidth, canvasHeight))
            return false
        return completeActivePolygonSelection()
    }

    function syncTemporaryBrushModifierState() {
        if (optionModifierActive && activeSelectionBrushCanTemporarilyInvert()) {
            if (!selectionBrushRestorePending) {
                selectionBrushRestoreSubtract = editorPreview.activeMaskOperation === "subtract"
                selectionBrushRestorePending = true
                editorPreview.startSelectionBrush(!selectionBrushRestoreSubtract)
            }
            return
        }

        if (selectionBrushRestorePending && !editorPreview.brushStrokeActive) {
            editorPreview.startSelectionBrush(selectionBrushRestoreSubtract)
            selectionBrushRestorePending = false
        }
    }

    function deselectMaskEditing() {
        clearTransientTools()
        activeGradePreset = ""
        editorPreview.deselectActiveMask()
    }

    function handleMaskPickOutcome(outcome) {
        if (outcome === "selection-ai" || outcome === "remove-ai")
            transientMaskTool = ""
    }

    function selectTool(mode) {
        clearTransientTools()
        if (mode === "smart" || mode === "smart-subtract" || mode === "smart-new" ||
                mode === "remove" || mode === "remove-new" || mode === "polygon" ||
                mode === "lasso" || mode === "clone-source") {
            transientMaskTool = mode
            inspectorTab = "masks"
            inspectorMode = "masks"
        }
    }

    function activateRailMode(mode) {
        clearTransientTools()
        if (mode === "adjust") {
            inspectorTab = "adjust"
            inspectorMode = "adjust"
            return
        }
        if (mode === "crop") {
            inspectorMode = "crop"
            editorPreview.cropEditing = editorPreview.hasAsset
            return
        }
        if (mode === "masks") {
            inspectorTab = "masks"
            inspectorMode = "masks"
            return
        }
        if (mode === "local") {
            inspectorTab = "masks"
            inspectorMode = "masks"
            if (editorPreview.hasAsset)
                selectTool("smart")
            return
        }
        if (mode === "retouch") {
            inspectorTab = "masks"
            inspectorMode = "masks"
            if (editorPreview.hasAsset)
                editorPreview.startHealBrush()
            return
        }
        if (mode === "scope") {
            inspectorTab = "scopes"
            inspectorMode = "adjust"
            scopePanelsExpanded = !scopePanelsExpanded
        }
    }

    function activeVisibilityContract() {
        return inspectorMode === "masks" ? editVisibilityContract + "|mode:masks" : editVisibilityContract
    }

    function activateEditTool(toolName) {
        const tool = String(toolName || "adjust")
        if (tool === "masks") {
            activateRailMode("masks")
            return
        }
        if (tool === "brush") {
            startMaskBrushSource("add")
            return
        }
        if (tool === "retouch") {
            activateRailMode("retouch")
            return
        }
        if (tool === "crop") {
            activateRailMode("crop")
            return
        }
        if (tool === "compare") {
            inspectorMode = "adjust"
            inspectorTab = "adjust"
            editorPreview.compareMode = true
            return
        }
        activateRailMode("adjust")
    }

    function startMaskBrushSource(operationName) {
        selectionBrushRestorePending = false
        clearTransientTools()
        inspectorTab = "masks"
        inspectorMode = "masks"
        editorPreview.startSelectionBrush(String(operationName || "add") === "subtract")
    }

    function brushToolReady() {
        return inspectorMode === "masks" &&
               editorPreview.activeMaskKind === "brush" &&
               editorPreview.activeMaskRetouchMode.length === 0
    }

    function activeToolLabel() {
        if (transientMaskTool === "lasso")
            return "Lasso"
        if (transientMaskTool === "clone-source")
            return "Pick Clone Source"
        if (transientMaskTool === "polygon")
            return "Polygon"
        if (transientMaskTool === "remove-new")
            return "Pick New Remove Target"
        if (transientMaskTool === "remove")
            return "Pick Remove Target"
        if (transientMaskTool === "smart-subtract")
            return "Subtract Object"
        if (transientMaskTool === "smart-new")
            return "New Object Selection"
        if (transientMaskTool === "smart")
            return "Object Select"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "clone")
            return "Clone Stamp"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "heal")
            return "Heal Brush"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "patch")
            return "Patch Brush"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "remove")
            return editorPreview.activeMaskCommitted ? "Remove Brush" : "Pending Remove Brush"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode === "dust")
            return "Dust Removal"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode === "red-eye")
            return "Red-Eye Removal"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode === "skin-cleanup")
            return "Skin Cleanup (Heuristic)"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode === "frequency-smoothing")
            return "Frequency Smoothing"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskKind === "object" &&
                editorPreview.activeMaskRetouchMode === "remove")
            return editorPreview.activeMaskCommitted ? "Remove Target" : "Pending Remove Target"
        if (editorPreview.activeMaskKind === "polygon")
            return "Polygon Selection"
        if (editorPreview.activeMaskKind === "lasso")
            return "Lasso Selection"
        if (editorPreview.activeMaskKind === "luminance")
            return "Luminance Range"
        if (editorPreview.activeMaskKind === "color")
            return "Color Range"
        if (editorPreview.activeMaskKind === "depth")
            return "Depth Range"
        if (editorPreview.activeMaskKind === "alpha")
            return "Alpha Image"
        if (isAiMaskKind(editorPreview.activeMaskKind))
            return aiMaskLabel(editorPreview.activeMaskKind) + " Selection"
        if (editorPreview.activeMaskKind === "brush" &&
                editorPreview.activeMaskName.indexOf("Edge ") === 0 &&
                editorPreview.activeMaskOperation === "subtract")
            return "Edge Erase"
        if (editorPreview.activeMaskKind === "brush" &&
                editorPreview.activeMaskName.indexOf("Edge ") === 0)
            return "Edge Add"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskOperation === "subtract")
            return "Brush Erase"
        if (editorPreview.activeMaskKind === "brush")
            return "Brush Add"
        return "Adjust"
    }

    function editWorkflowPolishSignature() {
        const pendingTool = transientMaskTool.length > 0 ||
                            editorPreview.brushStrokeActive ||
                            editorPreview.cropEditing ||
                            editorPreview.compareMode
        return "edit-workflow-polish:v1" +
               "|asset:" + String(editorPreview.hasAsset) +
               "|tool-label:" + root.activeToolLabel() +
               "|inspector-tab:" + inspectorTab +
               "|inspector-mode:" + inspectorMode +
               "|left-panel-collapsed:" + String(leftPanelCollapsed) +
               "|inspector-collapsed:" + String(inspectorCollapsed) +
               "|filmstrip-collapsed:" + String(filmstripCollapsed) +
               "|pending:" + String(pendingTool) +
               "|transient:" + (transientMaskTool.length > 0 ? transientMaskTool : "none") +
               "|target:" + (editorPreview.hasActiveMask ? "mask" : "base") +
               "|preserve:tool-rail-image-stage-toolbar-inspector-filmstrip-bottom-actions"
    }

    function resetActiveMaskColorGrade() {
        editorPreview.activeMaskTemperature = 0
        editorPreview.activeMaskTint = 0
        editorPreview.activeMaskVibrance = 0
        editorPreview.activeMaskSaturation = 0
        editorPreview.activeMaskHueShift = 0
        editorPreview.activeMaskSaturationShift = 0
        editorPreview.activeMaskColorizeHue = 0
        editorPreview.activeMaskColorizeStrength = 0
    }

    property string activeGradePreset: ""

    function resetAllMaskAdjustments() {
        editorPreview.activeMaskExposure = 0
        editorPreview.activeMaskContrast = 0
        editorPreview.activeMaskHighlights = 0
        editorPreview.activeMaskShadows = 0
        editorPreview.activeMaskWhites = 0
        editorPreview.activeMaskBlacks = 0
        resetActiveMaskColorGrade()
    }

    function applyActiveMaskGradePreset(preset) {
        if (!editorPreview.hasActiveMask || editorPreview.activeMaskToolRole === "retouch")
            return

        if (preset === "reset") {
            resetAllMaskAdjustments()
            activeGradePreset = ""
            return
        }

        resetAllMaskAdjustments()
        activeGradePreset = preset

        if (preset === "warm") {
            editorPreview.activeMaskTemperature = 18
            editorPreview.activeMaskTint = 4
            editorPreview.activeMaskVibrance = 10
            editorPreview.activeMaskSaturation = 6
            return
        }

        if (preset === "cool") {
            editorPreview.activeMaskTemperature = -18
            editorPreview.activeMaskTint = -4
            editorPreview.activeMaskVibrance = 6
            editorPreview.activeMaskSaturation = -4
            return
        }

        if (preset === "pop") {
            editorPreview.activeMaskVibrance = 24
            editorPreview.activeMaskSaturation = 8
            editorPreview.activeMaskContrast = 12
            return
        }

        if (preset === "desat") {
            editorPreview.activeMaskVibrance = -12
            editorPreview.activeMaskSaturation = -28
            return
        }

        if (preset === "tint-warm") {
            editorPreview.activeMaskColorizeHue = 0.09
            editorPreview.activeMaskColorizeStrength = 24
            return
        }

        if (preset === "tint-cool") {
            editorPreview.activeMaskColorizeHue = 0.58
            editorPreview.activeMaskColorizeStrength = 20
            return
        }

        if (preset === "lift") {
            editorPreview.activeMaskExposure = 0.35
            editorPreview.activeMaskWhites = 12
            editorPreview.activeMaskShadows = 10
            return
        }

        if (preset === "burn") {
            editorPreview.activeMaskExposure = -0.35
            editorPreview.activeMaskBlacks = -12
            editorPreview.activeMaskContrast = 10
            return
        }

        if (preset === "soften") {
            editorPreview.activeMaskContrast = -16
            editorPreview.activeMaskHighlights = -10
            editorPreview.activeMaskShadows = 10
            editorPreview.activeMaskSaturation = -8
            return
        }

        if (preset === "teal-pop") {
            editorPreview.activeMaskTemperature = -10
            editorPreview.activeMaskTint = -6
            editorPreview.activeMaskVibrance = 18
            editorPreview.activeMaskColorizeHue = 0.54
            editorPreview.activeMaskColorizeStrength = 14
        }
    }

    function maskWorkspaceAccentColor() {
        if (editorPreview.hasActiveMask &&
                editorPreview.activeMaskIndex >= 0 &&
                editorPreview.activeMaskIndex < editorPreview.maskStack.length)
            return maskAccent(editorPreview.maskStack[editorPreview.activeMaskIndex])
        if (objectPickArmed || polygonDrawArmed || lassoDrawArmed)
            return theme.accentWarm400
        if (cloneSourcePickArmed)
            return theme.accentWarm500
        return theme.accentCool400
    }

    function selectionOperationOverlayColor(operationName) {
        const operation = String(operationName || "add").trim().toLowerCase()
        if (operation === "subtract")
            return "#ff8a4c"
        if (operation === "intersect")
            return "#8fa8ff"
        return "#24dcc2"
    }

    function previewStatusText() {
        if (inspectorMode !== "masks" || !editorPreview.hasAsset)
            return editorPreview.processing ? "" : editorPreview.status

        if (cloneSourcePickArmed)
            return "Masks · " + activeToolLabel() + " · Choose a clean source area"
        if (objectPickArmed && objectPickMode === "remove")
            return "Masks · " + activeToolLabel() + " · Click or drag over the target"
        if (objectPickArmed)
            return "Masks · " + activeToolLabel() + " · Click the image to build the selection"
        if (polygonDrawArmed)
            return "Masks · Polygon · Click points and double-click to finish"
        if (lassoDrawArmed)
            return "Masks · Lasso · Drag directly on the canvas"
        if (editorPreview.hasActiveMask)
            return "Masks · " + activeToolLabel() + " · " + editorPreview.activeMaskName
        return "Masks · Choose a selection source or start painting"
    }

    function activeMaskHasAdjustments() {
        return Math.abs(editorPreview.activeMaskExposure) > 0.001 ||
               Math.abs(editorPreview.activeMaskContrast) > 0.001 ||
               Math.abs(editorPreview.activeMaskHighlights) > 0.001 ||
               Math.abs(editorPreview.activeMaskShadows) > 0.001 ||
               Math.abs(editorPreview.activeMaskWhites) > 0.001 ||
               Math.abs(editorPreview.activeMaskBlacks) > 0.001 ||
               Math.abs(editorPreview.activeMaskTemperature) > 0.001 ||
               Math.abs(editorPreview.activeMaskTint) > 0.001 ||
               Math.abs(editorPreview.activeMaskVibrance) > 0.001 ||
               Math.abs(editorPreview.activeMaskSaturation) > 0.001 ||
               Math.abs(editorPreview.activeMaskHueShift) > 0.001 ||
               Math.abs(editorPreview.activeMaskSaturationShift) > 0.001 ||
               Math.abs(editorPreview.activeMaskColorizeStrength) > 0.001
    }

    function activeMaskSupportsSelectionRefine() {
        return editorPreview.hasActiveMask &&
               (editorPreview.activeMaskKind === "linear" ||
                editorPreview.activeMaskKind === "radial" ||
                editorPreview.activeMaskKind === "brush" ||
                editorPreview.activeMaskKind === "polygon" ||
                editorPreview.activeMaskKind === "alpha" ||
                isAiMaskKind(editorPreview.activeMaskKind) ||
                editorPreview.activeMaskKind === "luminance" ||
                editorPreview.activeMaskKind === "color")
    }

    function isAiMaskKind(kind) {
        return kind === "subject" || kind === "sky" || kind === "background" ||
               kind === "people" || kind === "face" || kind === "hair" ||
               kind === "clothing" || kind === "object"
    }

    function aiMaskLabel(kind) {
        if (kind === "sky")
            return "Sky (Heuristic)"
        if (kind === "background")
            return "Background (Subject Invert)"
        if (kind === "people")
            return "People (Unsupported)"
        if (kind === "face")
            return "Face (Unsupported)"
        if (kind === "hair")
            return "Hair (Unsupported)"
        if (kind === "clothing")
            return "Clothing (Unsupported)"
        if (kind === "object")
            return "Object"
        return "Subject"
    }

    function maskAccent(mask) {
        if (!mask)
            return "#8ab8d8"
        if (mask.toolRole !== "retouch" &&
                (mask.operation === "add" || mask.operation === "subtract" ||
                 mask.operation === "intersect"))
            return selectionOperationOverlayColor(mask.operation)
        if (mask.toolRole === "retouch" && mask.retouchMode === "clone")
            return theme.accentWarm500
        if (mask.toolRole === "retouch" && mask.retouchMode === "heal")
            return "#72cbb2"
        if (mask.toolRole === "retouch" && mask.retouchMode === "dust")
            return "#c2b36a"
        if (mask.toolRole === "retouch" && mask.retouchMode === "red-eye")
            return "#d36a5e"
        if (mask.toolRole === "retouch" && mask.retouchMode === "skin-cleanup")
            return "#d7a08c"
        if (mask.toolRole === "retouch" && mask.retouchMode === "frequency-smoothing")
            return "#c994d8"
        if (mask.toolRole === "retouch" && mask.retouchMode === "remove")
            return "#d98962"
        if (mask.kind === "radial")
            return "#8ab8d8"
        if (mask.kind === "brush")
            return "#cf8f4d"
        if (mask.kind === "polygon" || mask.kind === "lasso")
            return "#7ec97a"
        if (mask.kind === "luminance")
            return "#d8d27a"
        if (mask.kind === "color")
            return "#c77dd8"
        if (mask.kind === "depth")
            return "#72b7a6"
        if (mask.kind === "alpha")
            return "#a3b35f"
        if (mask.kind === "subject")
            return "#5fc9b5"
        if (mask.kind === "sky")
            return "#6db7e8"
        if (mask.kind === "background")
            return "#9b8fdc"
        if (mask.kind === "people" || mask.kind === "face")
            return "#d889a8"
        if (mask.kind === "hair" || mask.kind === "clothing")
            return "#c4a35f"
        if (mask.kind === "object")
            return theme.accentWarm400
        return "#c99a52"
    }

    function activeMaskResetFeather() {
        if (editorPreview.activeMaskKind === "radial")
            return 0.25
        if (editorPreview.activeMaskKind === "brush")
            return 0.75
        if (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso")
            return 0.06
        if (editorPreview.activeMaskKind === "alpha" || isAiMaskKind(editorPreview.activeMaskKind))
            return 0.5
        return 0.22
    }

    function nudgeActiveSelectionExpand(delta) {
        editorPreview.activeMaskSelectionExpand = clamp(editorPreview.activeMaskSelectionExpand + delta, -100, 100)
    }

    function nudgeActiveMaskFeather(delta) {
        const minimum = (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.0 : 0.05
        const maximum = (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.2 : 1.0
        editorPreview.activeMaskFeather = clamp(
                    editorPreview.activeMaskFeather + delta,
                    minimum,
                    maximum)
    }

    function maxPanX() {
        return Math.max(0, zoomPreviewContent.width - zoomPreviewFlick.width);
    }

    function sideBySideCompareEnabled() {
        return editorPreview.compareMode &&
               compareViewMode === "side-by-side" &&
               referenceDisplaySource.length > 0
    }

    function wipeCompareEnabled() {
        return editorPreview.compareMode &&
               compareViewMode === "wipe" &&
               referenceDisplaySource.length > 0
    }

    function maxPanY() {
        return Math.max(0, zoomPreviewContent.height - zoomPreviewFlick.height);
    }

    function clampPanPosition() {
        zoomPreviewFlick.contentX = Math.max(0, Math.min(maxPanX(), zoomPreviewFlick.contentX));
        zoomPreviewFlick.contentY = Math.max(0, Math.min(maxPanY(), zoomPreviewFlick.contentY));
    }

    function rectContainsPoint(rect, x, y) {
        return rect.width > 0 && rect.height > 0 &&
               x >= rect.x && x <= rect.x + rect.width &&
               y >= rect.y && y <= rect.y + rect.height
    }

    function imagePaintedRectInZoomViewport(image) {
        const paintedWidth = Math.max(0, image.paintedWidth)
        const paintedHeight = Math.max(0, image.paintedHeight)
        const origin = image.mapToItem(zoomPreviewFlick,
                                       (image.width - paintedWidth) / 2,
                                       (image.height - paintedHeight) / 2)
        return Qt.rect(origin.x, origin.y, paintedWidth, paintedHeight)
    }

    function itemRectInZoomViewport(item) {
        const origin = item.mapToItem(zoomPreviewFlick, 0, 0)
        return Qt.rect(origin.x, origin.y, Math.max(0, item.width), Math.max(0, item.height))
    }

    function editedFitImageRectInZoomViewport() {
        if (editorPreview.displayPreviewNativeAvailable)
            return itemRectInZoomViewport(editedPreviewFitNative)
        if (wipeCompareEnabled())
            return imagePaintedRectInZoomViewport(editedPreviewFitWipe)
        return imagePaintedRectInZoomViewport(editedPreviewFit)
    }

    function referenceFitImageRectInZoomViewport() {
        if (wipeCompareEnabled())
            return imagePaintedRectInZoomViewport(referencePreviewFitWipe)
        return imagePaintedRectInZoomViewport(referencePreviewFit)
    }

    function editedZoomImageRectForZoom(zoomLevel) {
        const editedWidth = editedPreviewPixelWidth(editedPreviewZoom) * zoomLevel
        const editedHeight = editedPreviewPixelHeight(editedPreviewZoom) * zoomLevel
        const referenceWidth = sideBySideCompareEnabled()
                               ? imagePixelWidth(referencePreviewZoom) * zoomLevel
                               : 0
        const contentWidth = contentWidthForZoom(zoomLevel)
        const contentHeight = contentHeightForZoom(zoomLevel)
        const x = sideBySideCompareEnabled()
                  ? Math.max(0, (contentWidth - (editedWidth + referenceWidth + 16)) / 2)
                  : Math.max(0, (contentWidth - editedWidth) / 2)
        const y = Math.max(0, (contentHeight - editedHeight) / 2)
        return Qt.rect(x, y, editedWidth, editedHeight)
    }

    function referenceZoomImageRectForZoom(zoomLevel) {
        const referenceWidth = imagePixelWidth(referencePreviewZoom) * zoomLevel
        const referenceHeight = imagePixelHeight(referencePreviewZoom) * zoomLevel
        const contentWidth = contentWidthForZoom(zoomLevel)
        const contentHeight = contentHeightForZoom(zoomLevel)
        if (sideBySideCompareEnabled()) {
            const editedRect = editedZoomImageRectForZoom(zoomLevel)
            return Qt.rect(editedRect.x + editedRect.width + 16,
                           Math.max(0, (contentHeight - referenceHeight) / 2),
                           referenceWidth,
                           referenceHeight)
        }
        return Qt.rect(Math.max(0, (contentWidth - referenceWidth) / 2),
                       Math.max(0, (contentHeight - referenceHeight) / 2),
                       referenceWidth,
                       referenceHeight)
    }

    function centerZoomPreview() {
        zoomPreviewFlick.contentX = maxPanX() / 2;
        zoomPreviewFlick.contentY = maxPanY() / 2;
    }

    function updatePreviewTargetEdge() {
        const viewportWidth = Math.max(1, previewSurface.width - 4);
        const viewportHeight = Math.max(1, previewSurface.height - 4);
        const compareWidth = sideBySideCompareEnabled() ? Math.max(1, (viewportWidth - 8) / 2) : viewportWidth;
        const previewSupersample = 1.5;
        const zoomMultiplier = fitToView ? previewSupersample : Math.min(3.0, Math.max(previewSupersample, manualZoom));
        editorPreview.previewTargetEdge = Math.round(Math.max(compareWidth, viewportHeight) * Screen.devicePixelRatio * zoomMultiplier);
    }

    function updatePreviewViewport() {
        if (!editorPreview.hasAsset || fitToView || editorPreview.compareMode ||
                !zoomPreviewFlick.visible ||
                !(editedPreviewZoom.visible || editedPreviewZoomNative.visible)) {
            editorPreview.setPreviewViewport(0, 0, 0, 0, 0, false)
            return
        }

        const left = Math.max(editedPreviewZoom.x, zoomPreviewFlick.contentX)
        const top = Math.max(editedPreviewZoom.y, zoomPreviewFlick.contentY)
        const right = Math.min(editedPreviewZoom.x + editedPreviewZoom.width,
                               zoomPreviewFlick.contentX + zoomPreviewFlick.width)
        const bottom = Math.min(editedPreviewZoom.y + editedPreviewZoom.height,
                                zoomPreviewFlick.contentY + zoomPreviewFlick.height)
        const visibleWidth = right - left
        const visibleHeight = bottom - top

        if (visibleWidth <= 1 || visibleHeight <= 1 || editedPreviewZoom.width <= 0 || editedPreviewZoom.height <= 0) {
            editorPreview.setPreviewViewport(0, 0, 0, 0, 0, false)
            return
        }

        const normalizedX = (left - editedPreviewZoom.x) / editedPreviewZoom.width
        const normalizedY = (top - editedPreviewZoom.y) / editedPreviewZoom.height
        const normalizedWidth = visibleWidth / editedPreviewZoom.width
        const normalizedHeight = visibleHeight / editedPreviewZoom.height
        const patchEdge = Math.round(Math.max(visibleWidth, visibleHeight) * Screen.devicePixelRatio)
        editorPreview.setPreviewViewport(
                    normalizedX,
                    normalizedY,
                    normalizedWidth,
                    normalizedHeight,
                    patchEdge,
                    true)
    }

    function imagePixelWidth(image) {
        return Math.max(1, image.implicitWidth > 0 ? image.implicitWidth : image.sourceSize.width);
    }

    function imagePixelHeight(image) {
        return Math.max(1, image.implicitHeight > 0 ? image.implicitHeight : image.sourceSize.height);
    }

    function editedOutputPixelWidth() {
        const sourceWidth = Number(editorPreview.sourceImageWidth || 0)
        if (sourceWidth <= 0)
            return 0
        const cropWidth = editorPreview.cropEditing ? 1.0 : clamp(Number(editorPreview.cropRectWidth || 1.0), 0.0, 1.0)
        return Math.max(1, Math.round(sourceWidth * Math.max(0.01, cropWidth)))
    }

    function editedOutputPixelHeight() {
        const sourceHeight = Number(editorPreview.sourceImageHeight || 0)
        if (sourceHeight <= 0)
            return 0
        const cropHeight = editorPreview.cropEditing ? 1.0 : clamp(Number(editorPreview.cropRectHeight || 1.0), 0.0, 1.0)
        return Math.max(1, Math.round(sourceHeight * Math.max(0.01, cropHeight)))
    }

    function editedPreviewPixelWidth(image) {
        const outputWidth = editedOutputPixelWidth()
        return outputWidth > 0 ? outputWidth : imagePixelWidth(image)
    }

    function editedPreviewPixelHeight(image) {
        const outputHeight = editedOutputPixelHeight()
        return outputHeight > 0 ? outputHeight : imagePixelHeight(image)
    }

    function preserveAspectFitSize(sourceWidth, sourceHeight, targetWidth, targetHeight) {
        const safeSourceWidth = Math.max(1, sourceWidth)
        const safeSourceHeight = Math.max(1, sourceHeight)
        const scale = Math.min(Math.max(0, targetWidth) / safeSourceWidth,
                               Math.max(0, targetHeight) / safeSourceHeight)
        return Qt.size(safeSourceWidth * scale, safeSourceHeight * scale)
    }

    function fitZoomLevel() {
        if (editedDisplaySource.length === 0)
            return 1.0;

        const imageWidth = editedPreviewPixelWidth(editedPreviewZoom);
        const imageHeight = editedPreviewPixelHeight(editedPreviewZoom);
        const availableWidth = sideBySideCompareEnabled()
                               ? Math.max(1, (fitPreviewRow.width - 8) / 2)
                               : Math.max(1, fitPreviewRow.width);
        const availableHeight = Math.max(1, fitPreviewRow.height);
        return Math.min(availableWidth / imageWidth, availableHeight / imageHeight);
    }

    function outlinePointCount(outlines) {
        if (!outlines || outlines.length === undefined)
            return 0
        let total = 0
        for (let i = 0; i < outlines.length; ++i) {
            const entry = outlines[i]
            if (!entry)
                continue
            if (entry.x !== undefined && entry.y !== undefined) {
                total += 1
            } else if (entry.length !== undefined) {
                total += entry.length
            }
        }
        return total
    }

    function activeSelectionOutlinePointCount() {
        const pathPointCount = root.outlinePointCount(editorPreview.activeMaskOutlinePaths)
        if (pathPointCount > 0)
            return pathPointCount
        return root.outlinePointCount(editorPreview.activeMaskOutlinePoints)
    }

    function smartSelectionPreviewOutlinePointCount() {
        return root.outlinePointCount(editorPreview.smartSelectPreviewOutlinePoints)
    }

    function selectionOutlineAnimationPointCount() {
        const activePointCount = root.activeSelectionOutlinePointCount()
        if (activePointCount >= 2)
            return activePointCount
        if (objectPickArmed && editorPreview.smartSelectPreviewActive)
            return root.smartSelectionPreviewOutlinePointCount()
        return activePointCount
    }

    function selectionOutlineAnimationShouldRun() {
        if (editorPreview.maskPreviewMode !== "marching-ants")
            return false
        if (!root.visible || (!editorPreview.hasActiveMask && !(objectPickArmed && editorPreview.smartSelectPreviewActive)))
            return false
        if (editorPreview.maskPreviewTemporarilyHidden === true || editorPreview.compareMode ||
                editorPreview.cropEditing || inspectorMode !== "masks")
            return false
        if (editorPreview.brushStrokeActive || editorPreview.smartSelectStrokeActive ||
                root.tabletBrushStrokeActive)
            return false
        const pointCount = root.selectionOutlineAnimationPointCount()
        return pointCount >= 2 && pointCount <= root.selectionOutlineAnimationPointBudget
    }

    function currentZoomLevel() {
        return fitToView ? fitZoomLevel() : manualZoom;
    }

    function publishPreviewViewportForCurrentView() {
        updatePreviewTargetEdge()
        updatePreviewViewport()
    }

    Timer {
        id: previewViewportSettleTimer
        interval: 160
        repeat: false
        onTriggered: root.publishPreviewViewportForCurrentView()
    }

    function schedulePreviewViewportUpdate(immediate) {
        if (immediate === true) {
            previewViewportSettleTimer.stop()
            Qt.callLater(root.publishPreviewViewportForCurrentView)
            return
        }
        previewViewportSettleTimer.restart()
    }

    NumberAnimation on selectionOutlinePhase {
        from: 0
        to: 24
        duration: 900
        loops: Animation.Infinite
        running: root.selectionOutlineAnimationEnabled
    }

    onManualZoomChanged: {
        schedulePreviewViewportUpdate(false)
    }
    onFitToViewChanged: {
        schedulePreviewViewportUpdate(false)
    }
    onCompareViewModeChanged: {
        compareWipePosition = 0.5
        schedulePreviewViewportUpdate(true)
    }

    Connections {
        target: editorPreview
        function onCompareModeChanged() {
            if (editorPreview.compareMode && editorPreview.whiteBalanceSampling)
                editorPreview.whiteBalanceSampling = false;
            schedulePreviewViewportUpdate(true);
        }
    }

    function contentWidthForZoom(zoomLevel) {
        const editedWidth = editedDisplaySource.length > 0 ? editedPreviewPixelWidth(editedPreviewZoom) * zoomLevel : 0;
        const referenceWidth = sideBySideCompareEnabled()
                               ? imagePixelWidth(referencePreviewZoom) * zoomLevel
                               : 0;
        const combinedWidth = sideBySideCompareEnabled() && referenceWidth > 0
                              ? editedWidth + referenceWidth + 16
                              : editedWidth;
        return Math.max(zoomPreviewFlick.width, combinedWidth);
    }

    function contentHeightForZoom(zoomLevel) {
        const editedHeight = editedDisplaySource.length > 0 ? editedPreviewPixelHeight(editedPreviewZoom) * zoomLevel : 0;
        const referenceHeight = sideBySideCompareEnabled()
                                ? imagePixelHeight(referencePreviewZoom) * zoomLevel
                                : 0;
        return Math.max(zoomPreviewFlick.height, Math.max(editedHeight, referenceHeight));
    }

    function setZoomLevel(value, pivotX, pivotY) {
        const nextZoom = clampZoom(value);
        const currentZoom = currentZoomLevel();
        if (Math.abs(nextZoom - currentZoom) < 0.0001)
            return;

        const fitZoom = fitZoomLevel();
        if (nextZoom <= fitZoom * 1.01) {
            fitToView = true;
            zoomPreviewFlick.contentX = 0;
            zoomPreviewFlick.contentY = 0;
            return;
        }

        const viewportPivotX = clamp(pivotX === undefined ? zoomPreviewFlick.width / 2 : pivotX,
                                     0,
                                     Math.max(0, zoomPreviewFlick.width));
        const viewportPivotY = clamp(pivotY === undefined ? zoomPreviewFlick.height / 2 : pivotY,
                                     0,
                                     Math.max(0, zoomPreviewFlick.height));
        const oldEditedRect = fitToView ? editedFitImageRectInZoomViewport()
                                        : editedZoomImageRectForZoom(currentZoom)
        const oldReferenceRect = (editorPreview.compareMode && referenceDisplaySource.length > 0)
                                 ? (fitToView ? referenceFitImageRectInZoomViewport()
                                              : referenceZoomImageRectForZoom(currentZoom))
                                 : oldEditedRect
        const oldFocusX = (fitToView ? 0 : zoomPreviewFlick.contentX) + viewportPivotX
        const oldFocusY = (fitToView ? 0 : zoomPreviewFlick.contentY) + viewportPivotY
        const useReferenceAnchor = editorPreview.compareMode &&
                                   referenceDisplaySource.length > 0 &&
                                   rectContainsPoint(oldReferenceRect, oldFocusX, oldFocusY) &&
                                   !rectContainsPoint(oldEditedRect, oldFocusX, oldFocusY)
        const oldAnchorRect = useReferenceAnchor ? oldReferenceRect : oldEditedRect
        const normalizedAnchorX = oldAnchorRect.width > 0
                                  ? clamp((oldFocusX - oldAnchorRect.x) / oldAnchorRect.width, 0, 1)
                                  : 0.5
        const normalizedAnchorY = oldAnchorRect.height > 0
                                  ? clamp((oldFocusY - oldAnchorRect.y) / oldAnchorRect.height, 0, 1)
                                  : 0.5

        fitToView = false;
        manualZoom = nextZoom;

        const newAnchorRect = useReferenceAnchor ? referenceZoomImageRectForZoom(nextZoom)
                                                 : editedZoomImageRectForZoom(nextZoom)
        zoomPreviewFlick.contentX = newAnchorRect.x + normalizedAnchorX * newAnchorRect.width - viewportPivotX;
        zoomPreviewFlick.contentY = newAnchorRect.y + normalizedAnchorY * newAnchorRect.height - viewportPivotY;
        clampPanPosition();
    }

    function hslColorForChannel(channelName) {
        for (let index = 0; index < hslChannels.length; ++index) {
            if (hslChannels[index].name === channelName)
                return hslChannels[index].color;
        }

        return "#c99a52";
    }

    function selectedHslColor() {
        return hslColorForChannel(editorPreview.hslChannel);
    }

    function rainbowTrack() {
        return ["#d45a62", "#dd8a39", "#d7bd42", "#61b56f", "#49b8c4", "#4e86d9", "#8b6be3", "#c85ac1", "#d45a62"];
    }

    function gradingPreviewColor(hueValue, saturationValue, luminanceValue) {
        const hue = ((hueValue % 1.0) + 1.0) % 1.0;
        const saturation = Math.max(0.18, Math.min(1.0, Math.abs(saturationValue) / 100.0));
        const lightness = Math.max(0.38, Math.min(0.76, 0.56 + luminanceValue / 280.0));
        return Qt.hsla(hue, saturation, lightness, 1.0);
    }

    function activeAdjustmentGroups() {
        return selectedAdjustmentGroups.filter(function(groupKey) {
            return adjustmentGroupAvailable(groupKey);
        });
    }

    function adjustmentGroupAvailable(groupKey) {
        return String(groupKey || "") !== "raw" || editorPreview.rawControlsAvailable;
    }

    function availableAdjustmentGroups() {
        return adjustmentGroups.filter(function(group) {
            return adjustmentGroupAvailable(group.key);
        });
    }

    function hasAdjustmentGroup(groupKey) {
        return adjustmentGroupAvailable(groupKey) && selectedAdjustmentGroups.indexOf(groupKey) >= 0;
    }

    function toggleAdjustmentGroup(groupKey) {
        if (!adjustmentGroupAvailable(groupKey))
            return;
        let nextGroups = selectedAdjustmentGroups.slice(0);
        const existingIndex = nextGroups.indexOf(groupKey);
        if (existingIndex >= 0) {
            if (nextGroups.length === 1)
                return;
            nextGroups.splice(existingIndex, 1);
        } else {
            nextGroups.push(groupKey);
        }
        selectedAdjustmentGroups = nextGroups;
    }

    function selectedBatchAssetIds() {
        return catalogAssets.selectedAssetIds;
    }

    function copyCurrentAdjustments() {
        editorPreview.copyCurrentSettings();
    }

    function pasteCopiedAdjustments() {
        const groups = activeAdjustmentGroups();
        const targetIds = selectedBatchAssetIds();
        const recipeJson = editorPreview.copiedSettingsRecipeJson();
        if (recipeJson.length === 0)
            return;
        editorPreview.pasteCopiedSettings(groups);
        catalogAssets.applyPresetRecipeJsonToAssetIds(targetIds, recipeJson, groups);
    }

    function syncCurrentAdjustments() {
        const targetIds = selectedBatchAssetIds();
        if (targetIds.length === 0)
            return;
        catalogAssets.applyRecipeJsonToAssetIds(targetIds, editorPreview.currentRecipeJson(), activeAdjustmentGroups());
    }

    function applyPresetToSelection(presetName) {
        if (!presetName || presetName.length === 0)
            return;

        const groups = activeAdjustmentGroups();
        const targetIds = selectedBatchAssetIds();
        const recipeJson = editorPreview.presetPreviewRecipeJson(presetName, groups, editorPreview.presetIntensity);
        if (recipeJson.length === 0)
            return;

        editorPreview.applyPreset(presetName, groups);
        catalogAssets.applyRecipeJsonToAssetIds(targetIds, recipeJson, groups);
        selectedPresetName = presetName;
    }

    function saveCurrentPreset() {
        const presetName = presetDraftName.trim();
        if (presetName.length === 0)
            return;
        const presetPack = presetDraftPack.trim().length > 0 ? presetDraftPack.trim() : "User Presets";
        if (selectedPresetName.length > 0 && editorPreview.isUserPreset(selectedPresetName)) {
            if (presetName !== selectedPresetName && !editorPreview.renamePreset(selectedPresetName, presetName))
                return;
            if (!editorPreview.movePresetToPack(presetName, presetPack))
                return;
            presetDraftName = presetName;
            presetDraftPack = presetPack;
            selectedPresetName = presetName;
            return;
        }

        if (editorPreview.savePresetToPack(presetName, presetPack, true)) {
            presetDraftName = presetName;
            presetDraftPack = presetPack;
            selectedPresetName = presetName;
        }
    }

    function selectAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson) {
        catalogAssets.selectIndex(index);
    }

    function selectRelativeAsset(offset) {
        if (catalogAssets.assetCount === 0)
            return;

        const baseIndex = catalogAssets.selectedIndex >= 0 ? catalogAssets.selectedIndex : 0;
        const nextIndex = Math.max(0, Math.min(catalogAssets.assetCount - 1, baseIndex + offset));
        if (nextIndex === baseIndex)
            return;

        catalogAssets.selectIndex(nextIndex);
    }

    Connections {
        target: editorPreview

        function onPreviewChanged() {
            editedDisplaySource = editorPreview.displayPreviewSource;
            referenceDisplaySource = editorPreview.displayReferencePreviewSource;
            schedulePreviewViewportUpdate(true);
        }

        function onActiveBrushStrokeChanged() {
            syncTemporaryBrushModifierState()
        }

        function onCompareModeChanged() {
            if (!fitToView)
                centerZoomPreview();
        }

        function onPresetsChanged() {
            if (selectedPresetName.length > 0 && editorPreview.presetNames.indexOf(selectedPresetName) >= 0) {
                presetDraftPack = editorPreview.presetPackName(selectedPresetName).length > 0
                        ? editorPreview.presetPackName(selectedPresetName)
                        : "User Presets"
                return
            }

            selectedPresetName = editorPreview.presetNames.length > 0 ? editorPreview.presetNames[0] : ""
            if (presetDraftName.length === 0 || selectedPresetName.length === 0) {
                presetDraftName = selectedPresetName
                presetDraftPack = selectedPresetName.length > 0 && editorPreview.presetPackName(selectedPresetName).length > 0
                        ? editorPreview.presetPackName(selectedPresetName)
                        : "User Presets"
            }
        }
    }

    Connections {
        target: catalogAssets

        function onSelectedIndexChanged() {
            if (catalogAssets.selectedIndex >= 0)
                filmstripView.positionViewAtIndex(catalogAssets.selectedIndex, ListView.Center);
        }
    }

    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: catalogAssets.selectedIndex > 0
        onActivated: selectRelativeAsset(-1)
    }

    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: catalogAssets.selectedIndex >= 0 && catalogAssets.selectedIndex < catalogAssets.assetCount - 1
        onActivated: selectRelativeAsset(1)
    }

    Shortcut {
        sequence: StandardKey.Deselect
        context: Qt.WindowShortcut
        enabled: editorPreview.hasActiveMask || editorPreview.smartSelectPreviewActive ||
                 objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed
        onActivated: deselectMaskEditing()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: editorPreview.hasActiveMask || editorPreview.smartSelectPreviewActive ||
                 objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed
        onActivated: deselectMaskEditing()
    }

    Shortcut {
        sequence: "Return"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" &&
                 editorPreview.hasActiveMask &&
                 editorPreview.activeMaskToolRole === "retouch" &&
                 !editorPreview.activeMaskCommitted
        onActivated: editorPreview.applyActiveRetouch()
    }

    Shortcut {
        sequence: "Enter"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" &&
                 editorPreview.hasActiveMask &&
                 editorPreview.activeMaskToolRole === "retouch" &&
                 !editorPreview.activeMaskCommitted
        onActivated: editorPreview.applyActiveRetouch()
    }

    Shortcut {
        sequence: "Backspace"
        context: Qt.WindowShortcut
        enabled: polygonDrawArmed && editorPreview.activeMaskPolygonPointCount > 0
        onActivated: editorPreview.removeLastActivePolygonPoint()
    }

    Shortcut {
        sequence: "Tab"
        context: Qt.WindowShortcut
        enabled: objectPickArmed && editorPreview.smartSelectCanCycleCandidates
        onActivated: editorPreview.cycleSmartSelectCandidate(1)
    }

    Shortcut {
        sequence: "Shift+Tab"
        context: Qt.WindowShortcut
        enabled: objectPickArmed && editorPreview.smartSelectCanCycleCandidates
        onActivated: editorPreview.cycleSmartSelectCandidate(-1)
    }

    Shortcut {
        sequence: "B"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasAsset && editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        onActivated: {
            selectionBrushRestorePending = false
            clearTransientTools()
            editorPreview.startSelectionBrush(false)
        }
    }

    Shortcut {
        sequence: "E"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasAsset && editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        onActivated: {
            selectionBrushRestorePending = false
            clearTransientTools()
            editorPreview.startSelectionBrush(true)
        }
    }

    Shortcut {
        sequence: "["
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.activeMaskKind === "brush"
        onActivated: editorPreview.activeMaskBrushSize = clamp(editorPreview.activeMaskBrushSize - 0.02, 0.01, 0.35)
    }

    Shortcut {
        sequence: "]"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.activeMaskKind === "brush"
        onActivated: editorPreview.activeMaskBrushSize = clamp(editorPreview.activeMaskBrushSize + 0.02, 0.01, 0.35)
    }

    Shortcut {
        sequence: "I"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        onActivated: editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted
    }

    Shortcut {
        sequence: "V"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasActiveMask
        readonly property string maskPreviewDisplayCycleContract: "legacy-editview-mask-preview-cycle|overlay|outline|marching-ants|hidden:v1"
        onActivated: {
            if (editorPreview.maskPreviewDisplayMode === "overlay")
                editorPreview.maskPreviewMode = "outline"
            else if (editorPreview.maskPreviewDisplayMode === "outline")
                editorPreview.maskPreviewMode = "marching-ants"
            else if (editorPreview.maskPreviewDisplayMode === "marching-ants")
                editorPreview.maskPreviewMode = "hidden"
            else
                editorPreview.maskPreviewMode = "overlay"
        }
    }

    Shortcut {
        sequence: "H"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasAsset
        onActivated: {
            clearTransientTools()
            editorPreview.startHealBrush()
        }
    }

    Shortcut {
        sequence: "Delete"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasActiveMask
        onActivated: editorPreview.removeActiveMask()
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Control) {
            controlModifierActive = true
            syncTemporaryBrushModifierState()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Alt) {
            if (!optionModifierActive) {
                optionModifierActive = true
                syncTemporaryBrushModifierState()
            }
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Space) {
            if (!spacePanActive)
                spacePanActive = true
            event.accepted = true
        }
    }

    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Control) {
            controlModifierActive = false
            syncTemporaryBrushModifierState()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Alt) {
            optionModifierActive = false
            syncTemporaryBrushModifierState()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Space) {
            spacePanActive = false
            event.accepted = true
        }
    }

    Component.onCompleted: {
        updatePreviewTargetEdge();
        editedDisplaySource = editorPreview.displayPreviewSource;
        referenceDisplaySource = editorPreview.displayReferencePreviewSource;
        resetInspectorScroll();
        if (editorPreview.presetNames.length > 0) {
            selectedPresetName = editorPreview.presetNames[0];
            presetDraftName = selectedPresetName;
            presetDraftPack = editorPreview.presetPackName(selectedPresetName).length > 0
                    ? editorPreview.presetPackName(selectedPresetName)
                    : "User Presets";
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 6

            Rectangle {
                Layout.preferredWidth: 42
                Layout.fillHeight: true
                radius: 0
                color: theme.alpha(theme.ink950, 0.46)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.32)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 14
                    anchors.bottomMargin: 14
                    spacing: 8

                    Repeater {
                        model: [
                            { "mode": "adjust", "icon": "adjust", "label": "Adjustments" },
                            { "mode": "crop", "icon": "crop", "label": "Crop / Frame" }
                        ]

                        delegate: EditRailButton {
                            required property var modelData

                            readonly property bool railActive: (modelData.mode === "adjust" && inspectorMode === "adjust" && inspectorTab === "adjust")
                                                               || (modelData.mode === "crop" && inspectorMode === "crop")

                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            label: modelData.label
                            iconName: modelData.icon
                            active: railActive
                            accent: theme.polar
                            onTriggered: activateRailMode(modelData.mode)
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 1
                        color: theme.alpha(theme.lineSoft, 0.62)
                    }

                    AppIcon {
                        Layout.alignment: Qt.AlignHCenter
                        name: leftPanelCollapsed ? "chevron-right" : "chevron-left"
                        color: theme.textMuted
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -10
                            cursorShape: Qt.PointingHandCursor
                            onClicked: leftPanelCollapsed = !leftPanelCollapsed
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: leftPanelCollapsed ? 0 : 176
                Layout.fillHeight: true
                visible: !leftPanelCollapsed
                radius: 0
                color: theme.alpha(theme.ink950, 0.36)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.24)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: "ASSET"
                            color: theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            font.letterSpacing: 1.0
                        }

                        AppIcon {
                            visible: false
                            name: "plus"
                            color: theme.textMuted
                            Layout.preferredWidth: 13
                            Layout.preferredHeight: 13
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 1
                        rowSpacing: 6

                        Repeater {
                            model: [
                                "Resolution  " + (editorPreview.displayPreviewNativeSize.width > 0 ? editorPreview.displayPreviewNativeSize.width + " x " + editorPreview.displayPreviewNativeSize.height : "Unknown"),
                                "Color space " + editorPreview.outputColorSpace,
                                "Camera      " + (editorPreview.cameraLabel.length > 0 ? editorPreview.cameraLabel : "Unknown"),
                                "Lens        " + (editorPreview.lensModel.length > 0 ? editorPreview.lensModel : "Unknown")
                            ]

                            delegate: Label {
                                required property string modelData

                                Layout.fillWidth: true
                                text: modelData
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 8
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 6
                                elide: Text.ElideNone
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: theme.alpha(theme.lineSoft, 0.24)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            Layout.fillWidth: true
                            text: "LAYOUT"
                            color: theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            font.letterSpacing: 1.0
                        }

                        Repeater {
                            model: [
                                { "label": inspectorWide ? "NARROW INSPECTOR" : "WIDE INSPECTOR", "icon": "split-view", "action": "wide", "active": inspectorWide },
                                { "label": filmstripCollapsed ? "SHOW FILMSTRIP" : "HIDE FILMSTRIP", "icon": "carousel", "action": "filmstrip", "active": !filmstripCollapsed },
                                { "label": inspectorCollapsed ? "SHOW INSPECTOR" : "HIDE INSPECTOR", "icon": "adjust", "action": "inspector", "active": !inspectorCollapsed }
                            ]

                            delegate: EditLayoutToggleRow {
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                label: modelData.label
                                iconName: modelData.icon
                                actionKey: modelData.action
                                active: modelData.active
                                onTriggered: function(actionKey) {
                                    if (actionKey === "wide")
                                        inspectorWide = !inspectorWide
                                    else if (actionKey === "filmstrip")
                                        filmstripCollapsed = !filmstripCollapsed
                                    else if (actionKey === "inspector")
                                        inspectorCollapsed = !inspectorCollapsed
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: false
                        spacing: 4

                        Repeater {
                            model: [
                                { "label": "ALL PHOTOS", "value": catalogAssets.assetCount > 0 ? catalogAssets.assetCount.toString() : "0", "icon": "library", "active": true },
                                { "label": "RECENT IMPORTS", "value": "128", "icon": "refresh", "active": false },
                                { "label": "FAVORITES", "value": "236", "icon": "carousel", "active": false },
                                { "label": "TRASH", "value": "32", "icon": "delete", "active": false }
                            ]

                            delegate: Rectangle {
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                radius: 0
                                color: modelData.active ? theme.alpha(theme.vectorGreen, 0.08) : "transparent"
                                border.width: 0

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 2
                                    visible: modelData.active
                                    color: theme.vectorGreen
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 10

                                    AppIcon {
                                        name: modelData.icon
                                        color: modelData.active ? theme.textMain : theme.textMuted
                                        Layout.preferredWidth: 12
                                        Layout.preferredHeight: 12
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: modelData.active ? theme.textStrong : theme.textMain
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                        font.letterSpacing: 0.35
                                        fontSizeMode: Text.HorizontalFit
                                        minimumPixelSize: 7
                                        elide: Text.ElideNone
                                    }

                                    Label {
                                        text: modelData.value
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 8
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        visible: false
                        color: theme.alpha(theme.lineSoft, 0.28)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: false
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: "COLLECTIONS"
                            color: theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            font.letterSpacing: 1.0
                        }

                        AppIcon {
                            name: "plus"
                            color: theme.textMuted
                            Layout.preferredWidth: 13
                            Layout.preferredHeight: 13
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: false
                        spacing: 4

                        Repeater {
                            model: [
                                { "label": "MOUNTAINS", "value": "124" },
                                { "label": "ICELAND", "value": "63" },
                                { "label": "ALPS", "value": "58" },
                                { "label": "PATAGONIA", "value": "37" },
                                { "label": "NORWAY", "value": "42" }
                            ]

                            delegate: RowLayout {
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    fontSizeMode: Text.HorizontalFit
                                    minimumPixelSize: 8
                                    elide: Text.ElideNone
                                }

                                Label {
                                    text: modelData.value
                                    color: theme.textFaint
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        visible: false
                        color: theme.alpha(theme.lineSoft, 0.28)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: false
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: "ACTIVE RECIPE"
                            color: theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            font.letterSpacing: 1.0
                        }

                        AppIcon {
                            name: "plus"
                            color: theme.textMuted
                            Layout.preferredWidth: 13
                            Layout.preferredHeight: 13
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: false
                        spacing: 0

                        Repeater {
                            model: [
                                { "label": "COLOR GRADE", "value": "100%", "icon": "adjust", "active": true, "locked": false },
                                { "label": "SKY ENHANCE", "value": "80%", "icon": "smart-select", "active": false, "locked": false },
                                { "label": "MIDTONE DETAIL", "value": "60%", "icon": "scope", "active": false, "locked": false },
                                { "label": "SUBJECT MASK", "value": "40%", "icon": "masks", "active": false, "locked": false },
                                { "label": "BACKGROUND", "value": "100%", "icon": "library", "active": false, "locked": true }
                            ]

                            delegate: Rectangle {
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                color: modelData.active ? theme.alpha(theme.vectorGreen, 0.08) : "transparent"

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 2
                                    visible: modelData.active
                                    color: theme.vectorGreen
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 9

                                    AppIcon {
                                        name: modelData.icon
                                        color: modelData.active ? theme.vectorGreen : theme.textMuted
                                        Layout.preferredWidth: 12
                                        Layout.preferredHeight: 12
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: modelData.active ? theme.textStrong : theme.textMain
                                        font.family: theme.fontUi
                                        font.pixelSize: 11
                                        fontSizeMode: Text.HorizontalFit
                                        minimumPixelSize: 8
                                        elide: Text.ElideNone
                                    }

                                    AppIcon {
                                        visible: modelData.locked
                                        name: "link"
                                        color: theme.textFaint
                                        Layout.preferredWidth: 10
                                        Layout.preferredHeight: 10
                                    }

                                    Label {
                                        text: modelData.value
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 8
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: theme.alpha(theme.lineSoft, 0.28)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: "Storage"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 10
                        }

                        Label {
                            text: "2.4 TB / 8 TB"
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 9
                        }
                    }
                }
            }

            BrandPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                headerVisible: false
                bodyMargin: 0
                bodySpacing: 8
                title: "Editor"

                Rectangle {
                    id: previewSurface
                    objectName: "editImageViewport"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 0
                    color: theme.ink950
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.34)
                    clip: true
                    onWidthChanged: {
                        schedulePreviewViewportUpdate(true)
                    }
                    onHeightChanged: {
                        schedulePreviewViewportUpdate(true)
                    }

                    Flickable {
                        id: canvasToolbar
                        objectName: "maskToolRail"
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 8
                        z: 120
                        height: visible ? 50 : 0
                        contentWidth: canvasToolbarRow.implicitWidth
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: contentWidth > width
                        visible: editorPreview.hasAsset && inspectorMode !== "crop"

                    Row {
                        id: canvasToolbarRow
                        spacing: 8

                        Rectangle {
                            width: selectToolColumn.implicitWidth + 16
                            height: selectToolColumn.implicitHeight + 10
                            radius: theme.radius8
                            color: theme.alpha(theme.ink950, objectPickArmed || lassoDrawArmed || polygonDrawArmed ? 0.96 : 0.86)
                            border.width: 1
                            border.color: theme.alpha(theme.accentWarm400, objectPickArmed || lassoDrawArmed || polygonDrawArmed ? 0.42 : 0.16)

                            Column {
                                id: selectToolColumn
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: "Select"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                Row {
                                    spacing: 4

                                    ActionButton {
                                        compact: true
                                        text: "Object"
                                        iconName: "smart-select"
                                        tooltipText: "Object Select"
                                        accent: theme.accentWarm400
                                        tone: active ? "primary" : "secondary"
                                        active: objectPickArmed && objectPickMode === "selection" && !objectPickCreateNew
                                        enabled: editorPreview.hasAsset
                                        onClicked: selectTool(objectPickArmed && objectPickMode === "selection" && !objectPickCreateNew ? "" : "smart")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Subtract"
                                        iconName: "smart-select"
                                        tooltipText: "Subtract Object"
                                        accent: theme.accentWarm400
                                        tone: active ? "primary" : "secondary"
                                        active: objectPickArmed && objectPickMode === "selection-subtract"
                                        enabled: editorPreview.hasAsset && editorPreview.hasActiveMask
                                        onClicked: selectTool(objectPickArmed && objectPickMode === "selection-subtract" ? "" : "smart-subtract")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Lasso"
                                        iconName: "lasso"
                                        tooltipText: "Lasso"
                                        accent: "#7ec97a"
                                        tone: active ? "primary" : "secondary"
                                        active: lassoDrawArmed
                                        onClicked: selectTool(lassoDrawArmed ? "" : "lasso")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Polygon"
                                        iconName: "polygon-tool"
                                        tooltipText: "Polygon"
                                        accent: "#7ec97a"
                                        tone: active ? "primary" : "secondary"
                                        active: polygonDrawArmed
                                        onClicked: {
                                            if (!polygonDrawArmed && editorPreview.activeMaskKind !== "polygon")
                                                editorPreview.startPolygonSelection(manualSelectionSubtractActive())
                                            selectTool(polygonDrawArmed ? "" : "polygon")
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            objectName: "brushToolHud"
                            width: brushToolColumn.implicitWidth + 16
                            height: brushToolColumn.implicitHeight + 10
                            radius: theme.radius8
                            color: theme.alpha(theme.ink950, editorPreview.activeMaskKind === "brush" &&
                                               editorPreview.activeMaskRetouchMode.length === 0 ? 0.96 : 0.86)
                            border.width: 1
                            border.color: theme.alpha(maskWorkspaceAccentColor(),
                                                      editorPreview.activeMaskKind === "brush" &&
                                                      editorPreview.activeMaskRetouchMode.length === 0 ? 0.34 : 0.14)

                            Column {
                                id: brushToolColumn
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: "Paint"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                Row {
                                    spacing: 4

                                    ActionButton {
                                        objectName: "maskSourceBrushAddButton"
                                        compact: true
                                        text: "Add"
                                        iconName: "brush-tool"
                                        tooltipText: "Brush Add"
                                        accent: maskWorkspaceAccentColor()
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskOperation === "add" &&
                                                editorPreview.activeMaskRetouchMode.length === 0
                                        onClicked: {
                                            selectionBrushRestorePending = false
                                            clearTransientTools()
                                            editorPreview.startSelectionBrush(false)
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Erase"
                                        iconName: "brush-subtract"
                                        tooltipText: "Brush Erase"
                                        accent: "#d36a5e"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskOperation === "subtract" &&
                                                editorPreview.activeMaskRetouchMode.length === 0
                                        onClicked: {
                                            selectionBrushRestorePending = false
                                            clearTransientTools()
                                            editorPreview.startSelectionBrush(true)
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Linear"
                                        iconName: "geometry"
                                        tooltipText: "Linear Gradient Mask"
                                        accent: maskWorkspaceAccentColor()
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "linear"
                                        onClicked: {
                                            clearTransientTools()
                                            inspectorTab = "masks"
                                            inspectorMode = "masks"
                                            editorPreview.addMask("linear")
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Radial"
                                        iconName: "masks"
                                        tooltipText: "Radial Gradient Mask"
                                        accent: maskWorkspaceAccentColor()
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "radial"
                                        onClicked: {
                                            clearTransientTools()
                                            inspectorTab = "masks"
                                            inspectorMode = "masks"
                                            editorPreview.addMask("radial")
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            objectName: "brushContextPanel"
                            width: 1
                            height: 1
                            visible: root.brushToolReady()
                        }

                        Rectangle {
                            visible: false
                            width: visible ? retouchToolColumn.implicitWidth + 16 : 0
                            height: visible ? retouchToolColumn.implicitHeight + 10 : 0
                            radius: theme.radius8
                            color: (objectPickArmed && objectPickMode === "remove") ||
                                   (editorPreview.activeMaskToolRole === "retouch")
                                   ? theme.alpha(theme.ink950, 0.96)
                                   : theme.alpha(theme.ink950, 0.86)
                            border.width: 1
                            border.color: theme.alpha("#d98962",
                                                      ((objectPickArmed && objectPickMode === "remove") ||
                                                       (editorPreview.activeMaskToolRole === "retouch")) ? 0.34 : 0.14)

                            Column {
                                id: retouchToolColumn
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: "Retouch"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                Row {
                                    spacing: 4

                                    ActionButton {
                                        compact: true
                                        text: "Remove Obj"
                                        iconName: "erase-tool"
                                        tooltipText: "Remove Object"
                                        accent: "#d98962"
                                        tone: active ? "primary" : "secondary"
                                        active: objectPickArmed && objectPickMode === "remove" && !objectPickCreateNew
                                        enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                        onClicked: selectTool(objectPickArmed && objectPickMode === "remove" && !objectPickCreateNew ? "" : "remove")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Remove Brush"
                                        iconName: "erase-tool"
                                        tooltipText: "Remove Brush"
                                        accent: "#d98962"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskRetouchMode === "remove"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.startRemoveBrush()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Dust"
                                        iconName: "erase-tool"
                                        tooltipText: "Dust Removal"
                                        accent: "#c2b36a"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskToolRole === "retouch" &&
                                                editorPreview.activeMaskRetouchMode === "dust"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.removeDustSpots()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Save Map"
                                        iconName: "save"
                                        tooltipText: "Save Sensor Dust Map"
                                        accent: "#c2b36a"
                                        enabled: editorPreview.activeMaskToolRole === "retouch" &&
                                                 editorPreview.activeMaskRetouchMode === "dust"
                                        onClicked: editorPreview.saveActiveDustMap()
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Sensor"
                                        iconName: "masks"
                                        tooltipText: "Apply Sensor Dust Map"
                                        accent: "#c2b36a"
                                        enabled: editorPreview.hasSensorDustMap
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.applySensorDustMap()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Red Eye"
                                        iconName: "erase-tool"
                                        tooltipText: "Red-Eye Removal"
                                        accent: "#d36a5e"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskToolRole === "retouch" &&
                                                editorPreview.activeMaskRetouchMode === "red-eye"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.removeRedEye()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Skin (Heuristic)"
                                        iconName: "brush-tool"
                                        tooltipText: "Skin Cleanup (Heuristic)"
                                        accent: "#d7a08c"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskToolRole === "retouch" &&
                                                editorPreview.activeMaskRetouchMode === "skin-cleanup"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.cleanupSkin()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Smooth (Experimental)"
                                        iconName: "brush-tool"
                                        tooltipText: "Frequency Smoothing (Experimental)"
                                        accent: "#c994d8"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskToolRole === "retouch" &&
                                                editorPreview.activeMaskRetouchMode === "frequency-smoothing"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.smoothSkinFrequency()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Heal"
                                        iconName: "brush-tool"
                                        tooltipText: "Heal"
                                        accent: "#72cbb2"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskRetouchMode === "heal"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.startHealBrush()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Patch"
                                        iconName: "brush-tool"
                                        tooltipText: "Patch"
                                        accent: "#72cbb2"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskRetouchMode === "patch"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.startPatchBrush()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Clone"
                                        iconName: "brush-tool"
                                        tooltipText: "Clone"
                                        accent: theme.accentWarm500
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskRetouchMode === "clone"
                                        onClicked: {
                                            clearTransientTools()
                                            if (editorPreview.activeMaskKind === "brush" &&
                                                    editorPreview.activeMaskRetouchMode === "clone") {
                                                selectTool("clone-source")
                                            } else {
                                                editorPreview.startCloneBrush()
                                                selectTool("clone-source")
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            visible: editorPreview.hasActiveMask || objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed
                            width: canvasActionsColumn.implicitWidth + 16
                            height: canvasActionsColumn.implicitHeight + 10
                            radius: theme.radius8
                            color: theme.alpha(theme.ink950, 0.90)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.22)

                            Column {
                                id: canvasActionsColumn
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: "Session"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                Row {
                                    spacing: 4

                                    ActionButton {
                                        compact: true
                                        text: "Deselect"
                                        iconName: "close"
                                        accent: "#f4f7fa"
                                        tone: "secondary"
                                        visible: editorPreview.hasActiveMask
                                        onClicked: deselectMaskEditing()
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Done"
                                        iconName: "close"
                                        accent: maskWorkspaceAccentColor()
                                        tone: "primary"
                                        onClicked: deselectMaskEditing()
                                    }
                                }
                            }
                        }
                    }
                    }

                    Item {
                        id: fitPreviewRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: canvasToolbar.visible ? canvasToolbar.bottom : parent.top
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        anchors.topMargin: canvasToolbar.visible ? 8 : 8
                        anchors.bottomMargin: 8
                        visible: fitToView

                    Image {
                        id: editedPreviewFit
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: sideBySideCompareEnabled() ? compareFitDivider.left : parent.right
                        anchors.rightMargin: sideBySideCompareEnabled() ? 8 : 0
                        source: editedDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        visible: editedDisplaySource.length > 0 && !wipeCompareEnabled() &&
                                 !editorPreview.displayPreviewNativeAvailable
                    }

                    EditorPreviewTextureItem {
                        id: editedPreviewFitNative
                        readonly property size fittedSize: preserveAspectFitSize(
                                                               editedPreviewPixelWidth(editedPreviewFit),
                                                               editedPreviewPixelHeight(editedPreviewFit),
                                                               editedPreviewFit.width,
                                                               editedPreviewFit.height)
                        x: editedPreviewFit.x + (editedPreviewFit.width - width) / 2
                        y: editedPreviewFit.y + Math.max(0, (editedPreviewFit.height - height) * 0.44)
                        width: Math.max(0, fittedSize.width)
                        height: Math.max(0, fittedSize.height)
                        visible: editedDisplaySource.length > 0 && !wipeCompareEnabled() &&
                                 editorPreview.displayPreviewNativeAvailable
                        controller: editorPreview
                    }

                    Rectangle {
                        id: compareFitDivider
                        visible: sideBySideCompareEnabled() &&
                                 (editedPreviewFit.visible || editedPreviewFitNative.visible)
                        width: 1
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: theme.alpha(theme.lineStrong, 0.42)
                    }

                    Image {
                        id: referencePreviewFit
                        anchors.left: compareFitDivider.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        source: referenceDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        visible: sideBySideCompareEnabled()
                    }

                    Image {
                        id: referencePreviewFitWipe
                        anchors.fill: parent
                        source: referenceDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        visible: wipeCompareEnabled()
                    }

                    Item {
                        id: editedPreviewFitWipeClip
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * compareWipePosition
                        clip: true
                        visible: wipeCompareEnabled()

                        Image {
                            id: editedPreviewFitWipe
                            x: 0
                            y: 0
                            width: fitPreviewRow.width
                            height: fitPreviewRow.height
                            source: editedDisplaySource
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            visible: editedDisplaySource.length > 0
                        }
                    }

                    Rectangle {
                        id: compareFitWipeHandle
                        visible: wipeCompareEnabled() && editedPreviewFitWipe.visible
                        x: Math.round(fitPreviewRow.width * compareWipePosition) - width / 2
                        y: 0
                        width: 2
                        height: parent.height
                        color: theme.alpha(theme.accentWarm400, 0.92)

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 10
                            width: 18
                            height: 24
                            radius: 9
                            color: theme.alpha(theme.ink950, 0.82)
                            border.width: 1
                            border.color: theme.alpha(theme.accentWarm400, 0.5)
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -8
                            cursorShape: Qt.SizeHorCursor
                            drag.target: null
                            onPressed: function(mouse) {
                                compareWipePosition = clamp((parent.x + mouse.x) / Math.max(1, fitPreviewRow.width), 0.05, 0.95)
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                compareWipePosition = clamp((parent.x + mouse.x) / Math.max(1, fitPreviewRow.width), 0.05, 0.95)
                            }
                        }
                    }

                    CropOverlay {
                        anchors.fill: parent
                        active: fitToView && editorPreview.cropEditing && !editorPreview.compareMode &&
                                (editedPreviewFit.visible || editedPreviewFitNative.visible)
                        imageX: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.x : editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2
                        imageY: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.y : editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2
                        imageWidth: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.width) : Math.max(0, editedPreviewFit.paintedWidth)
                        imageHeight: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.height) : Math.max(0, editedPreviewFit.paintedHeight)
                        cropX: editorPreview.cropRectX
                        cropY: editorPreview.cropRectY
                        cropWidth: editorPreview.cropRectWidth
                        cropHeight: editorPreview.cropRectHeight
                        aspectRatio: editorPreview.cropAspectRatio
                        guideMode: cropGuideMode
                        onRectEdited: function(x, y, width, height) {
                            editorPreview.updateCropRect(x, y, width, height);
                        }
                    }

                    TransformOverlay {
                        anchors.fill: parent
                        active: fitToView && inspectorMode === "crop" && !editorPreview.cropEditing &&
                                !editorPreview.compareMode && !editorPreview.whiteBalanceSampling &&
                                (editedPreviewFit.visible || editedPreviewFitNative.visible)
                        imageX: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.x : editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2
                        imageY: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.y : editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2
                        imageWidth: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.width) : Math.max(0, editedPreviewFit.paintedWidth)
                        imageHeight: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.height) : Math.max(0, editedPreviewFit.paintedHeight)
                        transformScale: editorPreview.transformScale
                        transformRotation: editorPreview.transformRotation
                        onScaleEdited: function(scale) { editorPreview.transformScale = scale }
                        onRotationEdited: function(rotation) { editorPreview.transformRotation = rotation }
                    }

                    MaskCanvasOverlay {
                        objectName: "editImageViewportMaskCanvasOverlay"
                        anchors.fill: parent
                        visible: fitToView &&
                                 (inspectorMode === "masks" || objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed) &&
                                 !editorPreview.compareMode && !editorPreview.cropEditing &&
                                 (editedPreviewFit.visible || editedPreviewFitNative.visible)
                        editRoot: root
                        imageX: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.x : editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2
                        imageY: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.y : editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2
                        imageWidth: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.width) : Math.max(0, editedPreviewFit.paintedWidth)
                        imageHeight: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.height) : Math.max(0, editedPreviewFit.paintedHeight)
                        cropX: editorPreview.cropRectX
                        cropY: editorPreview.cropRectY
                        cropWidth: editorPreview.cropRectWidth
                        cropHeight: editorPreview.cropRectHeight
                        transformRotation: editorPreview.transformRotation
                    }
                }

                MouseArea {
                    anchors.fill: fitPreviewRow
                    visible: fitToView && editorPreview.whiteBalanceSampling && !editorPreview.compareMode &&
                             !editorPreview.cropEditing &&
                             (editedPreviewFit.visible || editedPreviewFitNative.visible)
                    cursorShape: Qt.CrossCursor
                    onClicked: function (mouse) {
                        const imageX = editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.x : editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2;
                        const imageY = editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.y : editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2;
                        const imageWidth = editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.width : editedPreviewFit.paintedWidth;
                        const imageHeight = editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.height : editedPreviewFit.paintedHeight;

                        if (mouse.x < imageX || mouse.x > imageX + imageWidth || mouse.y < imageY || mouse.y > imageY + imageHeight) {
                            editorPreview.whiteBalanceSampling = false;
                            return;
                        }

                        editorPreview.sampleWhiteBalanceAt((mouse.x - imageX) / imageWidth, (mouse.y - imageY) / imageHeight);
                    }
                }

                Flickable {
                    id: zoomPreviewFlick
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: canvasToolbar.visible ? canvasToolbar.bottom : parent.top
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.topMargin: canvasToolbar.visible ? 8 : 8
                    anchors.bottomMargin: 8
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    visible: !fitToView
                    contentWidth: zoomPreviewContent.width
                    contentHeight: zoomPreviewContent.height
                    onContentWidthChanged: clampPanPosition()
                    onContentHeightChanged: clampPanPosition()
                    onContentXChanged: schedulePreviewViewportUpdate(false)
                    onContentYChanged: schedulePreviewViewportUpdate(false)
                    onWidthChanged: schedulePreviewViewportUpdate(false)
                    onHeightChanged: schedulePreviewViewportUpdate(false)

                    Item {
                        id: zoomPreviewContent
                        width: sideBySideCompareEnabled()
                               ? Math.max(zoomPreviewFlick.width, editedPreviewZoom.width + referencePreviewZoom.width + 16)
                               : Math.max(zoomPreviewFlick.width, editedPreviewZoom.width)
                        height: Math.max(zoomPreviewFlick.height, Math.max(editedPreviewZoom.height, referencePreviewZoom.height))

                        Image {
                            id: editedPreviewZoom
                            x: sideBySideCompareEnabled()
                               ? Math.max(0, (zoomPreviewContent.width - (editedPreviewZoom.width + referencePreviewZoom.width + 16)) / 2)
                               : Math.max(0, (zoomPreviewContent.width - editedPreviewZoom.width) / 2)
                            y: Math.max(0, (zoomPreviewContent.height - editedPreviewZoom.height) / 2)
                            width: editedPreviewPixelWidth(editedPreviewZoom) * manualZoom
                            height: editedPreviewPixelHeight(editedPreviewZoom) * manualZoom
                            source: editedDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: editedDisplaySource.length > 0 && !wipeCompareEnabled() &&
                                     !editorPreview.displayPreviewNativeAvailable
                        }

                        EditorPreviewTextureItem {
                            id: editedPreviewZoomNative
                            x: editedPreviewZoom.x
                            y: editedPreviewZoom.y
                            width: editedPreviewZoom.width
                            height: editedPreviewZoom.height
                            visible: editedDisplaySource.length > 0 && !wipeCompareEnabled() &&
                                     editorPreview.displayPreviewNativeAvailable
                            controller: editorPreview
                        }

                        Image {
                            readonly property var overlayRect: editorPreview.displayPreviewOverlayRect
                            readonly property bool overlayRectValid: overlayRect !== undefined && overlayRect !== null
                            x: editedPreviewZoom.x + (overlayRectValid ? overlayRect.x : 0) * editedPreviewZoom.width
                            y: editedPreviewZoom.y + (overlayRectValid ? overlayRect.y : 0) * editedPreviewZoom.height
                            width: (overlayRectValid ? overlayRect.width : 0) * editedPreviewZoom.width
                            height: (overlayRectValid ? overlayRect.height : 0) * editedPreviewZoom.height
                            source: editorPreview.displayPreviewOverlaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: !fitToView && (editedPreviewZoom.visible || editedPreviewZoomNative.visible) &&
                                     overlayRectValid &&
                                     editorPreview.displayPreviewOverlaySource.length > 0 &&
                                     width > 1 && height > 1
                        }

                        Image {
                            id: referencePreviewZoom
                            x: sideBySideCompareEnabled() ? editedPreviewZoom.x + editedPreviewZoom.width + 16 : editedPreviewZoom.x
                            y: Math.max(0, (zoomPreviewContent.height - referencePreviewZoom.height) / 2)
                            width: imagePixelWidth(referencePreviewZoom) * manualZoom
                            height: imagePixelHeight(referencePreviewZoom) * manualZoom
                            source: referenceDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: sideBySideCompareEnabled()
                        }

                        Image {
                            id: referencePreviewZoomWipe
                            x: Math.max(0, (zoomPreviewContent.width - referencePreviewZoomWipe.width) / 2)
                            y: Math.max(0, (zoomPreviewContent.height - referencePreviewZoomWipe.height) / 2)
                            width: imagePixelWidth(referencePreviewZoom) * manualZoom
                            height: imagePixelHeight(referencePreviewZoom) * manualZoom
                            source: referenceDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: wipeCompareEnabled()
                        }

                        Item {
                            id: editedPreviewZoomWipeClip
                            x: referencePreviewZoomWipe.x
                            y: referencePreviewZoomWipe.y
                            width: referencePreviewZoomWipe.width * compareWipePosition
                            height: referencePreviewZoomWipe.height
                            clip: true
                            visible: wipeCompareEnabled() && editedDisplaySource.length > 0

                            Image {
                                id: editedPreviewZoomWipe
                                x: 0
                                y: 0
                                width: referencePreviewZoomWipe.width
                                height: referencePreviewZoomWipe.height
                                source: editedDisplaySource
                                fillMode: Image.Stretch
                                asynchronous: true
                                cache: false
                            }
                        }

                        Rectangle {
                            id: compareZoomWipeHandle
                            visible: wipeCompareEnabled() && referencePreviewZoomWipe.visible
                            x: editedPreviewZoomWipeClip.x + editedPreviewZoomWipeClip.width - width / 2
                            y: editedPreviewZoomWipeClip.y
                            width: 2
                            height: editedPreviewZoomWipeClip.height
                            color: theme.alpha(theme.accentWarm400, 0.92)

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -8
                                cursorShape: Qt.SizeHorCursor
                                onPressed: function(mouse) {
                                    compareWipePosition = clamp((parent.x + mouse.x - referencePreviewZoomWipe.x) / Math.max(1, referencePreviewZoomWipe.width), 0.05, 0.95)
                                }
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return
                                    compareWipePosition = clamp((parent.x + mouse.x - referencePreviewZoomWipe.x) / Math.max(1, referencePreviewZoomWipe.width), 0.05, 0.95)
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: editorPreview.whiteBalanceSampling && !editorPreview.compareMode &&
                                     !editorPreview.cropEditing &&
                                     (editedPreviewZoom.visible || editedPreviewZoomNative.visible)
                            cursorShape: Qt.CrossCursor
                            onClicked: function (mouse) {
                                if (mouse.x < editedPreviewZoom.x || mouse.x > editedPreviewZoom.x + editedPreviewZoom.width || mouse.y < editedPreviewZoom.y || mouse.y > editedPreviewZoom.y + editedPreviewZoom.height) {
                                    editorPreview.whiteBalanceSampling = false;
                                    return;
                                }

                                editorPreview.sampleWhiteBalanceAt((mouse.x - editedPreviewZoom.x) / editedPreviewZoom.width, (mouse.y - editedPreviewZoom.y) / editedPreviewZoom.height);
                            }
                        }

                        CropOverlay {
                            anchors.fill: parent
                            active: !fitToView && editorPreview.cropEditing && !editorPreview.compareMode &&
                                    (editedPreviewZoom.visible || editedPreviewZoomNative.visible)
                            imageX: editedPreviewZoom.x
                            imageY: editedPreviewZoom.y
                            imageWidth: editedPreviewZoom.width
                            imageHeight: editedPreviewZoom.height
                            cropX: editorPreview.cropRectX
                            cropY: editorPreview.cropRectY
                            cropWidth: editorPreview.cropRectWidth
                            cropHeight: editorPreview.cropRectHeight
                            aspectRatio: editorPreview.cropAspectRatio
                            guideMode: cropGuideMode
                            onRectEdited: function(x, y, width, height) {
                                editorPreview.updateCropRect(x, y, width, height);
                            }
                        }

                        TransformOverlay {
                            anchors.fill: parent
                            active: !fitToView && inspectorMode === "crop" && !editorPreview.cropEditing &&
                                    !editorPreview.compareMode && !editorPreview.whiteBalanceSampling &&
                                    (editedPreviewZoom.visible || editedPreviewZoomNative.visible)
                            imageX: editedPreviewZoom.x
                            imageY: editedPreviewZoom.y
                            imageWidth: editedPreviewZoom.width
                            imageHeight: editedPreviewZoom.height
                            transformScale: editorPreview.transformScale
                            transformRotation: editorPreview.transformRotation
                            onScaleEdited: function(scale) { editorPreview.transformScale = scale }
                            onRotationEdited: function(rotation) { editorPreview.transformRotation = rotation }
                        }

                        MaskCanvasOverlay {
                            objectName: "editImageViewportMaskCanvasOverlay"
                            anchors.fill: parent
                            visible: !fitToView &&
                                     (inspectorMode === "masks" || objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed) &&
                                     !editorPreview.compareMode && !editorPreview.cropEditing &&
                                     (editedPreviewZoom.visible || editedPreviewZoomNative.visible)
                            editRoot: root
                            imageX: editedPreviewZoom.x
                            imageY: editedPreviewZoom.y
                            imageWidth: editedPreviewZoom.width
                            imageHeight: editedPreviewZoom.height
                            cropX: editorPreview.cropRectX
                            cropY: editorPreview.cropRectY
                            cropWidth: editorPreview.cropRectWidth
                            cropHeight: editorPreview.cropRectHeight
                            transformRotation: editorPreview.transformRotation
                        }
                    }
                }

                WheelHandler {
                    target: null
                    enabled: editorPreview.hasAsset
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function (event) {
                        const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y;
                        if (delta === 0)
                            return;

                        const zoomFactor = delta > 0 ? 1.05 : (1 / 1.05);
                        const surfacePivot = event.position || Qt.point(previewSurface.width / 2, previewSurface.height / 2);
                        const viewportPivot = previewSurface.mapToItem(zoomPreviewFlick, surfacePivot.x, surfacePivot.y);
                        setZoomLevel(currentZoomLevel() * zoomFactor, viewportPivot.x, viewportPivot.y);
                        event.accepted = true;
                    }
                }

                MouseArea {
                    anchors.fill: zoomPreviewFlick
                    visible: !fitToView && !editorPreview.whiteBalanceSampling && !editorPreview.cropEditing
                             && (spacePanActive ||
                                 (!(inspectorMode === "masks" && editorPreview.hasActiveMask && editorPreview.activeMaskKind === "brush")
                                  && !objectPickArmed && !polygonDrawArmed && !lassoDrawArmed && !cloneSourcePickArmed))
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    cursorShape: maxPanX() > 0 || maxPanY() > 0 || spacePanActive
                                 ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
                                 : Qt.ArrowCursor

                    property real lastX: 0
                    property real lastY: 0

                    onPressed: function (mouse) {
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }

                    onPositionChanged: function (mouse) {
                        if (!pressed)
                            return;

                        zoomPreviewFlick.contentX -= mouse.x - lastX;
                        zoomPreviewFlick.contentY -= mouse.y - lastY;
                        clampPanPosition();
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }
                }

                MouseArea {
                    anchors.fill: zoomPreviewFlick
                    visible: !fitToView && !editorPreview.whiteBalanceSampling && !editorPreview.cropEditing
                    acceptedButtons: Qt.MiddleButton | Qt.RightButton
                    preventStealing: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    z: 220

                    property real lastX: 0
                    property real lastY: 0

                    onPressed: function (mouse) {
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }

                    onPositionChanged: function (mouse) {
                        if (!pressed)
                            return;

                        zoomPreviewFlick.contentX -= mouse.x - lastX;
                        zoomPreviewFlick.contentY -= mouse.y - lastY;
                        clampPanPosition();
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: theme.radius8
                    visible: editorPreview.displayPreviewSource.length === 0
                    color: theme.ink800

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8

                        Label {
                            text: catalogAssets.selectedAssetId.length > 0 ? "Preview unavailable for this asset" : "Select an image in Library"
                            color: "#f2f4f6"
                            font.family: theme.fontUi
                            font.pixelSize: 18
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: catalogAssets.selectedAssetId.length > 0 ? "The source preview could not be decoded for this asset." : "The editor is selection-driven. Pick an image in Library to load the preview and recipe."
                            color: "#8f98a2"
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.preferredWidth: 420
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: filmstripCollapsed ? 40 : 92
                radius: 0
                color: theme.alpha(theme.ink900, 0.92)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.32)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: previewStatusText()
                            visible: text.length > 0
                            color: inspectorMode === "masks" ? theme.alpha(maskWorkspaceAccentColor(), 0.95) : theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 12
                            font.letterSpacing: 0.3
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 8
                            elide: Text.ElideNone
                        }

                        ActionButton {
                            text: filmstripCollapsed ? "Show Filmstrip" : "Hide Filmstrip"
                            iconName: filmstripCollapsed ? "chevron-right" : "chevron-left"
                            compact: true
                            showText: false
                            tooltipText: filmstripCollapsed ? "Show filmstrip" : "Hide filmstrip"
                            implicitWidth: 28
                            implicitHeight: 28
                            onClicked: filmstripCollapsed = !filmstripCollapsed
                        }

                        Rectangle {
                            radius: 0
                            color: theme.alpha(theme.ink950, 0.44)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.34)
                            implicitWidth: zoomModeRow.implicitWidth + 6
                            implicitHeight: 30

                            RowLayout {
                                id: zoomModeRow
                                anchors.fill: parent
                                anchors.margins: 3
                                spacing: 3

                                ActionButton {
                                    text: "Fit"
                                    iconName: "crop"
                                    compact: true
                                    active: fitToView
                                    showText: false
                                    tooltipText: "Fit preview to canvas"
                                    onClicked: {
                                        fitToView = true;
                                        zoomPreviewFlick.contentX = 0;
                                        zoomPreviewFlick.contentY = 0;
                                    }
                                }

                                ActionButton {
                                    text: "1:1"
                                    compact: true
                                    active: !fitToView && Math.abs(manualZoom - 1.0) < 0.01
                                    iconText: "1:1"
                                    showText: false
                                    tooltipText: "View at 1:1"
                                    onClicked: {
                                        setZoomLevel(1.0);
                                        centerZoomPreview();
                                    }
                                }

                                ActionButton {
                                    text: "Compare"
                                    iconName: "compare"
                                    compact: true
                                    active: editorPreview.compareMode
                                    enabled: editorPreview.hasAsset
                                    showText: false
                                    tooltipText: "Toggle compare"
                                    onClicked: editorPreview.compareMode = !editorPreview.compareMode
                                }

                                ActionButton {
                                    text: "Reference"
                                    iconName: "source"
                                    compact: true
                                    active: editorPreview.hasReferenceImage
                                    enabled: editorPreview.hasAsset
                                    showText: false
                                    tooltipText: editorPreview.hasReferenceImage
                                                 ? ("Reference: " + editorPreview.referenceImageName)
                                                 : "Choose reference image"
                                    onClicked: openReferenceImageDialog()
                                }

                                ActionButton {
                                    text: "Clear Reference"
                                    iconName: "reset"
                                    compact: true
                                    visible: editorPreview.hasReferenceImage
                                    enabled: editorPreview.hasAsset
                                    showText: false
                                    tooltipText: "Clear reference image"
                                    onClicked: editorPreview.clearReferenceImage()
                                }

                                ActionButton {
                                    text: "Split"
                                    iconName: "split-view"
                                    compact: true
                                    visible: editorPreview.compareMode
                                    active: compareViewMode === "side-by-side"
                                    accent: theme.accentCool500
                                    showText: false
                                    tooltipText: "Split compare"
                                    onClicked: compareViewMode = "side-by-side"
                                }

                                ActionButton {
                                    text: "Wipe"
                                    iconName: "wipe"
                                    compact: true
                                    visible: editorPreview.compareMode
                                    active: compareViewMode === "wipe"
                                    accent: theme.accentCool500
                                    showText: false
                                    tooltipText: "Wipe compare"
                                    onClicked: compareViewMode = "wipe"
                                }
                            }
                        }

                        Label {
                            text: (fitToView ? "Fit" : (Math.round(manualZoom * 100) + "%")) +
                                  (editorPreview.compareMode
                                   ? (compareViewMode === "wipe" ? " · Wipe" : " · Split")
                                   : "")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }
                    }

                    Item {
                        id: filmstripHolder
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !filmstripCollapsed

                        readonly property int thumbWidth: 82
                        readonly property int thumbHeight: 52
                        readonly property int thumbOuterWidth: 90

                        ListView {
                            id: filmstripView
                            width: filmstripHolder.width
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            clip: true
                            orientation: ListView.Horizontal
                            boundsBehavior: Flickable.StopAtBounds
                            spacing: 8
                            model: catalogAssets
                            currentIndex: catalogAssets.selectedIndex

                            function updateVisibleAssetRange() {
                                if (!catalogAssets)
                                    return
                                if (!visible || count <= 0 || filmstripHolder.thumbOuterWidth <= 0) {
                                    catalogAssets.setVisibleAssetRange(-1, -1)
                                    return
                                }
                                const stride = filmstripHolder.thumbOuterWidth + spacing
                                const first = Math.max(0, Math.floor(contentX / stride))
                                const visibleCount = Math.max(1, Math.ceil(width / stride) + 1)
                                catalogAssets.setVisibleAssetRange(first, Math.min(count - 1, first + visibleCount - 1))
                            }

                            Component.onCompleted: updateVisibleAssetRange()
                            onContentXChanged: updateVisibleAssetRange()
                            onWidthChanged: updateVisibleAssetRange()
                            onCountChanged: updateVisibleAssetRange()
                            onVisibleChanged: updateVisibleAssetRange()

                            delegate: EditFilmstripThumb {
                                required property int index
                                required property string assetId
                                required property string thumbnailSource

                                width: filmstripHolder.thumbOuterWidth
                                height: filmstripView.height
                                assetIndex: index
                                thumbnailWidth: filmstripHolder.thumbWidth
                                thumbnailHeight: filmstripHolder.thumbHeight
                                current: catalogAssets.selectedIndex === index
                                selected: catalogAssets.assetSelected(assetId)
                                onActivated: function(assetIndex, modifiers) {
                                    if (modifiers & (Qt.ControlModifier | Qt.MetaModifier))
                                        catalogAssets.toggleSelectionIndex(assetIndex)
                                    else
                                        catalogAssets.selectIndex(assetIndex)
                                }
                            }

                            footer: Item {
                                width: 10
                                height: filmstripView.height
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: catalogAssets.assetCount === 0
                            text: "Filmstrip follows your library selection"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 0
                visible: false
                radius: 0
                color: theme.alpha(theme.ink950, 0.42)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.26)
                clip: true

                RowLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.preferredWidth: 260
                        Layout.fillHeight: true
                        color: "transparent"
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.20)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 9

                            Label {
                                text: "CURVES"
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                font.letterSpacing: 1.0
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                Rectangle {
                                    anchors.fill: parent
                                    color: theme.alpha(theme.ink950, 0.54)
                                    border.width: 1
                                    border.color: theme.alpha(theme.lineSoft, 0.24)
                                }

                                ToneCurveEditor {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    compactMode: true
                                    accent: editorPreview.curveChannel === "Luma"
                                            ? "#d8dce0"
                                            : (editorPreview.curveChannel === "Red"
                                            ? "#d45a62"
                                            : (editorPreview.curveChannel === "Green"
                                               ? "#61b56f"
                                               : (editorPreview.curveChannel === "Blue"
                                                  ? "#4e86d9"
                                                   : "#c99a52")))
                                    selectedChannel: editorPreview.curveChannel
                                    curveMode: editorPreview.activeCurveMode
                                    pointsModel: editorPreview.activeCurvePoints
                                    presetNames: editorPreview.curvePresetNames
                                    histogramRedData: editorPreview.histogramRed
                                    histogramGreenData: editorPreview.histogramGreen
                                    histogramBlueData: editorPreview.histogramBlue
                                    histogramLuminanceData: editorPreview.histogramLuminance
                                    histogramLogScale: histogramLogScale
                                    onChannelSelected: editorPreview.curveChannel = channel
                                    onPointsEdited: editorPreview.activeCurvePoints = points
                                    onPresetSelected: function(preset) { editorPreview.applyCurvePreset(preset) }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 398
                        Layout.fillHeight: true
                        color: "transparent"
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.20)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 12

                            Label {
                                text: "COLOR WHEELS"
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                font.letterSpacing: 1.0
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 12

                                ColorWheelControl {
                                    title: "Shadows"
                                    hue: editorPreview.gradingShadowsHue
                                    saturation: editorPreview.gradingShadowsSaturation
                                    onHueEdited: function(value) { editorPreview.gradingShadowsHue = value }
                                    onSaturationEdited: function(value) { editorPreview.gradingShadowsSaturation = value }
                                }

                                ColorWheelControl {
                                    title: "Midtones"
                                    hue: editorPreview.gradingMidtonesHue
                                    saturation: editorPreview.gradingMidtonesSaturation
                                    onHueEdited: function(value) { editorPreview.gradingMidtonesHue = value }
                                    onSaturationEdited: function(value) { editorPreview.gradingMidtonesSaturation = value }
                                }

                                ColorWheelControl {
                                    title: "Highlights"
                                    hue: editorPreview.gradingHighlightsHue
                                    saturation: editorPreview.gradingHighlightsSaturation
                                    onHueEdited: function(value) { editorPreview.gradingHighlightsHue = value }
                                    onSaturationEdited: function(value) { editorPreview.gradingHighlightsSaturation = value }
                                }

                                ColorWheelControl {
                                    title: "Global"
                                    hue: editorPreview.temperature
                                    saturation: editorPreview.vibrance
                                    onHueEdited: function(value) { editorPreview.temperature = value }
                                    onSaturationEdited: function(value) { editorPreview.vibrance = value }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.20)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 18

                                Label {
                                    text: "HSL"
                                    color: theme.textMain
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    font.letterSpacing: 1.0
                                }

                                Label { text: "HUE"; color: theme.textFaint; font.family: theme.fontMono; font.pixelSize: 8 }
                                Label { text: "SATURATION"; color: theme.textFaint; font.family: theme.fontMono; font.pixelSize: 8 }
                                Label { text: "LUMINANCE"; color: theme.vectorGreen; font.family: theme.fontMono; font.pixelSize: 8 }
                            }

                            Repeater {
                                model: hslChannels

                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Rectangle {
                                        Layout.preferredWidth: 10
                                        Layout.preferredHeight: 10
                                        radius: 5
                                        color: modelData.color
                                    }

                                    ToolValueSlider {
                                        Layout.fillWidth: true
                                        label: modelData.name + " luminance"
                                        valueText: modelData.name === editorPreview.hslChannel ? (editorPreview.luminanceAdjustment / 100).toFixed(2) : "0.00"
                                        tooltipText: modelData.name + " luminance adjustment"
                                        from: -100
                                        to: 100
                                        value: modelData.name === editorPreview.hslChannel ? editorPreview.luminanceAdjustment : 0
                                        onMoved: {
                                            editorPreview.hslChannel = modelData.name
                                            editorPreview.luminanceAdjustment = value
                                        }
                                    }

                                    Label {
                                        Layout.preferredWidth: 38
                                        horizontalAlignment: Text.AlignRight
                                        text: modelData.name === editorPreview.hslChannel ? (editorPreview.luminanceAdjustment / 100).toFixed(2) : "0.00"
                                        color: theme.textMain
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.preferredWidth: inspectorCollapsed ? 24 : (inspectorWide ? 472 : 372)
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                visible: inspectorCollapsed
                radius: theme.radius8
                color: theme.ink850

                Label {
                    anchors.centerIn: parent
                    text: "<"
                    color: "#d5dbe1"
                    font.family: "Menlo"
                    font.pixelSize: 12
                    rotation: 180
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: inspectorCollapsed = false
                }
            }

            BrandPanel {
                anchors.fill: parent
                visible: !inspectorCollapsed
                headerVisible: false
                bodyMargin: 0
                bodySpacing: 8
                title: "Inspector"

                Rectangle {
                    visible: true
                    implicitHeight: inspectorSummary.implicitHeight + 16
                    Layout.fillWidth: true
                    radius: 0
                    color: theme.alpha(theme.ink950, 0.34)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.28)

                    ColumnLayout {
                        id: inspectorSummary
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Rectangle {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: 0
                                color: theme.alpha(theme.ink950, 0.34)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.34)

                                Label {
                                    anchors.centerIn: parent
                                    text: "<"
                                    color: "#d5dbe1"
                                    font.family: "Menlo"
                                    font.pixelSize: 12
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: inspectorCollapsed = true
                                }
                            }

                            Repeater {
                                model: [
                                    { "key": "adjust", "label": "ADJUST" },
                                    { "key": "agent", "label": "AGENT" },
                                    { "key": "masks", "label": "MASKS" },
                                    { "key": "scopes", "label": "SCOPES" },
                                    { "key": "info", "label": "INFO" }
                                ]

                                delegate: EditInspectorTabButton {
                                    required property var modelData

                                    Layout.preferredWidth: implicitWidth
                                    Layout.preferredHeight: 26
                                    label: modelData.label
                                    tabKey: modelData.key
                                    selected: inspectorTab === modelData.key
                                    accent: theme.polar
                                    onSelectedRequested: function(tabKey) {
                                        inspectorTab = tabKey
                                        inspectorMode = tabKey === "masks" ? "masks" : "adjust"
                                    }
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ActionButton {
                                text: inspectorWide ? "Narrow Inspector" : "Wide Inspector"
                                iconName: "split-view"
                                showText: false
                                tooltipText: inspectorWide ? "Use narrow inspector" : "Use wide inspector"
                                compact: true
                                active: inspectorWide
                                font.pixelSize: 10
                                implicitHeight: 28
                                implicitWidth: 28
                                onClicked: inspectorWide = !inspectorWide
                            }

                            ActionButton {
                                text: "Reset"
                                iconName: "reset"
                                showText: false
                                tooltipText: "Reset adjustments"
                                compact: true
                                enabled: editorPreview.hasAsset
                                font.pixelSize: 10
                                implicitHeight: 28
                                implicitWidth: 28
                                onClicked: editorPreview.resetAdjustments()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            ActionButton {
                                text: editorPreview.compareMode ? "Compare On" : "Compare"
                                iconName: "compare"
                                active: editorPreview.compareMode
                                compact: true
                                enabled: editorPreview.hasAsset
                                showText: false
                                tooltipText: editorPreview.compareMode ? "Disable compare" : "Enable compare"
                                font.pixelSize: 10
                                implicitHeight: 24
                                onClicked: editorPreview.compareMode = !editorPreview.compareMode
                            }

                            ActionButton {
                                text: "Reference"
                                iconName: "source"
                                active: editorPreview.hasReferenceImage
                                compact: true
                                enabled: editorPreview.hasAsset
                                showText: false
                                tooltipText: editorPreview.hasReferenceImage
                                             ? ("Reference: " + editorPreview.referenceImageName)
                                             : "Choose reference image"
                                font.pixelSize: 10
                                implicitHeight: 24
                                onClicked: openReferenceImageDialog()
                            }

                            ActionButton {
                                text: "Clear Reference"
                                iconName: "reset"
                                compact: true
                                visible: editorPreview.hasReferenceImage
                                enabled: editorPreview.hasAsset
                                showText: false
                                tooltipText: "Clear reference image"
                                font.pixelSize: 10
                                implicitHeight: 24
                                onClicked: editorPreview.clearReferenceImage()
                            }

                            ActionButton {
                                text: compareViewMode === "wipe" ? "Wipe" : "Split"
                                iconName: compareViewMode === "wipe" ? "wipe" : "split-view"
                                compact: true
                                visible: editorPreview.compareMode
                                showText: false
                                tooltipText: compareViewMode === "wipe" ? "Switch to split compare" : "Switch to wipe compare"
                                font.pixelSize: 10
                                implicitHeight: 24
                                onClicked: compareViewMode = compareViewMode === "wipe" ? "side-by-side" : "wipe"
                            }

                            Label {
                                text: "Output"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                font.letterSpacing: 0.4
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 140
                                model: ["sRGB", "Display P3", "Adobe RGB", "ProPhoto RGB", "Rec.709", "Rec.2020"]
                                currentIndex: Math.max(0, model.indexOf(editorPreview.outputColorSpace))
                                onActivated: editorPreview.outputColorSpace = currentText
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                text: (catalogAssets.selectedRecipeJson.length > 0 ? "Edited" : "Base")
                                      + (editorPreview.maskCount > 0 ? " / Masks" : "")
                                      + " · " + editorPreview.outputColorSpace
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                font.letterSpacing: 0.35
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: false && inspectorMode === "adjust"
                            radius: 0
                            color: theme.alpha(theme.ink950, 0.38)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.32)
                            implicitHeight: batchToolsColumn.implicitHeight + 16

                            ColumnLayout {
                                id: batchToolsColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    ActionButton {
                                        Layout.fillWidth: true
                                        text: "Copy"
                                        compact: true
                                        enabled: editorPreview.hasAsset
                                        onClicked: copyCurrentAdjustments()
                                    }

                                    ActionButton {
                                        Layout.fillWidth: true
                                        text: "Paste"
                                        compact: true
                                        enabled: editorPreview.hasAsset && editorPreview.hasCopiedSettings
                                        onClicked: pasteCopiedAdjustments()
                                    }

                                    ActionButton {
                                        Layout.fillWidth: true
                                        text: "Sync"
                                        compact: true
                                        enabled: editorPreview.hasAsset && catalogAssets.selectedAssetCount > 0
                                        onClicked: syncCurrentAdjustments()
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 1
                                        Layout.preferredHeight: 20
                                        color: theme.alpha(theme.lineSoft, 0.42)
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 24
                                        Layout.preferredHeight: 24
                                        radius: 6
                                        color: batchToolsExpanded ? theme.alpha(theme.accentWarm500, 0.14) : "transparent"
                                        border.width: 1
                                        border.color: batchToolsExpanded ? theme.alpha(theme.accentWarm500, 0.3) : theme.alpha(theme.lineSoft, 0.42)

                                        AppIcon {
                                            anchors.centerIn: parent
                                            name: "chevron-right"
                                            rotation: batchToolsExpanded ? 90 : 0
                                            color: theme.textFaint
                                            implicitWidth: 9
                                            implicitHeight: 9
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: batchToolsExpanded = !batchToolsExpanded
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: batchToolsExpanded
                                    spacing: 6

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: catalogAssets.selectedAssetCount > 1
                                                  ? catalogAssets.selectedAssetCount + " selected"
                                                  : "Single photo"
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                        }

                                        Item { Layout.fillWidth: true }

                                        ActionButton {
                                            text: "Clear"
                                            compact: true
                                            enabled: catalogAssets.selectedAssetCount > 1
                                            onClicked: catalogAssets.clearSecondarySelection()
                                        }
                                    }

                                    Label {
                                        text: "Copy/paste groups"
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 4

                                        Repeater {
                                            model: availableAdjustmentGroups()

                                            delegate: ActionButton {
                                                required property var modelData

                                                text: modelData.label
                                                compact: true
                                                active: hasAdjustmentGroup(modelData.key)
                                                onClicked: toggleAdjustmentGroup(modelData.key)
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 1
                                        color: theme.alpha(theme.lineSoft, 0.28)
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        ToneComboBox {
                                            Layout.fillWidth: true
                                            model: editorPreview.presetNames
                                            currentIndex: selectedPresetName.length > 0
                                                          ? editorPreview.presetNames.indexOf(selectedPresetName)
                                                          : (editorPreview.presetNames.length > 0 ? 0 : -1)
                                            onActivated: {
                                                selectedPresetName = currentText
                                                presetDraftName = currentText
                                                presetDraftPack = editorPreview.presetPackName(currentText).length > 0
                                                        ? editorPreview.presetPackName(currentText)
                                                        : "User Presets"
                                            }
                                        }

                                        ActionButton {
                                            text: "Apply"
                                            compact: true
                                            tone: "primary"
                                            enabled: selectedPresetName.length > 0 && editorPreview.presetRecipeJson(selectedPresetName).length > 0
                                            onClicked: applyPresetToSelection(selectedPresetName)
                                        }
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "PRESET INTENSITY"
                                        valueText: Math.round(editorPreview.presetIntensity * 100) + "%"
                                        from: 0
                                        to: 1
                                        defaultValue: 1
                                        value: editorPreview.presetIntensity
                                        onValueDragged: function(value) { editorPreview.presetIntensity = value }
                                        onResetRequested: editorPreview.presetIntensity = 1
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        visible: selectedPresetName.length > 0
                                                 && editorPreview.presetChangedControls(selectedPresetName, activeAdjustmentGroups()).length > 0

                                        Repeater {
                                            model: editorPreview.presetChangedControls(selectedPresetName, activeAdjustmentGroups()).slice(0, 8)

                                            delegate: FilterChip {
                                                required property string modelData

                                                text: modelData
                                                active: true
                                                enabled: false
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 5
                                        visible: activeAgentModelIsOllama()

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Label {
                                                objectName: "agentOllamaInstallCommandLabel"
                                                Layout.fillWidth: true
                                                text: agentModelInstallCommandText()
                                                color: theme.textMuted
                                                font.family: theme.fontMono
                                                font.pixelSize: 10
                                                wrapMode: Text.WrapAnywhere
                                                maximumLineCount: 2
                                                elide: Text.ElideNone
                                            }

                                            ActionButton {
                                                objectName: "agentOllamaProbeButton"
                                                text: "Test"
                                                iconName: "check"
                                                tone: "secondary"
                                                enabled: activeAgentModelIsOllama() && !root.agentEditController.running && !root.agentEditController.modelInstallRunning
                                                onClicked: updateAgentModelSetupMessage(root.agentEditController.probeSelectedModel())
                                            }

                                            ActionButton {
                                                objectName: "agentOllamaPullButton"
                                                text: root.agentEditController.modelInstallRunning ? "Pulling" : "Pull"
                                                iconName: "download"
                                                tone: "secondary"
                                                enabled: activeAgentModelIsOllama() && !root.agentEditController.running && !root.agentEditController.modelInstallRunning
                                                onClicked: updateAgentModelSetupMessage(root.agentEditController.installSelectedModel(true))
                                            }
                                        }

                                        Label {
                                            objectName: "agentOllamaInstallStatusLabel"
                                            Layout.fillWidth: true
                                            visible: agentModelSetupMessage.length > 0 || root.agentEditController.modelInstallStatus !== "Idle"
                                            text: agentModelSetupMessage.length > 0 ? agentModelSetupMessage : root.agentEditController.modelInstallStatus
                                            color: root.agentEditController.modelInstallStatus === "Install failed" ? theme.error : theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideNone
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        ToneTextField {
                                            Layout.fillWidth: true
                                            objectName: "presetNameField"
                                            placeholderText: "Preset name"
                                            text: presetDraftName
                                            onTextEdited: presetDraftName = text
                                            onAccepted: saveCurrentPreset()
                                        }

                                        ToneTextField {
                                            Layout.preferredWidth: 118
                                            objectName: "presetPackField"
                                            placeholderText: "Pack"
                                            text: presetDraftPack
                                            onTextEdited: presetDraftPack = text
                                            onAccepted: saveCurrentPreset()
                                        }

                                        ActionButton {
                                            text: "Save"
                                            compact: true
                                            tone: "primary"
                                            enabled: editorPreview.hasAsset && presetDraftName.trim().length > 0
                                            onClicked: saveCurrentPreset()
                                        }

                                        ConfirmActionButton {
                                            text: "Delete"
                                            compact: true
                                            enabled: selectedPresetName.length > 0 && editorPreview.presetRecipeJson(selectedPresetName).length > 0
                                            helperText: "Delete this saved preset"
                                            onConfirmed: {
                                                if (editorPreview.deletePreset(selectedPresetName)) {
                                                    selectedPresetName = ""
                                                    presetDraftName = ""
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ScrollView {
                    id: inspectorScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    Column {
                        width: inspectorScroll.availableWidth
                        spacing: 10

                        Loader {
                            width: parent.width
                            active: inspectorMode === "adjust" && inspectorTab === "agent"
                            visible: active
                            sourceComponent: agentPanelComponent
                        }

                        Component {
                            id: agentPanelComponent

                        Rectangle {
                            width: parent.width
                            visible: true
                            radius: 0
                            color: theme.alpha(theme.ink950, 0.34)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.28)
                            implicitHeight: agentPanelLayout.implicitHeight + 20

                            ColumnLayout {
                                id: agentPanelLayout
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 9

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Agent Run"
                                    subtitle: root.agentEditController.status

                                    GridLayout {
                                        objectName: "editAgentStyleChooser"
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        columns: width >= 500 ? 2 : 1
                                        columnSpacing: 6
                                        rowSpacing: 6

                                        Repeater {
                                            model: root.agentEditController.availableStyles

                                            delegate: AgentStyleTile {
                                                required property var modelData

                                                styleData: modelData
                                                selected: root.agentEditController.selectedStyleId === String(modelData.id || "")
                                                tileEnabled: !root.agentEditController.running
                                                onSelectedRequested: function(styleId) {
                                                    root.agentEditController.selectedStyleId = styleId
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        text: {
                                            for (let index = 0; index < root.agentEditController.availableStyles.length; ++index) {
                                                const style = root.agentEditController.availableStyles[index]
                                                if (style.id === root.agentEditController.selectedStyleId)
                                                    return style.goal || ""
                                            }
                                            return ""
                                        }
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }

                                    Label {
                                        objectName: "editAgentStyleChangeSummaryLabel"
                                        Layout.fillWidth: true
                                        visible: agentLatestStyleChangeSummary().length > 0
                                        text: agentLatestStyleChangeSummary()
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        maximumLineCount: 3
                                        elide: Text.ElideNone
                                    }

                                    ActionButton {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        text: agentStyleBuilderExpanded ? "Hide Style Builder" : "Create Style"
                                        iconName: "plus"
                                        compact: true
                                        enabled: !root.agentEditController.running
                                        onClicked: agentStyleBuilderExpanded = !agentStyleBuilderExpanded
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 7
                                        visible: agentStyleBuilderExpanded

                                        ToneTextField {
                                            Layout.fillWidth: true
                                            text: agentStyleDraftName
                                            placeholderText: "Style name"
                                            enabled: !root.agentEditController.running
                                            onTextEdited: agentStyleDraftName = text
                                        }

                                        TextArea {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 74
                                            text: agentStyleDraftGoal
                                            placeholderText: "Editing outcome, taste rules, and constraints"
                                            wrapMode: TextEdit.WordWrap
                                            enabled: !root.agentEditController.running
                                            color: theme.textStrong
                                            selectedTextColor: theme.ink950
                                            selectionColor: theme.focusRing
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            onTextChanged: {
                                                if (activeFocus)
                                                    agentStyleDraftGoal = text
                                            }
                                            background: Rectangle {
                                                radius: theme.radiusControl
                                                color: theme.alpha(theme.glassBase, 0.38)
                                                border.width: 1
                                                border.color: theme.alpha(theme.glassEdge, 0.24)
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            objectName: "agentPermissionModeLabel"
                                            text: "Permission mode"
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                        }

                                        Flow {
                                            objectName: "agentPermissionModeChooser"
                                            Layout.fillWidth: true
                                            spacing: 6

                                            AgentPermissionModeButton {
                                                objectName: "agentPermissionModeConservativeButton"
                                                mode: "conservative"
                                                selectedMode: agentStylePermissionMode
                                                text: "Conservative"
                                                helperText: "Use global tone, color, detail, and conservative crop permissions."
                                                enabled: !root.agentEditController.running
                                                onModeSelected: function(mode) { setAgentStylePermissionMode(mode) }
                                            }

                                            AgentPermissionModeButton {
                                                objectName: "agentPermissionModeBalancedButton"
                                                mode: "balanced"
                                                selectedMode: agentStylePermissionMode
                                                text: "Balanced"
                                                helperText: "Allow global edits, crop, and local masks while keeping retouch off."
                                                enabled: !root.agentEditController.running
                                                onModeSelected: function(mode) { setAgentStylePermissionMode(mode) }
                                            }

                                            AgentPermissionModeButton {
                                                objectName: "agentPermissionModeFullEditButton"
                                                mode: "full_edit"
                                                selectedMode: agentStylePermissionMode
                                                text: "Full edit"
                                                helperText: "Allow global edits, masks, layers, and LUTs while keeping retouch off."
                                                enabled: !root.agentEditController.running
                                                onModeSelected: function(mode) { setAgentStylePermissionMode(mode) }
                                            }

                                            AgentPermissionModeButton {
                                                objectName: "agentPermissionModeFullEditRetouchButton"
                                                mode: "full_edit_retouch"
                                                selectedMode: agentStylePermissionMode
                                                text: "Full edit + retouch"
                                                helperText: "Allow the full edit toolset, including local masks and minor retouch."
                                                enabled: !root.agentEditController.running
                                                onModeSelected: function(mode) { setAgentStylePermissionMode(mode) }
                                            }
                                        }

                                        Label {
                                            objectName: "agentPermissionModeSummaryLabel"
                                            Layout.fillWidth: true
                                            text: agentStylePermissionSummary()
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                            wrapMode: Text.WordWrap
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            visible: agentDetailsExpanded
                                            text: "Exact tool permissions"
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                        }

                                        Flow {
                                            objectName: "agentExactToolPermissions"
                                            Layout.fillWidth: true
                                            visible: agentDetailsExpanded
                                            spacing: 6

                                            AgentToolPermissionToggle {
                                                label: "RAW"
                                                checked: agentStyleAllowRaw
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow RAW-aware decode and camera-profile decisions."
                                                onToggled: function(checked) { agentStyleAllowRaw = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Tone/Color"
                                                checked: agentStyleAllowGlobalColor
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow global exposure, contrast, white balance, and color edits."
                                                onToggled: function(checked) { agentStyleAllowGlobalColor = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Curves"
                                                checked: agentStyleAllowCurves
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow tone-curve shaping."
                                                onToggled: function(checked) { agentStyleAllowCurves = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "HSL"
                                                checked: agentStyleAllowHsl
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow hue, saturation, and luminance channel edits."
                                                onToggled: function(checked) { agentStyleAllowHsl = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Grading"
                                                checked: agentStyleAllowColorGrading
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow shadows, midtones, and highlights color grading."
                                                onToggled: function(checked) { agentStyleAllowColorGrading = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Detail"
                                                checked: agentStyleAllowDetail
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow sharpening, noise, clarity, and texture decisions."
                                                onToggled: function(checked) { agentStyleAllowDetail = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Crop"
                                                checked: agentStyleAllowCrop
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow conservative crop and straighten changes."
                                                onToggled: function(checked) { agentStyleAllowCrop = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Masks"
                                                checked: agentStyleAllowMasks
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow local masks and targeted adjustments."
                                                onToggled: function(checked) { agentStyleAllowMasks = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Retouch"
                                                checked: agentStyleAllowRetouch
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow minor cleanup and retouch actions."
                                                onToggled: function(checked) { agentStyleAllowRetouch = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Layers/LUTs"
                                                checked: agentStyleAllowLayersLuts
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow layer and LUT-based look construction."
                                                onToggled: function(checked) { agentStyleAllowLayersLuts = checked }
                                            }

                                            AgentToolPermissionToggle {
                                                label: "Export"
                                                checked: agentStyleAllowExport
                                                toggleEnabled: !root.agentEditController.running
                                                helperText: "Allow export setting decisions."
                                                onToggled: function(checked) { agentStyleAllowExport = checked }
                                            }
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 2
                                            columnSpacing: 6
                                            rowSpacing: 6

                                            ToneTextField {
                                                Layout.fillWidth: true
                                                text: agentStyleMaxExposureShift
                                                placeholderText: "Max exposure shift"
                                                enabled: !root.agentEditController.running
                                                onTextEdited: agentStyleMaxExposureShift = text
                                            }
                                            ToneTextField {
                                                Layout.fillWidth: true
                                                text: agentStyleMaxSaturationShift
                                                placeholderText: "Max saturation shift"
                                                enabled: !root.agentEditController.running
                                                onTextEdited: agentStyleMaxSaturationShift = text
                                            }
                                            ToneTextField {
                                                Layout.fillWidth: true
                                                text: agentStyleMaxTemperatureShift
                                                placeholderText: "Max temperature shift"
                                                enabled: !root.agentEditController.running
                                                onTextEdited: agentStyleMaxTemperatureShift = text
                                            }
                                            ToneTextField {
                                                Layout.fillWidth: true
                                                text: agentStyleHighlightClipLimit
                                                placeholderText: "Highlight clip %"
                                                enabled: !root.agentEditController.running
                                                onTextEdited: agentStyleHighlightClipLimit = text
                                            }
                                            ToneTextField {
                                                Layout.fillWidth: true
                                                text: agentStyleShadowClipLimit
                                                placeholderText: "Shadow clip %"
                                                enabled: !root.agentEditController.running
                                                onTextEdited: agentStyleShadowClipLimit = text
                                            }
                                            ActionButton {
                                                Layout.fillWidth: true
                                                text: "Save Style"
                                                iconName: "save"
                                                tone: "primary"
                                                enabled: !root.agentEditController.running &&
                                                         agentStyleDraftName.trim().length > 0 &&
                                                         agentStyleDraftGoal.trim().length > 0
                                                onClicked: createAgentStyleFromDraft()
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            visible: agentStyleSaveMessage.length > 0
                                            text: agentStyleSaveMessage
                                            color: agentStyleSaveMessage.indexOf("Saved ") === 0 ? theme.textMuted : theme.error
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                            wrapMode: Text.WordWrap
                                        }
                                    }

                                    ToneComboBox {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        model: root.agentEditController.availableModelNames
                                        enabled: root.agentEditController.availableModelNames.length > 0 && !root.agentEditController.running
                                        currentIndex: agentSelectedModelIndex()
                                        onActivated: function(index) {
                                            const modelInfo = root.agentEditController.availableModels[index]
                                            if (modelInfo && modelInfo.id !== undefined)
                                                root.agentEditController.selectedModelId = modelInfo.id
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 5
                                        visible: agentDetailsExpanded && root.agentEditController.availableModels.length > 0

                                        Repeater {
                                            model: root.agentEditController.availableModels

                                            delegate: AgentModelRow {
                                                required property int index
                                                required property var modelData

                                                Layout.fillWidth: true
                                                selected: root.agentEditController.selectedModelId === String(modelData.id || "")
                                                commandEnabled: !root.agentEditController.running
                                                detailText: agentModelDetailText(modelData)
                                                statusText: agentModelStatusLabel(modelData)
                                                statusColor: agentModelStatusColor(modelData)
                                                onSelectedRequested: function(modelId) {
                                                    root.agentEditController.selectedModelId = modelId
                                                }
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        spacing: 6

                                        Label {
                                            Layout.fillWidth: true
                                            text: agentScopeText()
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideNone
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: agentModelStatusText()
                                            color: {
                                                const modelInfo = activeAgentModel()
                                                if (!modelInfo)
                                                    return theme.error
                                                const status = String(modelInfo.status || "").toLowerCase()
                                                return status === "failed" || status === "unavailable"
                                                       ? theme.error
                                                       : theme.textMuted
                                            }
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            horizontalAlignment: Text.AlignRight
                                            fontSizeMode: Text.HorizontalFit
                                            minimumPixelSize: 7
                                            elide: Text.ElideNone
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        spacing: 6

                                        Label {
                                            objectName: "editAgentMcpStatusLabel"
                                            Layout.fillWidth: true
                                            text: agentMcpStatusText()
                                            color: agentMcpStatusColor()
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideNone
                                        }

                                        Label {
                                            objectName: "editAgentMcpEndpointLabel"
                                            Layout.fillWidth: true
                                            text: String(root.agentEditController.mcpEndpoint || "")
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            horizontalAlignment: Text.AlignRight
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideNone
                                        }
                                    }

                                    Label {
                                        objectName: "editAgentMcpTelemetryLabel"
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded && agentMcpTelemetryText().length > 0
                                        text: agentMcpTelemetryText()
                                        color: theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 2
                                        elide: Text.ElideNone
                                    }

                                    Label {
                                        objectName: "editAgentMemoryVaultLabel"
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        text: agentMemoryVaultText()
                                        color: theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 3
                                        elide: Text.ElideNone
                                    }

                                    GridLayout {
                                        objectName: "editAgentMemoryVaultActions"
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        columns: width >= 520 ? 4 : (width >= 300 ? 2 : 1)
                                        columnSpacing: 6
                                        rowSpacing: 6

                                        ActionButton {
                                            objectName: "editAgentOpenMemoryVaultButton"
                                            Layout.fillWidth: true
                                            text: "Open Vault"
                                            iconName: "folder"
                                            compact: true
                                            enabled: root.hasAgentEditController
                                            tooltipText: "Open the local Markdown agent memory vault."
                                            onClicked: openAgentMemoryVault()
                                        }

                                        ActionButton {
                                            objectName: "editAgentOpenMemoryVaultInObsidianButton"
                                            Layout.fillWidth: true
                                            text: "Open Obsidian"
                                            iconName: "external-link"
                                            compact: true
                                            enabled: root.hasAgentEditController
                                            tooltipText: "Open the local Markdown vault in Obsidian, falling back to the folder if needed."
                                            onClicked: openAgentMemoryVaultInObsidian()
                                        }

                                        ActionButton {
                                            objectName: "editAgentExportMemoryVaultButton"
                                            Layout.fillWidth: true
                                            text: "Export Copy"
                                            iconName: "export"
                                            compact: true
                                            enabled: root.hasAgentEditController
                                            tooltipText: "Create a redacted copy of the local agent memory vault."
                                            onClicked: exportAgentMemoryVault()
                                        }

                                        ActionButton {
                                            objectName: "editAgentResetMemoryVaultButton"
                                            Layout.fillWidth: true
                                            text: agentMemoryVaultResetArmed ? "Confirm Reset" : "Reset Vault"
                                            iconName: "reset"
                                            compact: true
                                            tone: agentMemoryVaultResetArmed ? "danger" : "secondary"
                                            enabled: root.hasAgentEditController
                                            tooltipText: "Clear the local agent memory vault after a second confirmation click."
                                            onClicked: resetAgentMemoryVaultFromDetails()
                                        }
                                    }

                                    Label {
                                        objectName: "editAgentMemoryVaultActionLabel"
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded && agentMemoryVaultMessage.length > 0
                                        text: agentMemoryVaultMessage
                                        color: agentMemoryVaultResetArmed ? theme.warning : theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 3
                                        elide: Text.ElideNone
                                    }

                                    Rectangle {
                                        objectName: "agentWorkflowStatePanel"
                                        Layout.fillWidth: true
                                        implicitHeight: agentWorkflowStateColumn.implicitHeight + 16
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.glassBase, 0.44)
                                        border.width: 1
                                        border.color: theme.alpha(theme.glassEdge, 0.28)

                                        ColumnLayout {
                                            id: agentWorkflowStateColumn
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 5

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Label {
                                                    objectName: "agentWorkflowStatusLabel"
                                                    Layout.fillWidth: true
                                                    text: String(agentWorkflowState().statusLine || "AI Edit")
                                                    color: theme.textStrong
                                                    font.family: theme.fontUi
                                                    font.pixelSize: 12
                                                    font.weight: Font.DemiBold
                                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                    maximumLineCount: 2
                                                    elide: Text.ElideNone
                                                }

                                                Label {
                                                    objectName: "agentWorkflowScopeLabel"
                                                    Layout.preferredWidth: 118
                                                    text: String(agentWorkflowState().scopeLabel || "")
                                                    color: theme.focusRing
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 9
                                                    horizontalAlignment: Text.AlignRight
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }
                                            }

                                            Label {
                                                objectName: "agentWorkflowDetailLabel"
                                                Layout.fillWidth: true
                                                text: agentWorkflowDetailText()
                                                color: theme.textMuted
                                                font.family: theme.fontUi
                                                font.pixelSize: 10
                                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                maximumLineCount: 2
                                                elide: Text.ElideNone
                                            }
                                        }
                                    }

                                    Rectangle {
                                        objectName: "agentTasteCard"
                                        Layout.fillWidth: true
                                        visible: agentTasteCardVisible()
                                        implicitHeight: agentTasteCardColumn.implicitHeight + 16
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.ink950, 0.28)
                                        border.width: 1
                                        border.color: theme.alpha(theme.focusRing, 0.30)

                                        ColumnLayout {
                                            id: agentTasteCardColumn
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 4

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 7

                                                AppIcon {
                                                    Layout.preferredWidth: 14
                                                    Layout.preferredHeight: 14
                                                    name: "star"
                                                    color: theme.focusRing
                                                }

                                                Label {
                                                    objectName: "agentTasteCardTitle"
                                                    Layout.fillWidth: true
                                                    text: agentTasteCardTitle()
                                                    color: theme.textStrong
                                                    font.family: theme.fontUi
                                                    font.pixelSize: 11
                                                    font.weight: Font.DemiBold
                                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                    maximumLineCount: 2
                                                    elide: Text.ElideNone
                                                    clip: true
                                                }
                                            }

                                            Label {
                                                objectName: "agentTasteCardSummary"
                                                Layout.fillWidth: true
                                                text: agentTasteCardText()
                                                color: theme.textMuted
                                                font.family: theme.fontUi
                                                font.pixelSize: 10
                                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                maximumLineCount: 3
                                                elide: Text.ElideNone
                                            }
                                        }
                                    }

                                    ActionButton {
                                        objectName: "agentDetailsToggleButton"
                                        Layout.fillWidth: true
                                        text: agentDetailsExpanded ? "Hide Details" : "Show Details"
                                        iconName: agentDetailsExpanded ? "chevron-right" : "menu"
                                        compact: true
                                        enabled: !root.agentEditController.running
                                        onClicked: agentDetailsExpanded = !agentDetailsExpanded
                                    }

                                    TextArea {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 86
                                        text: root.agentEditController.userInstructions
                                        placeholderText: "Style notes"
                                        wrapMode: TextEdit.WordWrap
                                        enabled: !root.agentEditController.running
                                        color: theme.textStrong
                                        selectedTextColor: theme.ink950
                                        selectionColor: theme.focusRing
                                        font.family: theme.fontUi
                                        font.pixelSize: 11
                                        onTextChanged: {
                                            if (activeFocus)
                                                root.agentEditController.userInstructions = text
                                        }
                                        background: Rectangle {
                                            radius: theme.radiusControl
                                            color: theme.alpha(theme.glassBase, 0.38)
                                            border.width: 1
                                            border.color: theme.alpha(theme.glassEdge, 0.24)
                                        }
                                    }

                                    Rectangle {
                                        objectName: "agentPendingInputPanel"
                                        Layout.fillWidth: true
                                        visible: agentHasPendingInputSurface()
                                        implicitHeight: visible ? agentPendingInputColumn.implicitHeight + 14 : 0
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.warning, 0.10)
                                        border.width: 1
                                        border.color: theme.alpha(theme.warning, 0.34)

                                        ColumnLayout {
                                            id: agentPendingInputColumn
                                            anchors.fill: parent
                                            anchors.margins: 7
                                            spacing: 7

                                            Label {
                                                Layout.fillWidth: true
                                                text: root.agentEditController.awaitingUserInput ? "Agent needs an answer" : "Agent stopped safely"
                                                color: theme.textStrong
                                                font.family: theme.fontUi
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                wrapMode: Text.WordWrap
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: root.agentEditController.awaitingUserInput
                                                      ? agentPendingQuestionText()
                                                      : String(root.agentEditController.safeStopReason || root.agentEditController.lastRunReport.safeStopReason || root.agentEditController.lastRunReport.planSummary || "")
                                                color: theme.textMain
                                                font.family: theme.fontUi
                                                font.pixelSize: 10
                                                wrapMode: Text.WordWrap
                                            }

                                            TextArea {
                                                id: agentPendingAnswerText
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 64
                                                visible: root.agentEditController.awaitingUserInput
                                                placeholderText: "Answer the agent"
                                                wrapMode: TextEdit.WordWrap
                                                enabled: !root.agentEditController.running
                                                color: theme.textStrong
                                                selectedTextColor: theme.ink950
                                                selectionColor: theme.focusRing
                                                font.family: theme.fontUi
                                                font.pixelSize: 11
                                                background: Rectangle {
                                                    radius: theme.radiusControl
                                                    color: theme.alpha(theme.glassBase, 0.38)
                                                    border.width: 1
                                                    border.color: theme.alpha(theme.glassEdge, 0.24)
                                                }
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6

                                                ActionButton {
                                                    Layout.fillWidth: true
                                                    text: "Continue"
                                                    iconName: "check"
                                                    compact: true
                                                    tone: "primary"
                                                    visible: root.agentEditController.awaitingUserInput
                                                    enabled: !root.agentEditController.running &&
                                                             agentPendingAnswerText.text.trim().length > 0
                                                    onClicked: {
                                                        root.agentEditController.answerPendingUserInput(agentPendingAnswerText.text)
                                                        agentPendingAnswerText.text = ""
                                                    }
                                                }

                                                ActionButton {
                                                    Layout.fillWidth: true
                                                    text: "Dismiss"
                                                    iconName: "close"
                                                    compact: true
                                                    enabled: !root.agentEditController.running
                                                    onClicked: {
                                                        root.agentEditController.dismissPendingAgentInput()
                                                        agentPendingAnswerText.text = ""
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        text: "Base"
                                        color: theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        spacing: 5

                                        Repeater {
                                            model: ["current", "neutral"]

                                            delegate: ActionButton {
                                                required property string modelData

                                                text: agentRecipeBaseModeLabel(modelData)
                                                iconName: agentRecipeBaseModeIcon(modelData)
                                                compact: true
                                                active: root.agentEditController.recipeBaseMode === modelData
                                                enabled: !root.agentEditController.running
                                                onClicked: root.agentEditController.recipeBaseMode = modelData
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        text: "Run mode"
                                        color: theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded
                                        spacing: 5

                                        Repeater {
                                            model: ["analyze", "draft", "validate-batch", "export-preflight"]

                                            delegate: ActionButton {
                                                required property string modelData

                                                text: agentRunModeLabel(modelData)
                                                iconName: agentRunModeIcon(modelData)
                                                compact: true
                                                active: agentRunMode === modelData
                                                enabled: !root.agentEditController.running
                                                onClicked: agentRunMode = modelData
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        ActionButton {
                                            Layout.fillWidth: true
                                            minimumReadableWidth: 190
                                            allowLabelWrap: true
                                            text: agentWorkflowPrimaryLabel()
                                            iconName: agentWorkflowPrimaryIcon()
                                            tone: "primary"
                                            enabled: agentWorkflowPrimaryEnabled()
                                            onClicked: runAgentWorkflowPrimaryAction()
                                        }

                                        ActionButton {
                                            minimumReadableWidth: 96
                                            text: "Cancel"
                                            iconName: "close"
                                            compact: true
                                            enabled: root.agentEditController.running
                                            onClicked: root.agentEditController.cancelRun("user_cancelled")
                                        }
                                    }

                                    Rectangle {
                                        objectName: "editAgentThinkingPanel"
                                        Layout.fillWidth: true
                                        visible: agentThinkingVisible()
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.accentWarm400, 0.08)
                                        border.width: 1
                                        border.color: theme.alpha(theme.accentWarm400, 0.24)
                                        implicitHeight: editAgentThinkingColumn.implicitHeight + 12

                                        ColumnLayout {
                                            id: editAgentThinkingColumn
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 5

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6

                                                AppIcon {
                                                    Layout.preferredWidth: 13
                                                    Layout.preferredHeight: 13
                                                    name: "info"
                                                    color: theme.accentWarm400
                                                }

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: "Thinking"
                                                    color: theme.textStrong
                                                    font.family: theme.fontUi
                                                    font.pixelSize: 11
                                                    font.weight: Font.DemiBold
                                                }
                                            }

                                            Repeater {
                                                model: agentThinkingLines()

                                                Label {
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    text: modelData
                                                    color: theme.textMain
                                                    font.family: theme.fontUi
                                                    font.pixelSize: 10
                                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                    maximumLineCount: 2
                                                    elide: Text.ElideNone
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        objectName: "agentRunStagePanel"
                                        Layout.fillWidth: true
                                        visible: agentHasStageProgress()
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.ink950, 0.24)
                                        border.width: 1
                                        border.color: theme.alpha(theme.lineSoft, 0.22)
                                        implicitHeight: agentRunStageColumn.implicitHeight + 12

                                        ColumnLayout {
                                            id: agentRunStageColumn
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 6

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: "Run stages"
                                                    color: theme.textStrong
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 10
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }

                                                Label {
                                                    text: root.agentEditController.running ? root.agentEditController.status : "Last run"
                                                    color: root.agentEditController.running ? theme.accentCool500 : theme.textMuted
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 9
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }
                                            }

                                            GridLayout {
                                                id: agentRunStageGrid
                                                Layout.fillWidth: true
                                                columns: 2
                                                columnSpacing: 6
                                                rowSpacing: 5

                                                Repeater {
                                                    model: agentRunStages()

                                                    delegate: AgentRunStageRow {
                                                        objectName: "agentRunStageRow"
                                                        required property var modelData
                                                        readonly property string stageStatus: agentStageStatus(modelData.key)

                                                        Layout.preferredWidth: Math.max(0, (agentRunStageGrid.width - agentRunStageGrid.columnSpacing) / 2)
                                                        label: String(modelData.label || "Stage")
                                                        statusText: agentStageStatusLabel(stageStatus)
                                                        statusColor: agentStageStatusColor(stageStatus)
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        ActionButton {
                                            Layout.fillWidth: true
                                            minimumReadableWidth: 88
                                            text: "Commit"
                                            iconName: "check"
                                            compact: true
                                            enabled: root.agentEditController.draftActive && !root.agentEditController.running
                                            onClicked: root.agentEditController.commitDraft(true)
                                        }

                                        ActionButton {
                                            Layout.fillWidth: true
                                            minimumReadableWidth: 126
                                            text: agentSampleReviewWaiting() ? "Make Changes" : "Revise"
                                            iconName: "adjust"
                                            compact: true
                                            enabled: root.agentEditController.draftActive &&
                                                     !root.agentEditController.running &&
                                                     (!agentSampleReviewWaiting() ||
                                                      String(root.agentEditController.userInstructions || "").trim().length > 0)
                                            onClicked: {
                                                if (agentSampleReviewWaiting())
                                                    root.agentEditController.reviseSampleReview(root.agentEditController.userInstructions)
                                                else
                                                    root.agentEditController.runDraftOnSelected(root.agentEditController.userInstructions)
                                            }
                                        }

                                        ConfirmActionButton {
                                            Layout.fillWidth: true
                                            minimumReadableWidth: 132
                                            text: "Start Export"
                                            confirmText: "Confirm Export"
                                            iconName: "export"
                                            compact: true
                                            enabled: editorPreview.hasAsset && catalogAssets.selectedAssetCount >= 1 && !root.agentEditController.running
                                            helperText: "Queue selected assets after agent review"
                                            onConfirmed: root.agentEditController.queueExportForSelected("", "", true)
                                        }

                                        ConfirmActionButton {
                                            Layout.fillWidth: true
                                            minimumReadableWidth: 96
                                            text: "Rollback"
                                            compact: true
                                            enabled: root.agentEditController.draftActive && !root.agentEditController.running
                                            helperText: "Restore the pre-agent recipe"
                                            onConfirmed: root.agentEditController.rollbackDraft()
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded &&
                                                 root.agentEditController.lastRunReport.analysisSummary !== undefined &&
                                                 root.agentEditController.lastRunReport.analysisSummary.length > 0
                                        text: root.agentEditController.lastRunReport.analysisSummary || ""
                                        color: theme.textMain
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded &&
                                                 root.agentEditController.lastRunReport.planSummary !== undefined &&
                                                 root.agentEditController.lastRunReport.planSummary.length > 0
                                        text: root.agentEditController.lastRunReport.planSummary || ""
                                        color: theme.textMain
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }

                                    ColumnLayout {
                                        objectName: "agentProofSurface"
                                        Layout.fillWidth: true
                                        spacing: 6
                                        visible: agentHasProofSurface()

                                        Label {
                                            Layout.fillWidth: true
                                            text: agentProofPath("before").length > 0 &&
                                                  agentProofPath("after").length > 0
                                                  ? "Before / After proof"
                                                  : "Rendered proof"
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            visible: agentProofPath("before").length > 0 &&
                                                     agentProofPath("after").length > 0

                                            Repeater {
                                                model: [
                                                    { "role": "before", "label": "Before" },
                                                    { "role": "after", "label": "After" }
                                                ]

                                                delegate: AgentProofCard {
                                                    required property var modelData

                                                    Layout.preferredHeight: 104
                                                    label: String(modelData.label || "Proof")
                                                    imageObjectName: "agent" + String(modelData.label || "Proof") + "ProofImage"
                                                    source: agentProofSource(modelData.role)
                                                    showLabel: true
                                                }
                                            }
                                        }

                                        AgentProofCard {
                                            Layout.preferredHeight: 118
                                            visible: agentProofPath("before").length === 0 ||
                                                     agentProofPath("after").length === 0
                                            label: "Current"
                                            imageObjectName: "agentCurrentProofImage"
                                            source: agentProofSource("current")
                                            showLabel: false
                                        }
                                    }

                                    Rectangle {
                                        objectName: "editAgentRunAuditPanel"
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded && agentHasRunAudit()
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.ink950, 0.28)
                                        border.width: 1
                                        border.color: theme.alpha(theme.lineSoft, 0.22)
                                        implicitHeight: editAgentRunAuditColumn.implicitHeight + 12

                                        ColumnLayout {
                                            id: editAgentRunAuditColumn
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 6

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: "Run audit"
                                                    color: theme.textStrong
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 10
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }

                                                Label {
                                                    text: agentToolCalls().length + " tool calls"
                                                    color: theme.textMuted
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 9
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }
                                            }

                                            Label {
                                                objectName: "editAgentRunProofPathLabel"
                                                Layout.fillWidth: true
                                                visible: agentRunArtifactSummary().length > 0
                                                text: agentRunArtifactSummary()
                                                color: theme.textMuted
                                                font.family: theme.fontMono
                                                font.pixelSize: 9
                                                wrapMode: Text.WrapAnywhere
                                                maximumLineCount: 2
                                                elide: Text.ElideNone
                                            }

                                            Label {
                                                objectName: "editAgentRunEvidenceLabel"
                                                Layout.fillWidth: true
                                                visible: agentRunEvidenceSummary().length > 0
                                                text: agentRunEvidenceSummary()
                                                color: theme.textMuted
                                                font.family: theme.fontUi
                                                font.pixelSize: 9
                                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                maximumLineCount: 2
                                                elide: Text.ElideNone
                                            }

                                            Repeater {
                                                objectName: "editAgentLoopAttemptRepeater"
                                                model: agentLoopAttemptRows()

                                                delegate: AgentRunStageRow {
                                                    objectName: "editAgentLoopAttemptRow"
                                                    required property var modelData

                                                    label: String(modelData.label || "Attempt")
                                                    statusText: String(modelData.score || 0) + "% · " +
                                                                String(modelData.status || "checked") +
                                                                (Number(modelData.warningCount || 0) > 0
                                                                 ? " · " + Number(modelData.warningCount || 0) + " warn"
                                                                 : "")
                                                    statusColor: agentLoopStatusColor(modelData)
                                                }
                                            }

                                            Repeater {
                                                objectName: "editAgentRunToolLogRepeater"
                                                model: agentToolCalls().slice(0, 6)

                                                delegate: AgentToolCallRow {
                                                    objectName: "editAgentRunToolCallRow"
                                                    required property var modelData

                                                    title: agentToolCallTitle(modelData)
                                                    detail: agentToolCallDetail(modelData)
                                                    statusColor: agentToolCallStatusColor(modelData)
                                                }
                                            }

                                            Label {
                                                objectName: "editAgentRunWarningsLabel"
                                                Layout.fillWidth: true
                                                visible: agentRunWarnings().length > 0
                                                text: agentRunWarnings().join("\n")
                                                color: theme.warning
                                                font.family: theme.fontUi
                                                font.pixelSize: 9
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                    }

                                    Rectangle {
                                        objectName: "agentRecipeDiffPanel"
                                        Layout.fillWidth: true
                                        visible: agentDetailsExpanded && agentHasRecipeDiff()
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.ink950, 0.26)
                                        border.width: 1
                                        border.color: theme.alpha(theme.lineSoft, 0.22)
                                        implicitHeight: agentRecipeDiffColumn.implicitHeight + 12

                                        ColumnLayout {
                                            id: agentRecipeDiffColumn
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 6

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: "Recipe diff"
                                                    color: theme.textStrong
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 10
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }

                                                Label {
                                                    text: agentRecipeDiffGroups().length + " groups"
                                                    color: theme.textMuted
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 9
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }
                                            }

                                            Repeater {
                                                model: agentRecipeDiffGroups()

                                                delegate: AgentRecipeDiffRow {
                                                    objectName: "agentRecipeDiffRow"
                                                    required property var modelData

                                                    label: String(modelData.label || "Recipe group")
                                                    summary: String(modelData.summary || "")
                                                    countText: String(modelData.count || "0")
                                                    countColor: theme.accentCool500
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        objectName: "agentBatchStatusPanel"
                                        Layout.fillWidth: true
                                        visible: agentHasBatchStatus()
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.ink950, 0.30)
                                        border.width: 1
                                        border.color: theme.alpha(theme.lineSoft, 0.24)
                                        implicitHeight: agentBatchStatusColumn.implicitHeight + 12

                                        ColumnLayout {
                                            id: agentBatchStatusColumn
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 6

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: "Batch status"
                                                    color: theme.textStrong
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 10
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }

                                                Label {
                                                    text: agentBatchStatusSummaryText()
                                                    color: theme.textMuted
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 9
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }
                                            }

                                            RowLayout {
                                                objectName: "agentFixAllReviewIssuesRow"
                                                Layout.fillWidth: true
                                                visible: agentFixAllReviewIssuesAvailable()
                                                spacing: 6

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: "Multiple outliers can be repaired automatically."
                                                    color: theme.textMuted
                                                    font.family: theme.fontUi
                                                    font.pixelSize: 9
                                                    wrapMode: Text.WordWrap
                                                }

                                                ActionButton {
                                                    objectName: "agentFixAllReviewIssuesButton"
                                                    text: "Fix All"
                                                    iconName: "adjust"
                                                    compact: true
                                                    enabled: root.agentEditController.draftActive && !root.agentEditController.running
                                                    onClicked: root.agentEditController.fixAllReviewIssues()
                                                }
                                            }

                                            Repeater {
                                                model: agentAssetReports()

                                                delegate: AgentBatchStatusRow {
                                                    objectName: "agentBatchStatusRow"
                                                    required property int index
                                                    required property var modelData
                                                    readonly property string batchStatus: agentBatchReportStatus(modelData)

                                                    assetLabel: agentBatchReportAssetLabel(modelData, index)
                                                    detail: agentBatchReportDetail(modelData)
                                                    suggestedAction: agentBatchReportSuggestedAction(modelData)
                                                    detailsText: agentReviewIssueDetailsText(modelData)
                                                    detailsExpanded: agentReviewIssueExpanded(modelData)
                                                    statusText: batchStatus
                                                    statusColor: agentBatchStatusColor(batchStatus)
                                                    thumbnailSource: agentBatchReportThumbnailSource(modelData)
                                                    actionsVisible: modelData.actions !== undefined &&
                                                                    modelData.actions !== null &&
                                                                    modelData.actions.length > 0
                                                    actionEnabled: root.agentEditController.draftActive && !root.agentEditController.running
                                                    onFixRequested: root.agentEditController.fixReviewIssue(String(modelData.assetId || ""))
                                                    onAcceptRequested: root.agentEditController.acceptReviewIssue(String(modelData.assetId || ""))
                                                    onRollbackRequested: root.agentEditController.rollbackReviewIssue(String(modelData.assetId || ""))
                                                    onDetailsToggled: toggleAgentReviewIssueDetails(modelData)
                                                }
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                visible: agentAssetReports().length === 0 && root.agentEditController.running
                                                text: "Waiting for per-asset run reports"
                                                color: theme.textMuted
                                                font.family: theme.fontUi
                                                font.pixelSize: 9
                                                wrapMode: Text.WordWrap
                                            }
                                        }
	                                    }

                                    Rectangle {
                                        objectName: "agentRunHistoryPanel"
                                        Layout.fillWidth: true
                                        visible: agentRunHistory().length > 0
                                        radius: theme.radiusControl
                                        color: theme.alpha(theme.ink950, 0.26)
                                        border.width: 1
                                        border.color: theme.alpha(theme.lineSoft, 0.22)
                                        implicitHeight: agentRunHistoryColumn.implicitHeight + 12

                                        ColumnLayout {
                                            id: agentRunHistoryColumn
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 6

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: "Run history"
                                                    color: theme.textStrong
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 10
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }

                                                Label {
                                                    text: agentRunHistory().length + " recent"
                                                    color: theme.textMuted
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 9
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 7
                                                    elide: Text.ElideNone
                                                }
                                            }

                                            Repeater {
                                                model: agentRunHistory().slice(0, 5)

                                                delegate: AgentRunHistoryRow {
                                                    objectName: "agentRunHistoryRow"
                                                    required property var modelData

                                                    title: agentRunHistoryTitle(modelData)
                                                    detail: agentRunHistoryDetail(modelData)
                                                    statusText: modelData.ok === true ? "ok" : "failed"
                                                    statusColor: agentRunHistoryStatusColor(modelData)
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: root.agentEditController.lastRunReport.warnings !== undefined &&
                                                 root.agentEditController.lastRunReport.warnings.length > 0
                                        text: root.agentEditController.lastRunReport.warnings !== undefined
                                              ? root.agentEditController.lastRunReport.warnings.join("\n")
                                              : ""
                                        color: theme.warning
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: root.agentEditController.errorMessage.length > 0
                                        text: root.agentEditController.errorMessage
                                        color: theme.error
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                        }

                        Rectangle {
                            width: parent.width
                            visible: inspectorMode === "masks" || (inspectorMode === "adjust" && inspectorTab === "adjust")
                            radius: 0
                            color: inspectorMode === "masks"
                                   ? theme.alpha(theme.ink950, 0.26)
                                   : theme.alpha(theme.ink950, 0.34)
                            border.width: 1
                            border.color: inspectorMode === "masks"
                                          ? theme.alpha(theme.lineSoft, 0.22)
                                          : theme.alpha(theme.lineSoft, 0.34)
                            implicitHeight: histogramColumn.implicitHeight + (inspectorMode === "masks" ? 10 : 12)

                            ColumnLayout {
                                id: histogramColumn
                                anchors.fill: parent
                                anchors.margins: 7
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        text: "Histogram"
                                        color: theme.textMain
                                        font.family: theme.fontMono
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.2
                                    }

                                    Item { Layout.fillWidth: true }

                                    ActionButton {
                                        text: "RGB"
                                        compact: true
                                        active: histogramShowRgb
                                        onClicked: histogramShowRgb = !histogramShowRgb
                                    }

                                    ActionButton {
                                        text: "Luma"
                                        compact: true
                                        active: histogramShowLuminance
                                        onClicked: histogramShowLuminance = !histogramShowLuminance
                                    }

                                    ActionButton {
                                        text: histogramLogScale ? "Log" : "Lin"
                                        compact: true
                                        active: histogramLogScale
                                        onClicked: histogramLogScale = !histogramLogScale
                                    }
                                }

                                HistogramDisplay {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: inspectorMode === "masks" ? 78 : 86
                                    redData: editorPreview.histogramRed
                                    greenData: editorPreview.histogramGreen
                                    blueData: editorPreview.histogramBlue
                                    luminanceData: editorPreview.histogramLuminance
                                    showRgb: histogramShowRgb
                                    showLuminance: histogramShowLuminance
                                    logScale: histogramLogScale
                                    shadowClipRatio: editorPreview.histogramSampleCount > 0
                                                     ? editorPreview.histogramShadowClipCount / editorPreview.histogramSampleCount
                                                     : 0
                                    highlightClipRatio: editorPreview.histogramSampleCount > 0
                                                        ? editorPreview.histogramHighlightClipCount / editorPreview.histogramSampleCount
                                                        : 0
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label {
                                        text: "Blk " + editorPreview.histogramShadowClipCount
                                        color: editorPreview.histogramShadowClipCount > 0 ? theme.accentWarm400 : theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }

                                    Label {
                                        text: "Wht " + editorPreview.histogramHighlightClipCount
                                        color: editorPreview.histogramHighlightClipCount > 0 ? "#f2bc79" : theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }

                                    Item { Layout.fillWidth: true }

                                    Label {
                                        text: editorPreview.histogramSampleCount > 0
                                              ? Math.round((editorPreview.histogramShadowClipCount / editorPreview.histogramSampleCount) * 100) + "% / " +
                                                Math.round((editorPreview.histogramHighlightClipCount / editorPreview.histogramSampleCount) * 100) + "%"
                                              : "0% / 0%"
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            visible: inspectorMode === "adjust" && inspectorTab === "scopes"
                            radius: 0
                            color: theme.alpha(theme.ink950, 0.18)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.24)
                            implicitHeight: scopeColumn.implicitHeight + 12

                            ColumnLayout {
                                id: scopeColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        text: "SCOPES"
                                        color: theme.textStrong
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 1.0
                                    }

                                    Item { Layout.fillWidth: true }

                                    Label {
                                        text: "RGB  /  LUMA"
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 8
                                        font.letterSpacing: 1.2
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 10
                                    rowSpacing: 10

                                    Repeater {
                                        model: ["Waveform", "Parade", "Vectorscope", "Histogram"]

                                        delegate: Rectangle {
                                            id: scopeTile

                                            required property string modelData

                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 104
                                            radius: 0
                                            color: theme.alpha(theme.ink950, 0.56)
                                            border.width: 1
                                            border.color: theme.alpha(theme.lineSoft, 0.22)

                                            Label {
                                                anchors.left: parent.left
                                                anchors.top: parent.top
                                                anchors.leftMargin: 8
                                                anchors.topMargin: 7
                                                text: scopeTile.modelData.toUpperCase()
                                                color: theme.textMuted
                                                font.family: theme.fontMono
                                                font.pixelSize: 8
                                                font.letterSpacing: 1.3
                                                z: 1
                                            }

                                            Label {
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.rightMargin: 8
                                                anchors.topMargin: 7
                                                visible: scopeTile.modelData === "Histogram"
                                                text: "LUMA"
                                                color: theme.textFaint
                                                font.family: theme.fontMono
                                                font.pixelSize: 8
                                                font.letterSpacing: 0.9
                                                z: 1
                                            }

                                            HistogramDisplay {
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                anchors.topMargin: 22
                                                visible: scopeTile.modelData === "Histogram"
                                                redData: editorPreview.histogramRed
                                                greenData: editorPreview.histogramGreen
                                                blueData: editorPreview.histogramBlue
                                                luminanceData: editorPreview.histogramLuminance
                                                showRgb: false
                                                showLuminance: true
                                                logScale: histogramLogScale
                                            framed: false
                                            shadowClipRatio: editorPreview.histogramSampleCount > 0
                                                             ? editorPreview.histogramShadowClipCount / editorPreview.histogramSampleCount
                                                             : 0
                                            highlightClipRatio: editorPreview.histogramSampleCount > 0
                                                                ? editorPreview.histogramHighlightClipCount / editorPreview.histogramSampleCount
                                                                : 0
                                        }

                                        Canvas {
                                            id: scopeCanvas
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            anchors.topMargin: 22
                                            visible: scopeTile.modelData !== "Histogram"

                                            onWidthChanged: requestPaint()
                                            onHeightChanged: requestPaint()

                                            onPaint: {
                                                const ctx = getContext("2d")
                                                ctx.reset()
                                                const w = width
                                                const h = height
                                                const red = editorPreview.scopeWaveformRed || []
                                                const green = editorPreview.scopeWaveformGreen || []
                                                const blue = editorPreview.scopeWaveformBlue || []
                                                const luma = editorPreview.scopeWaveformLuminance || []
                                                const densityRed = editorPreview.scopeDensityRed || []
                                                const densityGreen = editorPreview.scopeDensityGreen || []
                                                const densityBlue = editorPreview.scopeDensityBlue || []
                                                const densityLuma = editorPreview.scopeDensityLuminance || []
                                                const vectors = editorPreview.scopeVectorscopePoints || []
                                                const densityLevels = 64

                                                function drawGrid(x, y, plotWidth, plotHeight, withLabels) {
                                                    ctx.save()
                                                    ctx.lineWidth = 1
                                                    ctx.strokeStyle = theme.alpha(theme.polar, 0.075)
                                                    for (let i = 1; i < 4; ++i) {
                                                        const gx = Math.round(x + (plotWidth / 4) * i) + 0.5
                                                        ctx.beginPath()
                                                        ctx.moveTo(gx, y)
                                                        ctx.lineTo(gx, y + plotHeight)
                                                        ctx.stroke()
                                                    }
                                                    for (let i = 0; i <= 4; ++i) {
                                                        const gy = Math.round(y + (plotHeight / 4) * i) + 0.5
                                                        ctx.beginPath()
                                                        ctx.moveTo(x, gy)
                                                        ctx.lineTo(x + plotWidth, gy)
                                                        ctx.stroke()
                                                    }
                                                    ctx.strokeStyle = theme.alpha(theme.lineSoft, 0.18)
                                                    ctx.strokeRect(Math.round(x) + 0.5, Math.round(y) + 0.5,
                                                                   Math.max(0, plotWidth - 1), Math.max(0, plotHeight - 1))
                                                    if (withLabels) {
                                                        ctx.fillStyle = theme.alpha(theme.textFaint, 0.44)
                                                        ctx.font = "7px \"" + theme.fontMono + "\""
                                                        ctx.textAlign = "left"
                                                        ctx.textBaseline = "middle"
                                                        const labels = [["100", 0], ["75", 0.25], ["50", 0.5], ["25", 0.75], ["0", 1]]
                                                        for (let j = 0; j < labels.length; ++j) {
                                                            const ly = Math.max(y + 4, Math.min(y + plotHeight - 4, y + labels[j][1] * plotHeight))
                                                            ctx.fillText(labels[j][0], 0, ly)
                                                        }
                                                    }
                                                    ctx.restore()
                                                }

                                                function channelValue(data, index, key) {
                                                    if (!data || data.length === 0)
                                                        return 0
                                                    const raw = data[Math.max(0, Math.min(data.length - 1, index))] || 0
                                                    const value = typeof raw === "object" ? Number(raw[key] || 0) : Number(raw || 0)
                                                    return Math.max(0, Math.min(1, value))
                                                }

                                                function drawAverage(data, color, x, y, plotWidth, plotHeight, alpha, lineWidth) {
                                                    const count = data ? data.length : 0
                                                    if (count < 2)
                                                        return
                                                    const step = plotWidth / Math.max(1, count - 1)
                                                    ctx.save()
                                                    ctx.strokeStyle = theme.alpha(color, alpha)
                                                    ctx.lineWidth = lineWidth
                                                    ctx.beginPath()
                                                    for (let i = 0; i < count; ++i) {
                                                        const px = x + step * i
                                                        const py = y + plotHeight - channelValue(data, i, "avg") * plotHeight
                                                            if (i === 0)
                                                                ctx.moveTo(px, py)
                                                            else
                                                                ctx.lineTo(px, py)
                                                        }
                                                        ctx.stroke()
                                                        ctx.restore()
                                                    }

                                                    function drawDensity(data, color, x, y, plotWidth, plotHeight, alphaScale, pointScale) {
                                                        if (!data || data.length < densityLevels)
                                                            return
                                                        const columns = Math.max(1, Math.floor(data.length / densityLevels))
                                                        const xStep = plotWidth / columns
                                                        const yStep = plotHeight / densityLevels
                                                        const pixelW = Math.max(1, xStep * pointScale)
                                                        const pixelH = Math.max(1, yStep * 1.05)
                                                        ctx.save()
                                                        ctx.globalCompositeOperation = "lighter"
                                                        for (let column = 0; column < columns; ++column) {
                                                            const px = x + column * xStep
                                                            for (let level = 0; level < densityLevels; ++level) {
                                                                const density = Number(data[column * densityLevels + level] || 0)
                                                                if (density <= 0.008)
                                                                    continue
                                                                const alpha = Math.min(0.72, Math.pow(density, 1.35) * alphaScale)
                                                                if (alpha <= 0.006)
                                                                    continue
                                                                const py = y + plotHeight - (level + 1) * yStep
                                                                ctx.fillStyle = theme.alpha(color, alpha)
                                                                ctx.fillRect(px, py, pixelW, pixelH)
                                                            }
                                                        }
                                                        ctx.restore()
                                                    }

                                                    if (scopeTile.modelData === "Parade") {
                                                        const labelWidth = 18
                                                        const gap = 7
                                                        const plotX = labelWidth
                                                        const segment = (w - labelWidth - gap * 2) / 3
                                                        drawGrid(plotX, 0, w - plotX, h, true)
                                                        ctx.save()
                                                        ctx.strokeStyle = theme.alpha(theme.lineSoft, 0.18)
                                                        ctx.beginPath()
                                                        ctx.moveTo(plotX + segment + gap / 2, 0)
                                                        ctx.lineTo(plotX + segment + gap / 2, h)
                                                        ctx.moveTo(plotX + segment * 2 + gap * 1.5, 0)
                                                        ctx.lineTo(plotX + segment * 2 + gap * 1.5, h)
                                                        ctx.stroke()
                                                        ctx.fillStyle = theme.alpha(theme.textMuted, 0.76)
                                                        ctx.font = "7px \"" + theme.fontMono + "\""
                                                        ctx.textAlign = "center"
                                                        ctx.textBaseline = "top"
                                                        ctx.fillText("R", plotX + segment * 0.5, 2)
                                                        ctx.fillText("G", plotX + segment * 1.5 + gap, 2)
                                                        ctx.fillText("B", plotX + segment * 2.5 + gap * 2, 2)
                                                        ctx.restore()
                                                        drawDensity(densityRed, theme.signalRed, plotX, 0, segment, h, 0.70, 1.08)
                                                        drawDensity(densityGreen, theme.vectorGreen, plotX + segment + gap, 0, segment, h, 0.62, 1.08)
                                                        drawDensity(densityBlue, theme.signalCyan, plotX + segment * 2 + gap * 2, 0, segment, h, 0.62, 1.08)
                                                        drawAverage(red, theme.signalRed, plotX, 0, segment, h, 0.62, 1.1)
                                                        drawAverage(green, theme.vectorGreen, plotX + segment + gap, 0, segment, h, 0.58, 1.1)
                                                        drawAverage(blue, theme.signalCyan, plotX + segment * 2 + gap * 2, 0, segment, h, 0.58, 1.1)
                                                        return
                                                    }

                                                    if (scopeTile.modelData === "Vectorscope") {
                                                        const cx = w / 2
                                                        const cy = h / 2
                                                        const radius = Math.min(w, h) * 0.39
                                                        ctx.save()
                                                        ctx.strokeStyle = theme.alpha(theme.polar, 0.18)
                                                        ctx.lineWidth = 1
                                                        ctx.beginPath()
                                                        ctx.arc(cx, cy, radius, 0, Math.PI * 2)
                                                        ctx.stroke()
                                                        ctx.strokeStyle = theme.alpha(theme.polar, 0.075)
                                                        ctx.beginPath()
                                                        ctx.arc(cx, cy, radius * 0.55, 0, Math.PI * 2)
                                                        ctx.stroke()
                                                        ctx.beginPath()
                                                        ctx.moveTo(cx - radius, cy)
                                                        ctx.lineTo(cx + radius, cy)
                                                        ctx.moveTo(cx, cy - radius)
                                                        ctx.lineTo(cx, cy + radius)
                                                        ctx.stroke()
                                                        const labels = [
                                                            ["R", theme.signalRed, -Math.PI * 0.88],
                                                            ["MG", theme.signalAmber, -Math.PI * 0.42],
                                                            ["B", theme.signalCyan, Math.PI * 0.18],
                                                            ["CY", theme.signalCyan, Math.PI * 0.63],
                                                            ["G", theme.vectorGreen, Math.PI * 0.5]
                                                        ]
                                                        ctx.font = "7px \"" + theme.fontMono + "\""
                                                        ctx.textAlign = "center"
                                                        ctx.textBaseline = "middle"
                                                        for (let i = 0; i < labels.length; ++i) {
                                                            const angle = labels[i][2]
                                                            ctx.strokeStyle = theme.alpha(labels[i][1], 0.30)
                                                            ctx.beginPath()
                                                            ctx.moveTo(cx, cy)
                                                            ctx.lineTo(cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius)
                                                            ctx.stroke()
                                                            ctx.fillStyle = theme.alpha(labels[i][1], 0.80)
                                                            ctx.fillText(labels[i][0],
                                                                         cx + Math.cos(angle) * radius * 1.28,
                                                                         cy + Math.sin(angle) * radius * 1.28)
                                                        }
                                                        const glow = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius * 0.34)
                                                        glow.addColorStop(0, theme.alpha(theme.polar, 0.22))
                                                        glow.addColorStop(1, theme.alpha(theme.polar, 0.0))
                                                        ctx.fillStyle = glow
                                                        ctx.beginPath()
                                                        ctx.arc(cx, cy, radius * 0.34, 0, Math.PI * 2)
                                                        ctx.fill()
                                                        ctx.restore()
                                                        ctx.save()
                                                        ctx.globalCompositeOperation = "lighter"
                                                        for (let i = 0; i < vectors.length; ++i) {
                                                            const point = vectors[i]
                                                            const px = cx + (Number(point.x || 0.5) - 0.5) * radius * 1.75
                                                            const py = cy + (Number(point.y || 0.5) - 0.5) * radius * 1.75
                                                            const pr = Number(point.red || 0.8)
                                                            const pg = Number(point.green || 0.8)
                                                            const pb = Number(point.blue || 0.8)
                                                            const l = Number(point.luma || 0.5)
                                                            ctx.fillStyle = Qt.rgba(pr, pg, pb, 0.040 + l * 0.075)
                                                            ctx.fillRect(px - 0.6, py - 0.6, 1.45, 1.45)
                                                        }
                                                        ctx.fillStyle = theme.alpha(theme.polar, 0.32)
                                                        ctx.fillRect(cx - 1.1, cy - 1.1, 2.2, 2.2)
                                                        ctx.restore()
                                                        return
                                                    }

                                                    const plotX = 18
                                                    drawGrid(plotX, 0, w - plotX, h, true)
                                                    drawDensity(densityLuma, theme.polar, plotX, 0, w - plotX, h, 0.46, 1.06)
                                                    drawDensity(densityBlue, theme.signalCyan, plotX, 0, w - plotX, h, 0.16, 1.02)
                                                    drawDensity(densityRed, theme.signalAmber, plotX, 0, w - plotX, h, 0.13, 1.02)
                                                    drawAverage(luma, theme.polar, plotX, 0, w - plotX, h, 0.58, 1.1)
                                                    drawAverage(blue, theme.signalCyan, plotX, 0, w - plotX, h, 0.24, 0.9)
                                                    drawAverage(red, theme.signalAmber, plotX, 0, w - plotX, h, 0.20, 0.9)
                                                }

                                                Component.onCompleted: requestPaint()

                                                Connections {
                                                    target: editorPreview
                                                    function onHistogramChanged() { scopeCanvas.requestPaint() }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            visible: inspectorMode === "adjust" && inspectorTab === "adjust"
                            radius: 0
                            color: "transparent"
                            border.width: 0
                            implicitHeight: referenceAdjustmentsColumn.implicitHeight + 16

                            ColumnLayout {
                                id: referenceAdjustmentsColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                anchors.topMargin: 8
                                spacing: 10

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: theme.alpha(theme.lineSoft, 0.30)
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label {
                                        Layout.fillWidth: true
                                        text: "ADJUSTMENTS"
                                        color: theme.textMain
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
                                        font.letterSpacing: 1.0
                                    }

                                    Label {
                                        text: "RESET"
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 8
                                        font.letterSpacing: 0.8
                                    }

                                    AppIcon {
                                        Layout.preferredWidth: 12
                                        Layout.preferredHeight: 12
                                        name: "menu"
                                        color: theme.textMuted
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Rectangle {
                                        Layout.preferredWidth: 8
                                        Layout.preferredHeight: 8
                                        radius: 4
                                        color: theme.alpha(theme.polar, 0.10)
                                        border.width: 1
                                        border.color: theme.alpha(theme.polar, 0.42)
                                    }

                                    Label {
                                        text: "BASE ADJUSTMENTS"
                                        color: theme.textMain
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                        font.letterSpacing: 0.5
                                    }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    visible: false
                                    title: "Layers"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: true

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Label {
                                            Layout.fillWidth: true
                                            text: editorPreview.photoLayerSoloActive ? "Layer isolation active" : editorPreview.photoLayerCount + " photo layers"
                                            color: editorPreview.photoLayerSoloActive ? theme.vectorGreen : theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: 8
                                            font.letterSpacing: 0.8
                                        }

                                        ActionButton {
                                            objectName: "photoLayerClearSoloButton"
                                            visible: editorPreview.photoLayerSoloActive
                                            text: "Clear Isolate"
                                            compact: true
                                            active: true
                                            accent: theme.vectorGreen
                                            tooltipText: "Show all visible layers again"
                                            onClicked: editorPreview.clearPhotoLayerSolo()
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 112
                                            Layout.preferredHeight: 24
                                            radius: 4
                                            color: addAdjustmentLayerArea.containsMouse ? theme.alpha(theme.vectorGreen, 0.14) : theme.alpha(theme.panelRaised, 0.72)
                                            border.width: 1
                                            border.color: theme.alpha(theme.vectorGreen, 0.36)

                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 6

                                                AppIcon {
                                                    name: "adjust"
                                                    color: theme.vectorGreen
                                                    Layout.preferredWidth: 12
                                                    Layout.preferredHeight: 12
                                                }

                                                Label {
                                                    text: "ADD CURRENT"
                                                    color: theme.textMain
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 8
                                                    font.letterSpacing: 0.8
                                                }
                                            }

                                            MouseArea {
                                                id: addAdjustmentLayerArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: editorPreview.addAdjustmentLayer("")
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 76
                                            Layout.preferredHeight: 24
                                            radius: 4
                                            color: addFillLayerArea.containsMouse ? theme.alpha(theme.polar, 0.14) : theme.alpha(theme.panelRaised, 0.72)
                                            border.width: 1
                                            border.color: theme.alpha(theme.polar, 0.36)

                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 6

                                                Rectangle {
                                                    Layout.preferredWidth: 10
                                                    Layout.preferredHeight: 10
                                                    radius: 2
                                                    color: "#808080"
                                                    border.width: 1
                                                    border.color: theme.alpha(theme.textMain, 0.26)
                                                }

                                                Label {
                                                    text: "FILL"
                                                    color: theme.textMain
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 8
                                                    font.letterSpacing: 0.8
                                                }
                                            }

                                            MouseArea {
                                                id: addFillLayerArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: editorPreview.addFillLayer("#808080", "")
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 76
                                            Layout.preferredHeight: 24
                                            radius: 4
                                            color: addTextLayerArea.containsMouse ? theme.alpha(theme.vectorGreen, 0.14) : theme.alpha(theme.panelRaised, 0.72)
                                            border.width: 1
                                            border.color: theme.alpha(theme.vectorGreen, 0.30)

                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 6

                                                Label {
                                                    text: "T"
                                                    color: theme.vectorGreen
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                }

                                                Label {
                                                    text: "TEXT"
                                                    color: theme.textMain
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 8
                                                    font.letterSpacing: 0.8
                                                }
                                            }

                                            MouseArea {
                                                id: addTextLayerArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: editorPreview.addTextLayer("Text", "")
                                            }
                                        }
                                    }

                                    Repeater {
                                        model: editorPreview.photoLayers

                                        delegate: Rectangle {
                                            required property var modelData

                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 58
                                            radius: 6
                                            color: modelData.enabled === false ? theme.alpha(theme.panelRaised, 0.26) : theme.alpha(theme.vectorGreen, 0.06)
                                            border.width: 1
                                            border.color: modelData.solo === true ? theme.alpha(theme.vectorGreen, 0.58)
                                                          : modelData.locked === true ? theme.alpha(theme.textFaint, 0.36)
                                                          : theme.alpha(theme.lineSoft, 0.30)

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                spacing: 8

                                                Rectangle {
                                                    objectName: "photoLayerThumbnail"
                                                    Layout.preferredWidth: 34
                                                    Layout.preferredHeight: 34
                                                    radius: 5
                                                    color: modelData.kind === "fill"
                                                           ? String(modelData.fillColor || "#808080")
                                                           : modelData.kind === "text"
                                                             ? theme.alpha(theme.vectorGreen, 0.15)
                                                             : modelData.kind === "source" || modelData.kind === "pixel"
                                                               ? theme.alpha(theme.polar, 0.16)
                                                               : modelData.kind === "group"
                                                                 ? theme.alpha(theme.textFaint, 0.14)
                                                                 : theme.alpha(theme.vectorGreen, 0.10)
                                                    border.width: 1
                                                    border.color: theme.alpha(theme.textMain, 0.24)

                                                    AppIcon {
                                                        anchors.centerIn: parent
                                                        name: modelData.kind === "group" ? "folder" : modelData.kind === "pixel" || modelData.kind === "source" ? "library" : modelData.kind === "fill" ? "scope" : modelData.kind === "text" ? "insert" : "adjust"
                                                        color: modelData.enabled === false ? theme.textMuted : theme.textMain
                                                        width: 15
                                                        height: 15
                                                    }
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 1

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: String(modelData.name || (modelData.kind === "group" ? "Group" : modelData.kind === "fill" ? "Fill Layer" : modelData.kind === "text" ? "Text Layer" : modelData.kind === "source" ? "Source Layer" : "Adjustment Layer"))
                                                        color: modelData.enabled === false ? theme.textMuted : theme.textMain
                                                        font.family: theme.fontUi
                                                        font.pixelSize: 12
                                                        font.weight: Font.Medium
                                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                        maximumLineCount: 2
                                                        elide: Text.ElideNone
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: String(modelData.layerAuditLabel || (String(modelData.blendMode || "normal") + " · " + Math.round(Number(modelData.opacity === undefined ? 1 : modelData.opacity) * 100) + "%"))
                                                        color: theme.textFaint
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 8
                                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                        maximumLineCount: 2
                                                        elide: Text.ElideNone
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        visible: String(modelData.dependencyLabel || "").length > 0
                                                        text: String(modelData.dependencyLabel || "")
                                                        color: theme.vectorGreen
                                                        font.family: theme.fontUi
                                                        font.pixelSize: 9
                                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                        maximumLineCount: 2
                                                        elide: Text.ElideNone
                                                    }
                                                }

                                                RowLayout {
                                                    spacing: 4

                                                    ActionButton {
                                                        objectName: "photoLayerVisibilityButton"
                                                        text: modelData.enabled === false ? "Off" : "On"
                                                        compact: true
                                                        active: modelData.enabled !== false
                                                        accent: theme.vectorGreen
                                                        onClicked: editorPreview.setPhotoLayerEnabled(modelData.flatIndex, !(modelData.enabled !== false))
                                                    }

                                                    ActionButton {
                                                        objectName: "photoLayerRowSoloButton"
                                                        text: modelData.solo === true ? "Isolated" : "Isolate"
                                                        compact: true
                                                        active: modelData.solo === true
                                                        accent: theme.vectorGreen
                                                        enabled: modelData.locked !== true || modelData.solo === true
                                                        tooltipText: modelData.solo === true ? "Clear layer isolation" : "Show only this layer"
                                                        onClicked: {
                                                            if (modelData.solo === true)
                                                                editorPreview.clearPhotoLayerSolo()
                                                            else
                                                                editorPreview.isolatePhotoLayer(modelData.flatIndex)
                                                        }
                                                    }

                                                    ActionButton {
                                                        objectName: "photoLayerLockButton"
                                                        text: modelData.locked === true ? "Locked" : "Lock"
                                                        compact: true
                                                        active: modelData.locked === true
                                                        accent: theme.textFaint
                                                        onClicked: editorPreview.setPhotoLayerLocked(modelData.flatIndex, !(modelData.locked === true))
                                                    }

                                                    ActionButton {
                                                        objectName: "photoLayerMoveUpButton"
                                                        text: "Up"
                                                        compact: true
                                                        enabled: modelData.flatIndex > 0 && modelData.locked !== true
                                                        onClicked: editorPreview.movePhotoLayer(modelData.flatIndex, -1)
                                                    }

                                                    ActionButton {
                                                        objectName: "photoLayerMoveDownButton"
                                                        text: "Down"
                                                        compact: true
                                                        enabled: modelData.flatIndex < editorPreview.photoLayerCount - 1 && modelData.locked !== true
                                                        onClicked: editorPreview.movePhotoLayer(modelData.flatIndex, 1)
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: editorPreview.photoLayerCount === 0
                                        text: "No photo layers"
                                        color: theme.textFaint
                                        font.family: theme.fontUi
                                        font.pixelSize: 11
                                    }
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "EXPOSURE"
                                    valueText: editorPreview.exposure.toFixed(2)
                                    from: -2
                                    to: 2
                                    value: editorPreview.exposure
                                    onValueDragged: function(value) { editorPreview.exposure = value }
                                    onResetRequested: editorPreview.exposure = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "CONTRAST"
                                    valueText: (editorPreview.contrast / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.contrast
                                    onValueDragged: function(value) { editorPreview.contrast = value }
                                    onResetRequested: editorPreview.contrast = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "BRIGHTNESS"
                                    valueText: (editorPreview.brightness / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.brightness
                                    onValueDragged: function(value) { editorPreview.brightness = value }
                                    onResetRequested: editorPreview.brightness = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "HDR LOCAL TONE"
                                    valueText: Math.round(editorPreview.hdrLocalToneMapping).toString()
                                    from: 0
                                    to: 100
                                    value: editorPreview.hdrLocalToneMapping
                                    onValueDragged: function(value) { editorPreview.hdrLocalToneMapping = value }
                                    onResetRequested: editorPreview.hdrLocalToneMapping = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "FILMIC SHOULDER"
                                    valueText: (editorPreview.filmicShoulder / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.filmicShoulder
                                    onValueDragged: function(value) { editorPreview.filmicShoulder = value }
                                    onResetRequested: editorPreview.filmicShoulder = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "FILMIC TOE"
                                    valueText: (editorPreview.filmicToe / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.filmicToe
                                    onValueDragged: function(value) { editorPreview.filmicToe = value }
                                    onResetRequested: editorPreview.filmicToe = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "ADAPTIVE CONTRAST"
                                    valueText: (editorPreview.adaptiveContrast / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.adaptiveContrast
                                    onValueDragged: function(value) { editorPreview.adaptiveContrast = value }
                                    onResetRequested: editorPreview.adaptiveContrast = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "LEVELS BLACK"
                                    valueText: Math.round(editorPreview.levelsBlackPoint).toString()
                                    from: 0
                                    to: 100
                                    value: editorPreview.levelsBlackPoint
                                    onValueDragged: function(value) { editorPreview.levelsBlackPoint = value }
                                    onResetRequested: editorPreview.levelsBlackPoint = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "LEVELS GAMMA"
                                    valueText: (editorPreview.levelsGamma / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.levelsGamma
                                    onValueDragged: function(value) { editorPreview.levelsGamma = value }
                                    onResetRequested: editorPreview.levelsGamma = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "LEVELS WHITE"
                                    valueText: Math.round(editorPreview.levelsWhitePoint).toString()
                                    from: 0
                                    to: 100
                                    value: editorPreview.levelsWhitePoint
                                    onValueDragged: function(value) { editorPreview.levelsWhitePoint = value }
                                    onResetRequested: editorPreview.levelsWhitePoint = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "ZONE DEEP SHADOWS"
                                    valueText: (editorPreview.zoneDeepShadows / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.zoneDeepShadows
                                    onValueDragged: function(value) { editorPreview.zoneDeepShadows = value }
                                    onResetRequested: editorPreview.zoneDeepShadows = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "ZONE SHADOWS"
                                    valueText: (editorPreview.zoneShadows / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.zoneShadows
                                    onValueDragged: function(value) { editorPreview.zoneShadows = value }
                                    onResetRequested: editorPreview.zoneShadows = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "ZONE MIDTONES"
                                    valueText: (editorPreview.zoneMidtones / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.zoneMidtones
                                    onValueDragged: function(value) { editorPreview.zoneMidtones = value }
                                    onResetRequested: editorPreview.zoneMidtones = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "ZONE LIGHTS"
                                    valueText: (editorPreview.zoneLights / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.zoneLights
                                    onValueDragged: function(value) { editorPreview.zoneLights = value }
                                    onResetRequested: editorPreview.zoneLights = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "ZONE HIGHLIGHTS"
                                    valueText: (editorPreview.zoneHighlights / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.zoneHighlights
                                    onValueDragged: function(value) { editorPreview.zoneHighlights = value }
                                    onResetRequested: editorPreview.zoneHighlights = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "HIGHLIGHTS"
                                    valueText: (editorPreview.highlights / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.highlights
                                    onValueDragged: function(value) { editorPreview.highlights = value }
                                    onResetRequested: editorPreview.highlights = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "SHADOWS"
                                    valueText: (editorPreview.shadows / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.shadows
                                    onValueDragged: function(value) { editorPreview.shadows = value }
                                    onResetRequested: editorPreview.shadows = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "WHITES"
                                    valueText: (editorPreview.whites / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.whites
                                    onValueDragged: function(value) { editorPreview.whites = value }
                                    onResetRequested: editorPreview.whites = 0
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "BLACKS"
                                    valueText: (editorPreview.blacks / 100).toFixed(2)
                                    from: -100
                                    to: 100
                                    value: editorPreview.blacks
                                    onValueDragged: function(value) { editorPreview.blacks = value }
                                    onResetRequested: editorPreview.blacks = 0
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Curves"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    ToneCurveEditor {
                                        Layout.fillWidth: true
                                        accent: editorPreview.curveChannel === "Luma"
                                                ? "#d8dce0"
                                                : (editorPreview.curveChannel === "Red"
                                                ? "#d45a62"
                                                : (editorPreview.curveChannel === "Green"
                                                   ? "#61b56f"
                                                   : (editorPreview.curveChannel === "Blue"
                                                      ? "#4e86d9"
                                                      : "#c99a52")))
                                        selectedChannel: editorPreview.curveChannel
                                        curveMode: editorPreview.activeCurveMode
                                        pointsModel: editorPreview.activeCurvePoints
                                        presetNames: editorPreview.curvePresetNames
                                        histogramRedData: editorPreview.histogramRed
                                        histogramGreenData: editorPreview.histogramGreen
                                        histogramBlueData: editorPreview.histogramBlue
                                        histogramLuminanceData: editorPreview.histogramLuminance
                                        histogramLogScale: histogramLogScale
                                        onChannelSelected: editorPreview.curveChannel = channel
                                        onPointsEdited: editorPreview.activeCurvePoints = points
                                        onPresetSelected: function(preset) { editorPreview.applyCurvePreset(preset) }
                                    }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Color Balance"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Item { Layout.fillWidth: true }

                                        ActionButton {
                                            compact: true
                                            iconName: "eyedropper"
                                            showText: false
                                            tooltipText: editorPreview.whiteBalanceSampling ? "Cancel white balance picker" : "Sample white balance from image"
                                            active: editorPreview.whiteBalanceSampling
                                            enabled: editorPreview.hasAsset && !editorPreview.compareMode
                                            onClicked: editorPreview.whiteBalanceSampling = !editorPreview.whiteBalanceSampling
                                        }
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "TEMP"
                                        valueText: Math.round(editorPreview.temperature).toString()
                                        trackColors: ["#4c78cb", "#c8d0d8", "#e6a14f"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.temperature
                                        onValueDragged: function(value) { editorPreview.temperature = value }
                                        onResetRequested: editorPreview.temperature = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "TINT"
                                        valueText: Math.round(editorPreview.tint).toString()
                                        trackColors: ["#4ea56b", "#c8d0d8", "#bb67d6"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.tint
                                        onValueDragged: function(value) { editorPreview.tint = value }
                                        onResetRequested: editorPreview.tint = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "VIBRANCE"
                                        valueText: Math.round(editorPreview.vibrance).toString()
                                        from: -100
                                        to: 100
                                        value: editorPreview.vibrance
                                        onValueDragged: function(value) { editorPreview.vibrance = value }
                                        onResetRequested: editorPreview.vibrance = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "SATURATION"
                                        valueText: Math.round(editorPreview.saturation).toString()
                                        from: -100
                                        to: 100
                                        value: editorPreview.saturation
                                        onValueDragged: function(value) { editorPreview.saturation = value }
                                        onResetRequested: editorPreview.saturation = 0
                                    }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Color Models"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Repeater {
                                            model: [
                                                { name: "HSL", color: "#d8dce0" },
                                                { name: "HSV", color: "#49b8c4" },
                                                { name: "LCH", color: "#c99a52" },
                                                { name: "OKLab", color: "#8b6be3" }
                                            ]

                                            delegate: ColorChannelButton {
                                                required property var modelData

                                                label: modelData.name
                                                swatch: modelData.color
                                                selected: editorPreview.colorMixerModel === modelData.name
                                                onClicked: editorPreview.colorMixerModel = modelData.name
                                            }
                                        }
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Repeater {
                                            model: hslChannels

                                            delegate: ColorChannelButton {
                                                required property var modelData

                                                label: modelData.name
                                                swatch: modelData.color
                                                selected: editorPreview.hslChannel === modelData.name
                                                onClicked: editorPreview.hslChannel = modelData.name
                                            }
                                        }
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "HUE"
                                        valueText: Math.round(editorPreview.hueAdjustment).toString()
                                        trackColors: ["#d54f60", "#d78c38", "#d7bf43", "#61b56f", "#49b8c4", "#4e86d9", "#8b6be3", "#c85ac1"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.hueAdjustment
                                        onValueDragged: function(value) { editorPreview.hueAdjustment = value }
                                        onResetRequested: editorPreview.hueAdjustment = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "SATURATION"
                                        valueText: Math.round(editorPreview.saturationAdjustment).toString()
                                        trackColors: ["#505963", selectedHslColor(), "#ffffff"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.saturationAdjustment
                                        onValueDragged: function(value) { editorPreview.saturationAdjustment = value }
                                        onResetRequested: editorPreview.saturationAdjustment = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "LUMINANCE"
                                        valueText: Math.round(editorPreview.luminanceAdjustment).toString()
                                        trackColors: ["#101419", selectedHslColor(), "#eff3f6"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.luminanceAdjustment
                                        onValueDragged: function(value) { editorPreview.luminanceAdjustment = value }
                                        onResetRequested: editorPreview.luminanceAdjustment = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "CYAN"
                                        valueText: Math.round(editorPreview.selectiveCyan).toString()
                                        trackColors: [selectedHslColor(), "#49b8c4"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.selectiveCyan
                                        onValueDragged: function(value) { editorPreview.selectiveCyan = value }
                                        onResetRequested: editorPreview.selectiveCyan = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "MAGENTA"
                                        valueText: Math.round(editorPreview.selectiveMagenta).toString()
                                        trackColors: [selectedHslColor(), "#c85ac1"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.selectiveMagenta
                                        onValueDragged: function(value) { editorPreview.selectiveMagenta = value }
                                        onResetRequested: editorPreview.selectiveMagenta = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "YELLOW"
                                        valueText: Math.round(editorPreview.selectiveYellow).toString()
                                        trackColors: [selectedHslColor(), "#d7bf43"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.selectiveYellow
                                        onValueDragged: function(value) { editorPreview.selectiveYellow = value }
                                        onResetRequested: editorPreview.selectiveYellow = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "BLACK"
                                        valueText: Math.round(editorPreview.selectiveBlack).toString()
                                        trackColors: ["#eff3f6", selectedHslColor(), "#101419"]
                                        from: -100
                                        to: 100
                                        value: editorPreview.selectiveBlack
                                        onValueDragged: function(value) { editorPreview.selectiveBlack = value }
                                        onResetRequested: editorPreview.selectiveBlack = 0
                                    }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Channel Mixer"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        ChannelMixerButton {
                                            Layout.fillWidth: true
                                            text: "Identity"
                                            actionValue: "Identity"
                                            helperText: "Apply neutral channel mixer coefficients."
                                            onActionRequested: function(presetName) { editorPreview.applyChannelMixerPreset(presetName) }
                                        }
                                        ChannelMixerButton {
                                            Layout.fillWidth: true
                                            text: "Mono"
                                            actionValue: "Monochrome"
                                            helperText: "Convert the mix to monochrome."
                                            onActionRequested: function(presetName) { editorPreview.applyChannelMixerPreset(presetName) }
                                        }
                                        ChannelMixerButton {
                                            Layout.fillWidth: true
                                            text: "Swap RB"
                                            actionValue: "Swap Red Blue"
                                            helperText: "Swap red and blue channel contribution."
                                            onActionRequested: function(presetName) { editorPreview.applyChannelMixerPreset(presetName) }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: "Output Red"
                                            color: theme.textStrong
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                        }
                                        ChannelMixerButton {
                                            text: "Reset"
                                            iconOnlyAction: true
                                            actionValue: "red"
                                            helperText: "Reset the output red row."
                                            onActionRequested: function(rowName) { editorPreview.resetChannelMixerRow(rowName) }
                                        }
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM RED"
                                        valueText: Math.round(editorPreview.channelMixerRedFromRed).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerRedFromRed
                                        onValueDragged: function(value) { editorPreview.channelMixerRedFromRed = value }
                                        onResetRequested: editorPreview.channelMixerRedFromRed = 100
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM GREEN"
                                        valueText: Math.round(editorPreview.channelMixerRedFromGreen).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerRedFromGreen
                                        onValueDragged: function(value) { editorPreview.channelMixerRedFromGreen = value }
                                        onResetRequested: editorPreview.channelMixerRedFromGreen = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM BLUE"
                                        valueText: Math.round(editorPreview.channelMixerRedFromBlue).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerRedFromBlue
                                        onValueDragged: function(value) { editorPreview.channelMixerRedFromBlue = value }
                                        onResetRequested: editorPreview.channelMixerRedFromBlue = 0
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: "Output Green"
                                            color: theme.textStrong
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                        }
                                        ChannelMixerButton {
                                            text: "Reset"
                                            iconOnlyAction: true
                                            actionValue: "green"
                                            helperText: "Reset the output green row."
                                            onActionRequested: function(rowName) { editorPreview.resetChannelMixerRow(rowName) }
                                        }
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM RED"
                                        valueText: Math.round(editorPreview.channelMixerGreenFromRed).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerGreenFromRed
                                        onValueDragged: function(value) { editorPreview.channelMixerGreenFromRed = value }
                                        onResetRequested: editorPreview.channelMixerGreenFromRed = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM GREEN"
                                        valueText: Math.round(editorPreview.channelMixerGreenFromGreen).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerGreenFromGreen
                                        onValueDragged: function(value) { editorPreview.channelMixerGreenFromGreen = value }
                                        onResetRequested: editorPreview.channelMixerGreenFromGreen = 100
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM BLUE"
                                        valueText: Math.round(editorPreview.channelMixerGreenFromBlue).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerGreenFromBlue
                                        onValueDragged: function(value) { editorPreview.channelMixerGreenFromBlue = value }
                                        onResetRequested: editorPreview.channelMixerGreenFromBlue = 0
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: "Output Blue"
                                            color: theme.textStrong
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                        }
                                        ChannelMixerButton {
                                            text: "Reset"
                                            iconOnlyAction: true
                                            actionValue: "blue"
                                            helperText: "Reset the output blue row."
                                            onActionRequested: function(rowName) { editorPreview.resetChannelMixerRow(rowName) }
                                        }
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM RED"
                                        valueText: Math.round(editorPreview.channelMixerBlueFromRed).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerBlueFromRed
                                        onValueDragged: function(value) { editorPreview.channelMixerBlueFromRed = value }
                                        onResetRequested: editorPreview.channelMixerBlueFromRed = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM GREEN"
                                        valueText: Math.round(editorPreview.channelMixerBlueFromGreen).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerBlueFromGreen
                                        onValueDragged: function(value) { editorPreview.channelMixerBlueFromGreen = value }
                                        onResetRequested: editorPreview.channelMixerBlueFromGreen = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "FROM BLUE"
                                        valueText: Math.round(editorPreview.channelMixerBlueFromBlue).toString()
                                        from: -200
                                        to: 200
                                        value: editorPreview.channelMixerBlueFromBlue
                                        onValueDragged: function(value) { editorPreview.channelMixerBlueFromBlue = value }
                                        onResetRequested: editorPreview.channelMixerBlueFromBlue = 100
                                    }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Black & White"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    ToolOptionToggle {
                                        label: "Enable"
                                        checked: editorPreview.blackAndWhiteEnabled
                                        helperText: "Enable black and white channel mixing."
                                        onToggled: function(checked) { editorPreview.blackAndWhiteEnabled = checked }
                                    }

                                    InspectorSlider { Layout.fillWidth: true; label: "REDS"; valueText: Math.round(editorPreview.blackAndWhiteReds).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteReds; onValueDragged: function(value) { editorPreview.blackAndWhiteReds = value }; onResetRequested: editorPreview.blackAndWhiteReds = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "ORANGES"; valueText: Math.round(editorPreview.blackAndWhiteOranges).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteOranges; onValueDragged: function(value) { editorPreview.blackAndWhiteOranges = value }; onResetRequested: editorPreview.blackAndWhiteOranges = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "YELLOWS"; valueText: Math.round(editorPreview.blackAndWhiteYellows).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteYellows; onValueDragged: function(value) { editorPreview.blackAndWhiteYellows = value }; onResetRequested: editorPreview.blackAndWhiteYellows = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "GREENS"; valueText: Math.round(editorPreview.blackAndWhiteGreens).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteGreens; onValueDragged: function(value) { editorPreview.blackAndWhiteGreens = value }; onResetRequested: editorPreview.blackAndWhiteGreens = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "AQUAS"; valueText: Math.round(editorPreview.blackAndWhiteAquas).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteAquas; onValueDragged: function(value) { editorPreview.blackAndWhiteAquas = value }; onResetRequested: editorPreview.blackAndWhiteAquas = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "BLUES"; valueText: Math.round(editorPreview.blackAndWhiteBlues).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteBlues; onValueDragged: function(value) { editorPreview.blackAndWhiteBlues = value }; onResetRequested: editorPreview.blackAndWhiteBlues = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "PURPLES"; valueText: Math.round(editorPreview.blackAndWhitePurples).toString(); from: -100; to: 100; value: editorPreview.blackAndWhitePurples; onValueDragged: function(value) { editorPreview.blackAndWhitePurples = value }; onResetRequested: editorPreview.blackAndWhitePurples = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "MAGENTAS"; valueText: Math.round(editorPreview.blackAndWhiteMagentas).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteMagentas; onValueDragged: function(value) { editorPreview.blackAndWhiteMagentas = value }; onResetRequested: editorPreview.blackAndWhiteMagentas = 0 }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Color Replace"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    ToolOptionToggle {
                                        label: "Enable"
                                        checked: editorPreview.colorReplacementEnabled
                                        helperText: "Enable targeted hue replacement."
                                        onToggled: function(checked) { editorPreview.colorReplacementEnabled = checked }
                                    }
                                    InspectorSlider { Layout.fillWidth: true; label: "SOURCE HUE"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.colorReplacementSourceHue * 360).toString() + "°"; from: 0; to: 1; value: editorPreview.colorReplacementSourceHue; onValueDragged: function(value) { editorPreview.colorReplacementSourceHue = value }; onResetRequested: editorPreview.colorReplacementSourceHue = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "TARGET HUE"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.colorReplacementTargetHue * 360).toString() + "°"; from: 0; to: 1; value: editorPreview.colorReplacementTargetHue; onValueDragged: function(value) { editorPreview.colorReplacementTargetHue = value }; onResetRequested: editorPreview.colorReplacementTargetHue = 0.33 }
                                    InspectorSlider { Layout.fillWidth: true; label: "RANGE"; valueText: Math.round(editorPreview.colorReplacementRange).toString(); from: 1; to: 100; value: editorPreview.colorReplacementRange; onValueDragged: function(value) { editorPreview.colorReplacementRange = value }; onResetRequested: editorPreview.colorReplacementRange = 35 }
                                    InspectorSlider { Layout.fillWidth: true; label: "SOFTNESS"; valueText: Math.round(editorPreview.colorReplacementSoftness).toString(); from: 0; to: 100; value: editorPreview.colorReplacementSoftness; onValueDragged: function(value) { editorPreview.colorReplacementSoftness = value }; onResetRequested: editorPreview.colorReplacementSoftness = 100 }
                                    InspectorSlider { Layout.fillWidth: true; label: "STRENGTH"; valueText: Math.round(editorPreview.colorReplacementStrength).toString(); from: 0; to: 100; value: editorPreview.colorReplacementStrength; onValueDragged: function(value) { editorPreview.colorReplacementStrength = value }; onResetRequested: editorPreview.colorReplacementStrength = 0 }
                                    ToolOptionToggle {
                                        label: "Preserve Luminance"
                                        checked: editorPreview.colorReplacementPreserveLuminance
                                        helperText: "Keep source luminance while replacing hue."
                                        onToggled: function(checked) { editorPreview.colorReplacementPreserveLuminance = checked }
                                    }

                                    ToolOptionToggle {
                                        label: "Preserve Saturation"
                                        checked: editorPreview.colorReplacementPreserveSaturation
                                        helperText: "Keep source saturation while replacing hue."
                                        onToggled: function(checked) { editorPreview.colorReplacementPreserveSaturation = checked }
                                    }

                                    ToolOptionToggle {
                                        objectName: "colorReplaceProtectSkinCheckBox"
                                        label: "Protect Skin"
                                        checked: editorPreview.skinToneProtectionEnabled
                                        helperText: "Protect skin tones from replacement spill."
                                        onToggled: function(checked) { editorPreview.skinToneProtectionEnabled = checked }
                                    }
                                    InspectorSlider { objectName: "colorReplaceSkinHueSlider"; Layout.fillWidth: true; visible: editorPreview.skinToneProtectionEnabled; label: "PROTECTED HUE"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.skinToneLineAngle).toString() + "°"; from: 0; to: 1; value: editorPreview.skinToneLineHue; onValueDragged: function(value) { editorPreview.skinToneLineHue = value }; onResetRequested: editorPreview.skinToneLineHue = 0.08 }
                                    InspectorSlider { objectName: "colorReplaceSkinRangeSlider"; Layout.fillWidth: true; visible: editorPreview.skinToneProtectionEnabled; label: "HUE RANGE"; valueText: Math.round(editorPreview.skinToneProtectionRange).toString(); from: 1; to: 100; value: editorPreview.skinToneProtectionRange; onValueDragged: function(value) { editorPreview.skinToneProtectionRange = value }; onResetRequested: editorPreview.skinToneProtectionRange = 30 }
                                    InspectorSlider { objectName: "colorReplaceSkinProtectionSlider"; Layout.fillWidth: true; visible: editorPreview.skinToneProtectionEnabled; label: "PROTECTION"; valueText: Math.round(editorPreview.skinToneProtectionStrength).toString(); from: 0; to: 100; value: editorPreview.skinToneProtectionStrength; onValueDragged: function(value) { editorPreview.skinToneProtectionStrength = value }; onResetRequested: editorPreview.skinToneProtectionStrength = 0 }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Skin Protection Lab"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    ToolOptionToggle {
                                        label: "Protect"
                                        checked: editorPreview.skinToneProtectionEnabled
                                        helperText: "Enable skin-tone protection."
                                        onToggled: function(checked) { editorPreview.skinToneProtectionEnabled = checked }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 28
                                        color: "transparent"
                                        border.color: theme.lineSoft
                                        border.width: 1
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: parent.width * 0.82
                                            height: 2
                                            color: theme.accentWarm400
                                            rotation: editorPreview.skinToneLineAngle - 20
                                        }
                                    }
                                    InspectorSlider { Layout.fillWidth: true; label: "PROTECTED HUE"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.skinToneLineAngle).toString() + "°"; from: 0; to: 1; value: editorPreview.skinToneLineHue; onValueDragged: function(value) { editorPreview.skinToneLineHue = value }; onResetRequested: editorPreview.skinToneLineHue = 0.08 }
                                    InspectorSlider { Layout.fillWidth: true; label: "HUE RANGE"; valueText: Math.round(editorPreview.skinToneProtectionRange).toString(); from: 1; to: 100; value: editorPreview.skinToneProtectionRange; onValueDragged: function(value) { editorPreview.skinToneProtectionRange = value }; onResetRequested: editorPreview.skinToneProtectionRange = 30 }
                                    InspectorSlider { Layout.fillWidth: true; label: "PROTECTION"; valueText: Math.round(editorPreview.skinToneProtectionStrength).toString(); from: 0; to: 100; value: editorPreview.skinToneProtectionStrength; onValueDragged: function(value) { editorPreview.skinToneProtectionStrength = value }; onResetRequested: editorPreview.skinToneProtectionStrength = 0 }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Harmony Lab"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    ToolOptionToggle {
                                        label: "Enable"
                                        checked: editorPreview.colorHarmonyEnabled
                                        helperText: "Enable harmony palette guidance."
                                        onToggled: function(checked) { editorPreview.colorHarmonyEnabled = checked }
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Repeater {
                                            model: ["Complementary", "Analogous", "Triadic", "Split Complementary", "Tetradic"]
                                            delegate: ColorChannelButton {
                                                required property string modelData
                                                label: modelData === "Split Complementary" ? "Split" : modelData
                                                swatch: theme.accentWarm400
                                                selected: editorPreview.colorHarmonyMode === modelData
                                                onClicked: editorPreview.colorHarmonyMode = modelData
                                            }
                                        }
                                    }
                                    InspectorSlider { Layout.fillWidth: true; label: "BASE HUE"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.colorHarmonyBaseHue * 360).toString() + "°"; from: 0; to: 1; value: editorPreview.colorHarmonyBaseHue; onValueDragged: function(value) { editorPreview.colorHarmonyBaseHue = value }; onResetRequested: editorPreview.colorHarmonyBaseHue = 0 }
                                    InspectorSlider { Layout.fillWidth: true; label: "HUE PULL"; valueText: Math.round(editorPreview.colorHarmonyStrength).toString(); from: 0; to: 100; value: editorPreview.colorHarmonyStrength; onValueDragged: function(value) { editorPreview.colorHarmonyStrength = value }; onResetRequested: editorPreview.colorHarmonyStrength = 0 }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Split Toning"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 2
                                        columnSpacing: 10
                                        rowSpacing: 10

                                        ColorWheelControl {
                                            Layout.fillWidth: true
                                            title: "Shadows"
                                            hue: editorPreview.gradingShadowsHue
                                            saturation: editorPreview.gradingShadowsSaturation
                                            onHueEdited: function(value) { editorPreview.gradingShadowsHue = value }
                                            onSaturationEdited: function(value) { editorPreview.gradingShadowsSaturation = value }
                                        }

                                        ColorWheelControl {
                                            Layout.fillWidth: true
                                            title: "Midtones"
                                            hue: editorPreview.gradingMidtonesHue
                                            saturation: editorPreview.gradingMidtonesSaturation
                                            onHueEdited: function(value) { editorPreview.gradingMidtonesHue = value }
                                            onSaturationEdited: function(value) { editorPreview.gradingMidtonesSaturation = value }
                                        }

                                        ColorWheelControl {
                                            Layout.fillWidth: true
                                            title: "Highlights"
                                            hue: editorPreview.gradingHighlightsHue
                                            saturation: editorPreview.gradingHighlightsSaturation
                                            onHueEdited: function(value) { editorPreview.gradingHighlightsHue = value }
                                            onSaturationEdited: function(value) { editorPreview.gradingHighlightsSaturation = value }
                                        }

                                        ColorWheelControl {
                                            Layout.fillWidth: true
                                            title: "Global"
                                            hue: editorPreview.temperature
                                            saturation: editorPreview.vibrance
                                            onHueEdited: function(value) { editorPreview.temperature = value }
                                            onSaturationEdited: function(value) { editorPreview.vibrance = value }
                                        }
                                    }
                                }

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Detail"
                                    showAccentMarker: false
                                    uppercaseTitle: true
                                    titleFontFamily: theme.fontMono
                                    titlePixelSize: 10
                                    titleLetterSpacing: 0.5
                                    expandable: true
                                    expanded: false

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "SHARPENING"
                                        valueText: Math.round(editorPreview.sharpening).toString()
                                        from: 0
                                        to: 100
                                        value: editorPreview.sharpening
                                        onValueDragged: function(value) { editorPreview.sharpening = value }
                                        onResetRequested: editorPreview.sharpening = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "MASKING"
                                        valueText: Math.round(editorPreview.sharpenMasking).toString()
                                        from: 0
                                        to: 100
                                        value: editorPreview.sharpenMasking
                                        onValueDragged: function(value) { editorPreview.sharpenMasking = value }
                                        onResetRequested: editorPreview.sharpenMasking = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "LUMA NR"
                                        valueText: Math.round(editorPreview.luminanceNr).toString()
                                        from: 0
                                        to: 100
                                        value: editorPreview.luminanceNr
                                        onValueDragged: function(value) { editorPreview.luminanceNr = value }
                                        onResetRequested: editorPreview.luminanceNr = 0
                                    }

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "COLOR NR"
                                        valueText: Math.round(editorPreview.colorNr).toString()
                                        from: 0
                                        to: 100
                                        value: editorPreview.colorNr
                                        onValueDragged: function(value) { editorPreview.colorNr = value }
                                        onResetRequested: editorPreview.colorNr = 0
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: theme.alpha(theme.lineSoft, 0.30)
                                }

                                Label {
                                    text: "NAVIGATOR"
                                    color: theme.textMain
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    font.letterSpacing: 1.0
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 126
                                    color: theme.alpha(theme.ink950, 0.54)
                                    border.width: 1
                                    border.color: theme.alpha(theme.lineSoft, 0.24)
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        source: editedDisplaySource
                                        fillMode: Image.PreserveAspectCrop
                                        opacity: 0.82
                                        asynchronous: true
                                        cache: false
                                    }

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width * 0.62
                                        height: parent.height * 0.58
                                        color: "transparent"
                                        border.width: 1
                                        border.color: theme.alpha(theme.polar, 0.82)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 14

                                    Label { text: "FIT"; color: theme.vectorGreen; font.family: theme.fontMono; font.pixelSize: 8; font.underline: true }
                                    Label { text: "FILL"; color: theme.textFaint; font.family: theme.fontMono; font.pixelSize: 8 }
                                    Label { text: "1:1"; color: theme.textFaint; font.family: theme.fontMono; font.pixelSize: 8 }
                                    Label { text: "2:1"; color: theme.textFaint; font.family: theme.fontMono; font.pixelSize: 8 }
                                    Item { Layout.fillWidth: true }
                                    Label { text: Math.round(currentZoomLevel() * 100) + "%"; color: theme.textMuted; font.family: theme.fontMono; font.pixelSize: 8 }
                                }
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust" && inspectorTab === "info"
                            title: "Info"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 10
                            titleLetterSpacing: 0.5
                            expandable: false

                            Label {
                                Layout.fillWidth: true
                                text: "Resolution: " + (editorPreview.displayPreviewNativeSize.width > 0 ? editorPreview.displayPreviewNativeSize.width + " x " + editorPreview.displayPreviewNativeSize.height : "Unknown")
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: 11
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Color space: " + editorPreview.outputColorSpace
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: 11
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Camera: " + (editorPreview.cameraLabel.length > 0 ? editorPreview.cameraLabel : "Unknown")
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: 11
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                ActionButton {
                                    text: editorPreview.hasCameraDefault ? "Update Camera Default" : "Save Camera Default"
                                    compact: true
                                    tone: "primary"
                                    enabled: editorPreview.hasAsset && editorPreview.cameraLabel.length > 0
                                    onClicked: editorPreview.saveCameraDefault()
                                }

                                ConfirmActionButton {
                                    text: "Clear Default"
                                    compact: true
                                    enabled: editorPreview.hasCameraDefault
                                    helperText: "Clear saved defaults for this camera"
                                    onConfirmed: editorPreview.clearCameraDefault()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: editorPreview.rawControlsAvailable
                                spacing: 8

                                Label {
                                    text: "Camera Profile"
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                }

                                ToneComboBox {
                                    Layout.fillWidth: true
                                    model: [
                                        "Embedded",
                                        "Camera Native",
                                        "RAW Decoder Default",
                                        "Camera DCP: Adobe Standard",
                                        "Camera DCP: Camera Neutral",
                                        "Camera ICC: Manufacturer"
                                    ]
                                    currentIndex: Math.max(0, model.indexOf(editorPreview.inputColorProfile))
                                    onActivated: editorPreview.inputColorProfile = currentText
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: editorPreview.rawControlsAvailable
                                spacing: 8

                                Label {
                                    text: "Camera Look"
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                }

                                ToneComboBox {
                                    Layout.fillWidth: true
                                    model: ["Standard", "Neutral", "Vivid", "Portrait", "Landscape", "Monochrome"]
                                    currentIndex: Math.max(0, model.indexOf(editorPreview.rawCameraMatchingLook))
                                    onActivated: editorPreview.rawCameraMatchingLook = currentText
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Lens: " + (editorPreview.lensModel.length > 0 ? editorPreview.lensModel : "Unknown")
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }

                        MaskInspectorSections {
                            width: parent.width
                            visible: inspectorMode === "masks" && inspectorTab === "masks"
                            editRoot: root
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust" && editorPreview.rawControlsAvailable
                            title: "RAW Decode"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW Exposure"
                                valueText: editorPreview.rawExposure.toFixed(2) + " EV"
                                from: -4
                                to: 4
                                value: editorPreview.rawExposure
                                onValueDragged: function(value) { editorPreview.rawExposure = value }
                                onResetRequested: editorPreview.rawExposure = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Boost"
                                valueText: editorPreview.rawBoost.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawBoost
                                onValueDragged: function(value) { editorPreview.rawBoost = value }
                                onResetRequested: editorPreview.rawBoost = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadow Boost"
                                valueText: editorPreview.rawShadowBoost.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawShadowBoost
                                onValueDragged: function(value) { editorPreview.rawShadowBoost = value }
                                onResetRequested: editorPreview.rawShadowBoost = 1
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW Detail"
                                valueText: editorPreview.rawDetail.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawDetail
                                onValueDragged: function(value) { editorPreview.rawDetail = value }
                                onResetRequested: editorPreview.rawDetail = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW Chroma NR"
                                valueText: editorPreview.rawChromaNoiseReduction.toFixed(2)
                                from: 0
                                to: 1
                                value: editorPreview.rawChromaNoiseReduction
                                onValueDragged: function(value) { editorPreview.rawChromaNoiseReduction = value }
                                onResetRequested: editorPreview.rawChromaNoiseReduction = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Moire Reduction"
                                valueText: editorPreview.rawMoireReduction.toFixed(2)
                                from: 0
                                to: 1
                                value: editorPreview.rawMoireReduction
                                onValueDragged: function(value) { editorPreview.rawMoireReduction = value }
                                onResetRequested: editorPreview.rawMoireReduction = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hot/Dead Pixels"
                                valueText: editorPreview.rawHotDeadPixelCleanup.toFixed(2)
                                from: 0
                                to: 1
                                value: editorPreview.rawHotDeadPixelCleanup
                                onValueDragged: function(value) { editorPreview.rawHotDeadPixelCleanup = value }
                                onResetRequested: editorPreview.rawHotDeadPixelCleanup = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "False Color"
                                valueText: editorPreview.rawFalseColorSuppression.toFixed(2)
                                from: 0
                                to: 1
                                value: editorPreview.rawFalseColorSuppression
                                onValueDragged: function(value) { editorPreview.rawFalseColorSuppression = value }
                                onResetRequested: editorPreview.rawFalseColorSuppression = 0
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label {
                                    text: "Demosaic"
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                }

                                ToneComboBox {
                                    Layout.fillWidth: true
                                    model: ["Preview", "Balanced", "Full"]
                                    currentIndex: Math.max(0, model.indexOf(editorPreview.rawDemosaicQuality))
                                    onActivated: editorPreview.rawDemosaicQuality = currentText
                                }
                            }

                            ToolOptionToggle {
                                label: "Highlight Recovery"
                                checked: editorPreview.rawHighlightRecovery
                                helperText: "Recover clipped highlights from RAW data."
                                onToggled: function(checked) { editorPreview.rawHighlightRecovery = checked }
                            }

                            ToolOptionToggle {
                                label: "Gamut Mapping"
                                checked: editorPreview.rawGamutMapping
                                helperText: "Map out-of-gamut RAW colors into the working gamut."
                                onToggled: function(checked) { editorPreview.rawGamutMapping = checked }
                            }

                            ToolOptionToggle {
                                label: "Lens Correction"
                                checked: editorPreview.rawLensCorrection
                                helperText: "Apply available RAW lens correction metadata."
                                onToggled: function(checked) { editorPreview.rawLensCorrection = checked }
                            }

                            ToolOptionToggle {
                                label: "Override White Balance"
                                checked: editorPreview.rawWhiteBalanceOverride
                                helperText: "Use manual RAW white balance controls instead of as-shot values."
                                onToggled: function(checked) { editorPreview.rawWhiteBalanceOverride = checked }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    Layout.fillWidth: true
                                    text: "As Shot " + Math.round(editorPreview.rawAsShotWhiteBalanceTemperature).toString() + " K / " + Math.round(editorPreview.rawAsShotWhiteBalanceTint).toString()
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    fontSizeMode: Text.HorizontalFit
                                    minimumPixelSize: 8
                                    elide: Text.ElideNone
                                }

                                ActionButton {
                                    compact: true
                                    iconName: "rotate-ccw"
                                    text: "Use"
                                    tooltipText: "Use as-shot RAW white balance"
                                    enabled: editorPreview.rawControlsAvailable
                                    onClicked: editorPreview.useAsShotRawWhiteBalance()
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: editorPreview.rawClipSampleCount > 0
                                text: "RAW clipping  Blk " + editorPreview.rawShadowClipCount + " / Wht " + editorPreview.rawHighlightClipCount
                                color: (editorPreview.rawShadowClipCount > 0 || editorPreview.rawHighlightClipCount > 0) ? theme.accentWarm400 : theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 8
                                elide: Text.ElideNone
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW Black Level"
                                valueText: editorPreview.rawBlackLevel.toFixed(3)
                                from: 0
                                to: 0.25
                                value: editorPreview.rawBlackLevel
                                onValueDragged: function(value) { editorPreview.rawBlackLevel = value }
                                onResetRequested: editorPreview.rawBlackLevel = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW White Level"
                                valueText: editorPreview.rawWhiteLevel.toFixed(2)
                                from: 0.5
                                to: 2
                                value: editorPreview.rawWhiteLevel
                                onValueDragged: function(value) { editorPreview.rawWhiteLevel = value }
                                onResetRequested: editorPreview.rawWhiteLevel = 1
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Calibration Hue"
                                valueText: Math.round(editorPreview.rawCalibrationHue).toString()
                                from: -100
                                to: 100
                                value: editorPreview.rawCalibrationHue
                                onValueDragged: function(value) { editorPreview.rawCalibrationHue = value }
                                onResetRequested: editorPreview.rawCalibrationHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Calibration Sat"
                                valueText: Math.round(editorPreview.rawCalibrationSaturation).toString()
                                from: -100
                                to: 100
                                value: editorPreview.rawCalibrationSaturation
                                onValueDragged: function(value) { editorPreview.rawCalibrationSaturation = value }
                                onResetRequested: editorPreview.rawCalibrationSaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Red Primary Hue"
                                valueText: Math.round(editorPreview.rawRedPrimaryHue).toString()
                                from: -100
                                to: 100
                                value: editorPreview.rawRedPrimaryHue
                                onValueDragged: function(value) { editorPreview.rawRedPrimaryHue = value }
                                onResetRequested: editorPreview.rawRedPrimaryHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Red Primary Sat"
                                valueText: Math.round(editorPreview.rawRedPrimarySaturation).toString()
                                from: -100
                                to: 100
                                value: editorPreview.rawRedPrimarySaturation
                                onValueDragged: function(value) { editorPreview.rawRedPrimarySaturation = value }
                                onResetRequested: editorPreview.rawRedPrimarySaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Green Primary Hue"
                                valueText: Math.round(editorPreview.rawGreenPrimaryHue).toString()
                                from: -100
                                to: 100
                                value: editorPreview.rawGreenPrimaryHue
                                onValueDragged: function(value) { editorPreview.rawGreenPrimaryHue = value }
                                onResetRequested: editorPreview.rawGreenPrimaryHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Green Primary Sat"
                                valueText: Math.round(editorPreview.rawGreenPrimarySaturation).toString()
                                from: -100
                                to: 100
                                value: editorPreview.rawGreenPrimarySaturation
                                onValueDragged: function(value) { editorPreview.rawGreenPrimarySaturation = value }
                                onResetRequested: editorPreview.rawGreenPrimarySaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Blue Primary Hue"
                                valueText: Math.round(editorPreview.rawBluePrimaryHue).toString()
                                from: -100
                                to: 100
                                value: editorPreview.rawBluePrimaryHue
                                onValueDragged: function(value) { editorPreview.rawBluePrimaryHue = value }
                                onResetRequested: editorPreview.rawBluePrimaryHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Blue Primary Sat"
                                valueText: Math.round(editorPreview.rawBluePrimarySaturation).toString()
                                from: -100
                                to: 100
                                value: editorPreview.rawBluePrimarySaturation
                                onValueDragged: function(value) { editorPreview.rawBluePrimarySaturation = value }
                                onResetRequested: editorPreview.rawBluePrimarySaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                enabled: editorPreview.rawWhiteBalanceOverride
                                label: "RAW Temp"
                                valueText: Math.round(editorPreview.rawWhiteBalanceTemperature).toString() + " K"
                                from: 2000
                                to: 15000
                                value: editorPreview.rawWhiteBalanceTemperature
                                onValueDragged: function(value) { editorPreview.rawWhiteBalanceTemperature = value }
                                onResetRequested: editorPreview.rawWhiteBalanceTemperature = 5200
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                enabled: editorPreview.rawWhiteBalanceOverride
                                label: "RAW Tint"
                                valueText: Math.round(editorPreview.rawWhiteBalanceTint).toString()
                                trackColors: ["#4ea56b", "#c8d0d8", "#bb67d6"]
                                from: -150
                                to: 150
                                value: editorPreview.rawWhiteBalanceTint
                                onValueDragged: function(value) { editorPreview.rawWhiteBalanceTint = value }
                                onResetRequested: editorPreview.rawWhiteBalanceTint = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Channel Mixer"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                ChannelMixerButton {
                                    Layout.fillWidth: true
                                    text: "Identity"
                                    actionValue: "Identity"
                                    helperText: "Apply neutral channel mixer coefficients."
                                    onActionRequested: function(presetName) { editorPreview.applyChannelMixerPreset(presetName) }
                                }
                                ChannelMixerButton {
                                    Layout.fillWidth: true
                                    text: "Mono"
                                    actionValue: "Monochrome"
                                    helperText: "Convert the mix to monochrome."
                                    onActionRequested: function(presetName) { editorPreview.applyChannelMixerPreset(presetName) }
                                }
                                ChannelMixerButton {
                                    Layout.fillWidth: true
                                    text: "Swap RB"
                                    actionValue: "Swap Red Blue"
                                    helperText: "Swap red and blue channel contribution."
                                    onActionRequested: function(presetName) { editorPreview.applyChannelMixerPreset(presetName) }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: "Output Red"
                                    color: theme.textStrong
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                                ChannelMixerButton {
                                    text: "Reset"
                                    iconOnlyAction: true
                                    actionValue: "red"
                                    helperText: "Reset the output red row."
                                    onActionRequested: function(rowName) { editorPreview.resetChannelMixerRow(rowName) }
                                }
                            }
                            InspectorSlider { Layout.fillWidth: true; label: "From Red"; valueText: Math.round(editorPreview.channelMixerRedFromRed).toString(); from: -200; to: 200; value: editorPreview.channelMixerRedFromRed; onValueDragged: function(value) { editorPreview.channelMixerRedFromRed = value }; onResetRequested: editorPreview.channelMixerRedFromRed = 100 }
                            InspectorSlider { Layout.fillWidth: true; label: "From Green"; valueText: Math.round(editorPreview.channelMixerRedFromGreen).toString(); from: -200; to: 200; value: editorPreview.channelMixerRedFromGreen; onValueDragged: function(value) { editorPreview.channelMixerRedFromGreen = value }; onResetRequested: editorPreview.channelMixerRedFromGreen = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "From Blue"; valueText: Math.round(editorPreview.channelMixerRedFromBlue).toString(); from: -200; to: 200; value: editorPreview.channelMixerRedFromBlue; onValueDragged: function(value) { editorPreview.channelMixerRedFromBlue = value }; onResetRequested: editorPreview.channelMixerRedFromBlue = 0 }
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: "Output Green"
                                    color: theme.textStrong
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                                ChannelMixerButton {
                                    text: "Reset"
                                    iconOnlyAction: true
                                    actionValue: "green"
                                    helperText: "Reset the output green row."
                                    onActionRequested: function(rowName) { editorPreview.resetChannelMixerRow(rowName) }
                                }
                            }
                            InspectorSlider { Layout.fillWidth: true; label: "From Red"; valueText: Math.round(editorPreview.channelMixerGreenFromRed).toString(); from: -200; to: 200; value: editorPreview.channelMixerGreenFromRed; onValueDragged: function(value) { editorPreview.channelMixerGreenFromRed = value }; onResetRequested: editorPreview.channelMixerGreenFromRed = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "From Green"; valueText: Math.round(editorPreview.channelMixerGreenFromGreen).toString(); from: -200; to: 200; value: editorPreview.channelMixerGreenFromGreen; onValueDragged: function(value) { editorPreview.channelMixerGreenFromGreen = value }; onResetRequested: editorPreview.channelMixerGreenFromGreen = 100 }
                            InspectorSlider { Layout.fillWidth: true; label: "From Blue"; valueText: Math.round(editorPreview.channelMixerGreenFromBlue).toString(); from: -200; to: 200; value: editorPreview.channelMixerGreenFromBlue; onValueDragged: function(value) { editorPreview.channelMixerGreenFromBlue = value }; onResetRequested: editorPreview.channelMixerGreenFromBlue = 0 }
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: "Output Blue"
                                    color: theme.textStrong
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                                ChannelMixerButton {
                                    text: "Reset"
                                    iconOnlyAction: true
                                    actionValue: "blue"
                                    helperText: "Reset the output blue row."
                                    onActionRequested: function(rowName) { editorPreview.resetChannelMixerRow(rowName) }
                                }
                            }
                            InspectorSlider { Layout.fillWidth: true; label: "From Red"; valueText: Math.round(editorPreview.channelMixerBlueFromRed).toString(); from: -200; to: 200; value: editorPreview.channelMixerBlueFromRed; onValueDragged: function(value) { editorPreview.channelMixerBlueFromRed = value }; onResetRequested: editorPreview.channelMixerBlueFromRed = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "From Green"; valueText: Math.round(editorPreview.channelMixerBlueFromGreen).toString(); from: -200; to: 200; value: editorPreview.channelMixerBlueFromGreen; onValueDragged: function(value) { editorPreview.channelMixerBlueFromGreen = value }; onResetRequested: editorPreview.channelMixerBlueFromGreen = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "From Blue"; valueText: Math.round(editorPreview.channelMixerBlueFromBlue).toString(); from: -200; to: 200; value: editorPreview.channelMixerBlueFromBlue; onValueDragged: function(value) { editorPreview.channelMixerBlueFromBlue = value }; onResetRequested: editorPreview.channelMixerBlueFromBlue = 100 }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Black & White"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            ToolOptionToggle {
                                label: "Enable"
                                checked: editorPreview.blackAndWhiteEnabled
                                helperText: "Enable black and white channel mixing."
                                onToggled: function(checked) { editorPreview.blackAndWhiteEnabled = checked }
                            }
                            InspectorSlider { Layout.fillWidth: true; label: "Reds"; valueText: Math.round(editorPreview.blackAndWhiteReds).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteReds; onValueDragged: function(value) { editorPreview.blackAndWhiteReds = value }; onResetRequested: editorPreview.blackAndWhiteReds = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Oranges"; valueText: Math.round(editorPreview.blackAndWhiteOranges).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteOranges; onValueDragged: function(value) { editorPreview.blackAndWhiteOranges = value }; onResetRequested: editorPreview.blackAndWhiteOranges = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Yellows"; valueText: Math.round(editorPreview.blackAndWhiteYellows).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteYellows; onValueDragged: function(value) { editorPreview.blackAndWhiteYellows = value }; onResetRequested: editorPreview.blackAndWhiteYellows = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Greens"; valueText: Math.round(editorPreview.blackAndWhiteGreens).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteGreens; onValueDragged: function(value) { editorPreview.blackAndWhiteGreens = value }; onResetRequested: editorPreview.blackAndWhiteGreens = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Aquas"; valueText: Math.round(editorPreview.blackAndWhiteAquas).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteAquas; onValueDragged: function(value) { editorPreview.blackAndWhiteAquas = value }; onResetRequested: editorPreview.blackAndWhiteAquas = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Blues"; valueText: Math.round(editorPreview.blackAndWhiteBlues).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteBlues; onValueDragged: function(value) { editorPreview.blackAndWhiteBlues = value }; onResetRequested: editorPreview.blackAndWhiteBlues = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Purples"; valueText: Math.round(editorPreview.blackAndWhitePurples).toString(); from: -100; to: 100; value: editorPreview.blackAndWhitePurples; onValueDragged: function(value) { editorPreview.blackAndWhitePurples = value }; onResetRequested: editorPreview.blackAndWhitePurples = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Magentas"; valueText: Math.round(editorPreview.blackAndWhiteMagentas).toString(); from: -100; to: 100; value: editorPreview.blackAndWhiteMagentas; onValueDragged: function(value) { editorPreview.blackAndWhiteMagentas = value }; onResetRequested: editorPreview.blackAndWhiteMagentas = 0 }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Color Replace"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            ToolOptionToggle {
                                label: "Enable"
                                checked: editorPreview.colorReplacementEnabled
                                helperText: "Enable targeted hue replacement."
                                onToggled: function(checked) { editorPreview.colorReplacementEnabled = checked }
                            }
                            InspectorSlider { Layout.fillWidth: true; label: "Source Hue"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.colorReplacementSourceHue * 360).toString() + "°"; from: 0; to: 1; value: editorPreview.colorReplacementSourceHue; onValueDragged: function(value) { editorPreview.colorReplacementSourceHue = value }; onResetRequested: editorPreview.colorReplacementSourceHue = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Target Hue"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.colorReplacementTargetHue * 360).toString() + "°"; from: 0; to: 1; value: editorPreview.colorReplacementTargetHue; onValueDragged: function(value) { editorPreview.colorReplacementTargetHue = value }; onResetRequested: editorPreview.colorReplacementTargetHue = 0.33 }
                            InspectorSlider { Layout.fillWidth: true; label: "Range"; valueText: Math.round(editorPreview.colorReplacementRange).toString(); from: 1; to: 100; value: editorPreview.colorReplacementRange; onValueDragged: function(value) { editorPreview.colorReplacementRange = value }; onResetRequested: editorPreview.colorReplacementRange = 35 }
                            InspectorSlider { Layout.fillWidth: true; label: "Softness"; valueText: Math.round(editorPreview.colorReplacementSoftness).toString(); from: 0; to: 100; value: editorPreview.colorReplacementSoftness; onValueDragged: function(value) { editorPreview.colorReplacementSoftness = value }; onResetRequested: editorPreview.colorReplacementSoftness = 100 }
                            InspectorSlider { Layout.fillWidth: true; label: "Strength"; valueText: Math.round(editorPreview.colorReplacementStrength).toString(); from: 0; to: 100; value: editorPreview.colorReplacementStrength; onValueDragged: function(value) { editorPreview.colorReplacementStrength = value }; onResetRequested: editorPreview.colorReplacementStrength = 0 }
                            ToolOptionToggle {
                                objectName: "colorReplacementPreserveLuminanceCheckBox"
                                label: "Preserve Luminance"
                                checked: editorPreview.colorReplacementPreserveLuminance
                                helperText: "Keep source luminance while replacing hue."
                                onToggled: function(checked) { editorPreview.colorReplacementPreserveLuminance = checked }
                            }

                            ToolOptionToggle {
                                objectName: "colorReplacementPreserveSaturationCheckBox"
                                label: "Preserve Saturation"
                                checked: editorPreview.colorReplacementPreserveSaturation
                                helperText: "Keep source saturation while replacing hue."
                                onToggled: function(checked) { editorPreview.colorReplacementPreserveSaturation = checked }
                            }

                            ToolOptionToggle {
                                objectName: "colorReplaceProtectSkinCheckBox"
                                label: "Protect Skin"
                                checked: editorPreview.skinToneProtectionEnabled
                                helperText: "Protect skin tones from replacement spill."
                                onToggled: function(checked) { editorPreview.skinToneProtectionEnabled = checked }
                            }
                            InspectorSlider { objectName: "colorReplaceSkinHueSlider"; Layout.fillWidth: true; visible: editorPreview.skinToneProtectionEnabled; label: "Protected Hue"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.skinToneLineAngle).toString() + "°"; from: 0; to: 1; value: editorPreview.skinToneLineHue; onValueDragged: function(value) { editorPreview.skinToneLineHue = value }; onResetRequested: editorPreview.skinToneLineHue = 0.08 }
                            InspectorSlider { objectName: "colorReplaceSkinRangeSlider"; Layout.fillWidth: true; visible: editorPreview.skinToneProtectionEnabled; label: "Hue Range"; valueText: Math.round(editorPreview.skinToneProtectionRange).toString(); from: 1; to: 100; value: editorPreview.skinToneProtectionRange; onValueDragged: function(value) { editorPreview.skinToneProtectionRange = value }; onResetRequested: editorPreview.skinToneProtectionRange = 30 }
                            InspectorSlider { objectName: "colorReplaceSkinProtectionSlider"; Layout.fillWidth: true; visible: editorPreview.skinToneProtectionEnabled; label: "Protection"; valueText: Math.round(editorPreview.skinToneProtectionStrength).toString(); from: 0; to: 100; value: editorPreview.skinToneProtectionStrength; onValueDragged: function(value) { editorPreview.skinToneProtectionStrength = value }; onResetRequested: editorPreview.skinToneProtectionStrength = 0 }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Skin Protection Lab"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            ToolOptionToggle {
                                label: "Protect"
                                checked: editorPreview.skinToneProtectionEnabled
                                helperText: "Enable skin-tone protection."
                                onToggled: function(checked) { editorPreview.skinToneProtectionEnabled = checked }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                color: "transparent"
                                border.color: theme.lineSoft
                                border.width: 1
                                Rectangle { anchors.centerIn: parent; width: parent.width * 0.82; height: 2; color: theme.accentWarm400; rotation: editorPreview.skinToneLineAngle - 20 }
                            }
                            InspectorSlider { Layout.fillWidth: true; label: "Protected Hue"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.skinToneLineAngle).toString() + "°"; from: 0; to: 1; value: editorPreview.skinToneLineHue; onValueDragged: function(value) { editorPreview.skinToneLineHue = value }; onResetRequested: editorPreview.skinToneLineHue = 0.08 }
                            InspectorSlider { Layout.fillWidth: true; label: "Hue Range"; valueText: Math.round(editorPreview.skinToneProtectionRange).toString(); from: 1; to: 100; value: editorPreview.skinToneProtectionRange; onValueDragged: function(value) { editorPreview.skinToneProtectionRange = value }; onResetRequested: editorPreview.skinToneProtectionRange = 30 }
                            InspectorSlider { Layout.fillWidth: true; label: "Protection"; valueText: Math.round(editorPreview.skinToneProtectionStrength).toString(); from: 0; to: 100; value: editorPreview.skinToneProtectionStrength; onValueDragged: function(value) { editorPreview.skinToneProtectionStrength = value }; onResetRequested: editorPreview.skinToneProtectionStrength = 0 }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Harmony Lab"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            ToolOptionToggle {
                                label: "Enable"
                                checked: editorPreview.colorHarmonyEnabled
                                helperText: "Enable harmony palette guidance."
                                onToggled: function(checked) { editorPreview.colorHarmonyEnabled = checked }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: ["Complementary", "Analogous", "Triadic", "Split Complementary", "Tetradic"]
                                    delegate: ColorChannelButton {
                                        required property string modelData
                                        label: modelData === "Split Complementary" ? "Split" : modelData
                                        swatch: theme.accentWarm400
                                        selected: editorPreview.colorHarmonyMode === modelData
                                        onClicked: editorPreview.colorHarmonyMode = modelData
                                    }
                                }
                            }
                            InspectorSlider { Layout.fillWidth: true; label: "Base Hue"; valueInputMode: "degreesNormalized"; valueText: Math.round(editorPreview.colorHarmonyBaseHue * 360).toString() + "°"; from: 0; to: 1; value: editorPreview.colorHarmonyBaseHue; onValueDragged: function(value) { editorPreview.colorHarmonyBaseHue = value }; onResetRequested: editorPreview.colorHarmonyBaseHue = 0 }
                            InspectorSlider { Layout.fillWidth: true; label: "Hue Pull"; valueText: Math.round(editorPreview.colorHarmonyStrength).toString(); from: 0; to: 100; value: editorPreview.colorHarmonyStrength; onValueDragged: function(value) { editorPreview.colorHarmonyStrength = value }; onResetRequested: editorPreview.colorHarmonyStrength = 0 }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Base Adjustments"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Exposure"
                                valueText: editorPreview.exposure.toFixed(2) + " EV"
                                from: -2
                                to: 2
                                value: editorPreview.exposure
                                onValueDragged: function(value) { editorPreview.exposure = value }
                                onResetRequested: editorPreview.exposure = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Contrast"
                                valueText: Math.round(editorPreview.contrast).toString()
                                from: -100
                                to: 100
                                value: editorPreview.contrast
                                onValueDragged: function(value) { editorPreview.contrast = value }
                                onResetRequested: editorPreview.contrast = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Brightness"
                                valueText: Math.round(editorPreview.brightness).toString()
                                from: -100
                                to: 100
                                value: editorPreview.brightness
                                onValueDragged: function(value) { editorPreview.brightness = value }
                                onResetRequested: editorPreview.brightness = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "HDR Local Tone"
                                valueText: Math.round(editorPreview.hdrLocalToneMapping).toString()
                                from: 0
                                to: 100
                                value: editorPreview.hdrLocalToneMapping
                                onValueDragged: function(value) { editorPreview.hdrLocalToneMapping = value }
                                onResetRequested: editorPreview.hdrLocalToneMapping = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Filmic Shoulder"
                                valueText: Math.round(editorPreview.filmicShoulder).toString()
                                from: -100
                                to: 100
                                value: editorPreview.filmicShoulder
                                onValueDragged: function(value) { editorPreview.filmicShoulder = value }
                                onResetRequested: editorPreview.filmicShoulder = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Filmic Toe"
                                valueText: Math.round(editorPreview.filmicToe).toString()
                                from: -100
                                to: 100
                                value: editorPreview.filmicToe
                                onValueDragged: function(value) { editorPreview.filmicToe = value }
                                onResetRequested: editorPreview.filmicToe = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Adaptive Contrast"
                                valueText: Math.round(editorPreview.adaptiveContrast).toString()
                                from: -100
                                to: 100
                                value: editorPreview.adaptiveContrast
                                onValueDragged: function(value) { editorPreview.adaptiveContrast = value }
                                onResetRequested: editorPreview.adaptiveContrast = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Levels Black"
                                valueText: Math.round(editorPreview.levelsBlackPoint).toString()
                                from: 0
                                to: 100
                                value: editorPreview.levelsBlackPoint
                                onValueDragged: function(value) { editorPreview.levelsBlackPoint = value }
                                onResetRequested: editorPreview.levelsBlackPoint = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Levels Gamma"
                                valueText: Math.round(editorPreview.levelsGamma).toString()
                                from: -100
                                to: 100
                                value: editorPreview.levelsGamma
                                onValueDragged: function(value) { editorPreview.levelsGamma = value }
                                onResetRequested: editorPreview.levelsGamma = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Levels White"
                                valueText: Math.round(editorPreview.levelsWhitePoint).toString()
                                from: 0
                                to: 100
                                value: editorPreview.levelsWhitePoint
                                onValueDragged: function(value) { editorPreview.levelsWhitePoint = value }
                                onResetRequested: editorPreview.levelsWhitePoint = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zone Deep Shadows"
                                valueText: Math.round(editorPreview.zoneDeepShadows).toString()
                                from: -100
                                to: 100
                                value: editorPreview.zoneDeepShadows
                                onValueDragged: function(value) { editorPreview.zoneDeepShadows = value }
                                onResetRequested: editorPreview.zoneDeepShadows = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zone Shadows"
                                valueText: Math.round(editorPreview.zoneShadows).toString()
                                from: -100
                                to: 100
                                value: editorPreview.zoneShadows
                                onValueDragged: function(value) { editorPreview.zoneShadows = value }
                                onResetRequested: editorPreview.zoneShadows = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zone Midtones"
                                valueText: Math.round(editorPreview.zoneMidtones).toString()
                                from: -100
                                to: 100
                                value: editorPreview.zoneMidtones
                                onValueDragged: function(value) { editorPreview.zoneMidtones = value }
                                onResetRequested: editorPreview.zoneMidtones = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zone Lights"
                                valueText: Math.round(editorPreview.zoneLights).toString()
                                from: -100
                                to: 100
                                value: editorPreview.zoneLights
                                onValueDragged: function(value) { editorPreview.zoneLights = value }
                                onResetRequested: editorPreview.zoneLights = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zone Highlights"
                                valueText: Math.round(editorPreview.zoneHighlights).toString()
                                from: -100
                                to: 100
                                value: editorPreview.zoneHighlights
                                onValueDragged: function(value) { editorPreview.zoneHighlights = value }
                                onResetRequested: editorPreview.zoneHighlights = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Highlights"
                                valueText: Math.round(editorPreview.highlights).toString()
                                from: -100
                                to: 100
                                value: editorPreview.highlights
                                onValueDragged: function(value) { editorPreview.highlights = value }
                                onResetRequested: editorPreview.highlights = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadows"
                                valueText: Math.round(editorPreview.shadows).toString()
                                from: -100
                                to: 100
                                value: editorPreview.shadows
                                onValueDragged: function(value) { editorPreview.shadows = value }
                                onResetRequested: editorPreview.shadows = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Whites"
                                valueText: Math.round(editorPreview.whites).toString()
                                from: -100
                                to: 100
                                value: editorPreview.whites
                                onValueDragged: function(value) { editorPreview.whites = value }
                                onResetRequested: editorPreview.whites = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Blacks"
                                valueText: Math.round(editorPreview.blacks).toString()
                                from: -100
                                to: 100
                                value: editorPreview.blacks
                                onValueDragged: function(value) { editorPreview.blacks = value }
                                onResetRequested: editorPreview.blacks = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Tone Curve"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            ToneCurveEditor {
                                Layout.fillWidth: true
                                accent: editorPreview.curveChannel === "Luma"
                                        ? "#d8dce0"
                                        : (editorPreview.curveChannel === "Red"
                                        ? "#d45a62"
                                        : (editorPreview.curveChannel === "Green"
                                           ? "#61b56f"
                                           : (editorPreview.curveChannel === "Blue"
                                              ? "#4e86d9"
                                              : "#c99a52")))
                                selectedChannel: editorPreview.curveChannel
                                curveMode: editorPreview.activeCurveMode
                                pointsModel: editorPreview.activeCurvePoints
                                presetNames: editorPreview.curvePresetNames
                                histogramRedData: editorPreview.histogramRed
                                histogramGreenData: editorPreview.histogramGreen
                                histogramBlueData: editorPreview.histogramBlue
                                histogramLuminanceData: editorPreview.histogramLuminance
                                histogramLogScale: histogramLogScale
                                onChannelSelected: editorPreview.curveChannel = channel
                                onPointsEdited: editorPreview.activeCurvePoints = points
                                onPresetSelected: function(preset) { editorPreview.applyCurvePreset(preset) }
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Color"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Item { Layout.fillWidth: true }

                                ActionButton {
                                    compact: true
                                    iconName: "eyedropper"
                                    showText: false
                                    tooltipText: editorPreview.whiteBalanceSampling ? "Cancel white balance picker" : "Sample white balance from image"
                                    active: editorPreview.whiteBalanceSampling
                                    enabled: editorPreview.hasAsset && !editorPreview.compareMode
                                    onClicked: editorPreview.whiteBalanceSampling = !editorPreview.whiteBalanceSampling
                                }
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Temperature"
                                valueText: Math.round(editorPreview.temperature).toString()
                                trackColors: ["#4c78cb", "#c8d0d8", "#e6a14f"]
                                from: -100
                                to: 100
                                value: editorPreview.temperature
                                onValueDragged: function(value) { editorPreview.temperature = value }
                                onResetRequested: editorPreview.temperature = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Tint"
                                valueText: Math.round(editorPreview.tint).toString()
                                trackColors: ["#4ea56b", "#c8d0d8", "#bb67d6"]
                                from: -100
                                to: 100
                                value: editorPreview.tint
                                onValueDragged: function(value) { editorPreview.tint = value }
                                onResetRequested: editorPreview.tint = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Vibrance"
                                valueText: Math.round(editorPreview.vibrance).toString()
                                from: -100
                                to: 100
                                value: editorPreview.vibrance
                                onValueDragged: function(value) { editorPreview.vibrance = value }
                                onResetRequested: editorPreview.vibrance = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.saturation).toString()
                                from: -100
                                to: 100
                                value: editorPreview.saturation
                                onValueDragged: function(value) { editorPreview.saturation = value }
                                onResetRequested: editorPreview.saturation = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Presence"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Texture"
                                valueText: Math.round(editorPreview.texture).toString()
                                from: -100
                                to: 100
                                value: editorPreview.texture
                                onValueDragged: function(value) { editorPreview.texture = value }
                                onResetRequested: editorPreview.texture = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Clarity"
                                valueText: Math.round(editorPreview.clarity).toString()
                                from: -100
                                to: 100
                                value: editorPreview.clarity
                                onValueDragged: function(value) { editorPreview.clarity = value }
                                onResetRequested: editorPreview.clarity = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Dehaze"
                                valueText: Math.round(editorPreview.dehaze).toString()
                                from: -100
                                to: 100
                                value: editorPreview.dehaze
                                onValueDragged: function(value) { editorPreview.dehaze = value }
                                onResetRequested: editorPreview.dehaze = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Detail"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: false

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Sharpening"
                                valueText: Math.round(editorPreview.sharpening).toString()
                                from: 0
                                to: 100
                                value: editorPreview.sharpening
                                onValueDragged: function(value) { editorPreview.sharpening = value }
                                onResetRequested: editorPreview.sharpening = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Masking"
                                valueText: Math.round(editorPreview.sharpenMasking).toString()
                                from: 0
                                to: 100
                                value: editorPreview.sharpenMasking
                                onValueDragged: function(value) { editorPreview.sharpenMasking = value }
                                onResetRequested: editorPreview.sharpenMasking = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luma NR"
                                valueText: Math.round(editorPreview.luminanceNr).toString()
                                from: 0
                                to: 100
                                value: editorPreview.luminanceNr
                                onValueDragged: function(value) { editorPreview.luminanceNr = value }
                                onResetRequested: editorPreview.luminanceNr = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Color NR"
                                valueText: Math.round(editorPreview.colorNr).toString()
                                from: 0
                                to: 100
                                value: editorPreview.colorNr
                                onValueDragged: function(value) { editorPreview.colorNr = value }
                                onResetRequested: editorPreview.colorNr = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Optics"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: false

                            Label {
                                visible: editorPreview.lensModel.length > 0
                                text: "Lens: " + editorPreview.lensModel
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Distortion"
                                valueText: Math.round(editorPreview.lensDistortion).toString()
                                trackColors: ["#5d6a77", "#c7d0d7", "#d0a157"]
                                from: -100
                                to: 100
                                value: editorPreview.lensDistortion
                                onValueDragged: function(value) { editorPreview.lensDistortion = value }
                                onResetRequested: editorPreview.lensDistortion = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Edge Light"
                                valueText: Math.round(editorPreview.lensVignette).toString()
                                trackColors: ["#13181d", "#7c8792", "#f1f5f8"]
                                from: -100
                                to: 100
                                value: editorPreview.lensVignette
                                onValueDragged: function(value) { editorPreview.lensVignette = value }
                                onResetRequested: editorPreview.lensVignette = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Defringe"
                                valueText: Math.round(editorPreview.chromaticAberration).toString()
                                trackColors: ["#4d6074", "#d7dde2", "#d0a157"]
                                from: 0
                                to: 100
                                value: editorPreview.chromaticAberration
                                onValueDragged: function(value) { editorPreview.chromaticAberration = value }
                                onResetRequested: editorPreview.chromaticAberration = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Color Mixer"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Repeater {
                                    model: [
                                        { name: "HSL", color: "#d8dce0" },
                                        { name: "HSV", color: "#49b8c4" },
                                        { name: "LCH", color: "#c99a52" },
                                        { name: "OKLab", color: "#8b6be3" }
                                    ]

                                    delegate: ColorChannelButton {
                                        required property var modelData

                                        label: modelData.name
                                        swatch: modelData.color
                                        selected: editorPreview.colorMixerModel === modelData.name
                                        onClicked: editorPreview.colorMixerModel = modelData.name
                                    }
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Repeater {
                                    model: hslChannels

                                    delegate: ColorChannelButton {
                                        required property var modelData

                                        label: modelData.name
                                        swatch: modelData.color
                                        selected: editorPreview.hslChannel === modelData.name
                                        onClicked: editorPreview.hslChannel = modelData.name
                                    }
                                }
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.hueAdjustment).toString()
                                trackColors: ["#d54f60", "#d78c38", "#d7bf43", "#61b56f", "#49b8c4", "#4e86d9", "#8b6be3", "#c85ac1"]
                                from: -100
                                to: 100
                                value: editorPreview.hueAdjustment
                                onValueDragged: function(value) { editorPreview.hueAdjustment = value }
                                onResetRequested: editorPreview.hueAdjustment = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.saturationAdjustment).toString()
                                trackColors: ["#505963", selectedHslColor(), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.saturationAdjustment
                                onValueDragged: function(value) { editorPreview.saturationAdjustment = value }
                                onResetRequested: editorPreview.saturationAdjustment = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.luminanceAdjustment).toString()
                                trackColors: ["#101419", selectedHslColor(), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.luminanceAdjustment
                                onValueDragged: function(value) { editorPreview.luminanceAdjustment = value }
                                onResetRequested: editorPreview.luminanceAdjustment = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Cyan"
                                valueText: Math.round(editorPreview.selectiveCyan).toString()
                                trackColors: [selectedHslColor(), "#49b8c4"]
                                from: -100
                                to: 100
                                value: editorPreview.selectiveCyan
                                onValueDragged: function(value) { editorPreview.selectiveCyan = value }
                                onResetRequested: editorPreview.selectiveCyan = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Magenta"
                                valueText: Math.round(editorPreview.selectiveMagenta).toString()
                                trackColors: [selectedHslColor(), "#c85ac1"]
                                from: -100
                                to: 100
                                value: editorPreview.selectiveMagenta
                                onValueDragged: function(value) { editorPreview.selectiveMagenta = value }
                                onResetRequested: editorPreview.selectiveMagenta = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Yellow"
                                valueText: Math.round(editorPreview.selectiveYellow).toString()
                                trackColors: [selectedHslColor(), "#d7bf43"]
                                from: -100
                                to: 100
                                value: editorPreview.selectiveYellow
                                onValueDragged: function(value) { editorPreview.selectiveYellow = value }
                                onResetRequested: editorPreview.selectiveYellow = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Black"
                                valueText: Math.round(editorPreview.selectiveBlack).toString()
                                trackColors: ["#eff3f6", selectedHslColor(), "#101419"]
                                from: -100
                                to: 100
                                value: editorPreview.selectiveBlack
                                onValueDragged: function(value) { editorPreview.selectiveBlack = value }
                                onResetRequested: editorPreview.selectiveBlack = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: false && inspectorMode === "adjust"
                            title: "Color Grading"
                            showAccentMarker: false
                            uppercaseTitle: true
                            titleFontFamily: theme.fontMono
                            titlePixelSize: 11
                            titleWeight: Font.DemiBold
                            titleLetterSpacing: 0.9
                            expandable: true
                            expanded: true

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Shadows"
                                showAccentMarker: false
                                titlePixelSize: 9
                                titleWeight: Font.DemiBold
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueInputMode: "degreesNormalized"
                                valueText: Math.round(editorPreview.gradingShadowsHue * 360) + "\u00B0"
                                trackColors: rainbowTrack()
                                from: 0
                                to: 1
                                value: editorPreview.gradingShadowsHue
                                onValueDragged: function(value) { editorPreview.gradingShadowsHue = value }
                                onResetRequested: editorPreview.gradingShadowsHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.gradingShadowsSaturation).toString()
                                trackColors: ["#505963", gradingPreviewColor(editorPreview.gradingShadowsHue, 100, editorPreview.gradingShadowsLuminance), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingShadowsSaturation
                                onValueDragged: function(value) { editorPreview.gradingShadowsSaturation = value }
                                onResetRequested: editorPreview.gradingShadowsSaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.gradingShadowsLuminance).toString()
                                trackColors: ["#101419", gradingPreviewColor(editorPreview.gradingShadowsHue, editorPreview.gradingShadowsSaturation, 0), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingShadowsLuminance
                                onValueDragged: function(value) { editorPreview.gradingShadowsLuminance = value }
                                onResetRequested: editorPreview.gradingShadowsLuminance = 0
                            }

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Midtones"
                                showAccentMarker: false
                                titlePixelSize: 9
                                titleWeight: Font.DemiBold
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueInputMode: "degreesNormalized"
                                valueText: Math.round(editorPreview.gradingMidtonesHue * 360) + "\u00B0"
                                trackColors: rainbowTrack()
                                from: 0
                                to: 1
                                value: editorPreview.gradingMidtonesHue
                                onValueDragged: function(value) { editorPreview.gradingMidtonesHue = value }
                                onResetRequested: editorPreview.gradingMidtonesHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.gradingMidtonesSaturation).toString()
                                trackColors: ["#505963", gradingPreviewColor(editorPreview.gradingMidtonesHue, 100, editorPreview.gradingMidtonesLuminance), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingMidtonesSaturation
                                onValueDragged: function(value) { editorPreview.gradingMidtonesSaturation = value }
                                onResetRequested: editorPreview.gradingMidtonesSaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.gradingMidtonesLuminance).toString()
                                trackColors: ["#101419", gradingPreviewColor(editorPreview.gradingMidtonesHue, editorPreview.gradingMidtonesSaturation, 0), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingMidtonesLuminance
                                onValueDragged: function(value) { editorPreview.gradingMidtonesLuminance = value }
                                onResetRequested: editorPreview.gradingMidtonesLuminance = 0
                            }

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Highlights"
                                showAccentMarker: false
                                titlePixelSize: 9
                                titleWeight: Font.DemiBold
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueInputMode: "degreesNormalized"
                                valueText: Math.round(editorPreview.gradingHighlightsHue * 360) + "\u00B0"
                                trackColors: rainbowTrack()
                                from: 0
                                to: 1
                                value: editorPreview.gradingHighlightsHue
                                onValueDragged: function(value) { editorPreview.gradingHighlightsHue = value }
                                onResetRequested: editorPreview.gradingHighlightsHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.gradingHighlightsSaturation).toString()
                                trackColors: ["#505963", gradingPreviewColor(editorPreview.gradingHighlightsHue, 100, editorPreview.gradingHighlightsLuminance), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingHighlightsSaturation
                                onValueDragged: function(value) { editorPreview.gradingHighlightsSaturation = value }
                                onResetRequested: editorPreview.gradingHighlightsSaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.gradingHighlightsLuminance).toString()
                                trackColors: ["#101419", gradingPreviewColor(editorPreview.gradingHighlightsHue, editorPreview.gradingHighlightsSaturation, 0), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingHighlightsLuminance
                                onValueDragged: function(value) { editorPreview.gradingHighlightsLuminance = value }
                                onResetRequested: editorPreview.gradingHighlightsLuminance = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "crop"
                            title: "Crop"
                            subtitle: "Ratio, guides, and crop zoom."
                            expandable: true
                            expanded: true

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                ActionButton {
                                    Layout.fillWidth: true
                                    text: editorPreview.cropEditing ? "Done" : "Edit Crop"
                                    active: editorPreview.cropEditing
                                    onClicked: editorPreview.cropEditing = !editorPreview.cropEditing
                                }

                                ActionButton {
                                    text: "Reset Box"
                                    enabled: editorPreview.hasAsset
                                    onClicked: editorPreview.resetCropRect()
                                }
                            }

                            Label {
                                text: "Ratio"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            ToneComboBox {
                                Layout.fillWidth: true
                                model: editorPreview.cropAspectPresets()
                                currentIndex: model.indexOf(editorPreview.cropPreset)
                                onActivated: editorPreview.cropPreset = currentText
                            }

                            Label {
                                text: "Guides"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            ToneComboBox {
                                Layout.fillWidth: true
                                model: ["Off", "Thirds", "Center", "Diagonal", "Golden Ratio", "Golden Spiral"]
                                currentIndex: model.indexOf(cropGuideMode)
                                onActivated: cropGuideMode = currentText
                            }

                            Label {
                                text: "Upright"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            ToneComboBox {
                                Layout.fillWidth: true
                                model: ["Off", "Vertical", "Full", "Guided"]
                                currentIndex: model.indexOf(editorPreview.uprightMode)
                                onActivated: editorPreview.uprightMode = currentText
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Vertical Guide"
                                valueText: Math.round(editorPreview.uprightVerticalGuideAngle).toString() + " deg"
                                from: -45
                                to: 45
                                value: editorPreview.uprightVerticalGuideAngle
                                onValueDragged: function(value) { editorPreview.uprightVerticalGuideAngle = value }
                                onResetRequested: editorPreview.uprightVerticalGuideAngle = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Horizontal Guide"
                                valueText: Math.round(editorPreview.uprightHorizontalGuideAngle).toString() + " deg"
                                from: -45
                                to: 45
                                value: editorPreview.uprightHorizontalGuideAngle
                                onValueDragged: function(value) { editorPreview.uprightHorizontalGuideAngle = value }
                                onResetRequested: editorPreview.uprightHorizontalGuideAngle = 0
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Straighten Horizon"
                                enabled: editorPreview.hasAsset && Math.abs(editorPreview.uprightHorizontalGuideAngle) > 0.05
                                onClicked: editorPreview.straightenFromHorizonAngle(editorPreview.uprightHorizontalGuideAngle)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Apply Upright"
                                enabled: editorPreview.hasAsset
                                onClicked: editorPreview.applyGuidedUpright()
                            }

                            ToolOptionToggle {
                                label: "Local Crop Edge Fill"
                                checked: editorPreview.contentAwareCropFill
                                helperText: "Fill transparent crop edges with local content-aware pixels."
                                onToggled: function(checked) { editorPreview.contentAwareCropFill = checked }
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zoom"
                                valueText: editorPreview.cropZoom.toFixed(2) + "x"
                                from: 1
                                to: 3
                                stepSize: 0.01
                                value: editorPreview.cropZoom
                                onValueDragged: function(value) { editorPreview.cropZoom = value }
                                onResetRequested: editorPreview.cropZoom = 1
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Rotate"
                                valueText: editorPreview.transformRotation.toFixed(1) + " deg"
                                from: -45
                                to: 45
                                stepSize: 0.1
                                value: editorPreview.transformRotation
                                onValueDragged: function(value) { editorPreview.transformRotation = value }
                                onResetRequested: editorPreview.transformRotation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Scale"
                                valueText: editorPreview.transformScale.toFixed(2) + "x"
                                from: 0.25
                                to: 4
                                stepSize: 0.01
                                value: editorPreview.transformScale
                                onValueDragged: function(value) { editorPreview.transformScale = value }
                                onResetRequested: editorPreview.transformScale = 1
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Vertical"
                                valueText: Math.round(editorPreview.perspectiveVertical).toString()
                                from: -100
                                to: 100
                                value: editorPreview.perspectiveVertical
                                onValueDragged: function(value) { editorPreview.perspectiveVertical = value }
                                onResetRequested: editorPreview.perspectiveVertical = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Horizontal"
                                valueText: Math.round(editorPreview.perspectiveHorizontal).toString()
                                from: -100
                                to: 100
                                value: editorPreview.perspectiveHorizontal
                                onValueDragged: function(value) { editorPreview.perspectiveHorizontal = value }
                                onResetRequested: editorPreview.perspectiveHorizontal = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Skew X"
                                valueText: Math.round(editorPreview.transformSkewX).toString()
                                from: -100
                                to: 100
                                value: editorPreview.transformSkewX
                                onValueDragged: function(value) { editorPreview.transformSkewX = value }
                                onResetRequested: editorPreview.transformSkewX = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Skew Y"
                                valueText: Math.round(editorPreview.transformSkewY).toString()
                                from: -100
                                to: 100
                                value: editorPreview.transformSkewY
                                onValueDragged: function(value) { editorPreview.transformSkewY = value }
                                onResetRequested: editorPreview.transformSkewY = 0
                            }

                            Label {
                                text: "Liquify"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Strength"
                                valueText: Math.round(editorPreview.liquifyStrength).toString()
                                from: -100
                                to: 100
                                value: editorPreview.liquifyStrength
                                onValueDragged: function(value) { editorPreview.liquifyStrength = value }
                                onResetRequested: editorPreview.liquifyStrength = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Radius"
                                valueInputMode: "percentNormalized"
                                valueText: Math.round(editorPreview.liquifyRadius * 100).toString() + "%"
                                from: 0.02
                                to: 1
                                stepSize: 0.01
                                value: editorPreview.liquifyRadius
                                onValueDragged: function(value) { editorPreview.liquifyRadius = value }
                                onResetRequested: editorPreview.liquifyRadius = 0.25
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Center X"
                                valueInputMode: "percentNormalized"
                                valueText: Math.round(editorPreview.liquifyCenterX * 100).toString() + "%"
                                from: 0
                                to: 1
                                stepSize: 0.01
                                value: editorPreview.liquifyCenterX
                                onValueDragged: function(value) { editorPreview.liquifyCenterX = value }
                                onResetRequested: editorPreview.liquifyCenterX = 0.5
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Center Y"
                                valueInputMode: "percentNormalized"
                                valueText: Math.round(editorPreview.liquifyCenterY * 100).toString() + "%"
                                from: 0
                                to: 1
                                stepSize: 0.01
                                value: editorPreview.liquifyCenterY
                                onValueDragged: function(value) { editorPreview.liquifyCenterY = value }
                                onResetRequested: editorPreview.liquifyCenterY = 0.5
                            }
                        }
                    }
                }
            }
        }
    }
}
