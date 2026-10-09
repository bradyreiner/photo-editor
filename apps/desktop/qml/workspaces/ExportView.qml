pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PhotoEditor
import "../components"

Rectangle {
    id: root
    objectName: "exportWorkspace"
    color: "transparent"

    Theme { id: theme }
    Settings {
        id: exportWorkspaceSettings
        category: "PhotoExport"
        property string lastDeliverySettingsJson: ""
        property bool previewVisible: true
        property bool activityVisible: true
    }
    property bool exportSettingsReady: false
    property bool exportPreviewVisible: true
    property bool exportActivityVisible: true
    onExportPreviewVisibleChanged: {
        if (exportSettingsReady) exportWorkspaceSettings.previewVisible = exportPreviewVisible
    }
    onExportActivityVisibleChanged: {
        if (exportSettingsReady) exportWorkspaceSettings.activityVisible = exportActivityVisible
    }

    function exportLayoutMenuItems() {
        return [
            { key: "show-main", label: "Show preview and activity", icon: "grid" },
            { key: "focus-settings", label: "Focus on settings", icon: "panel-right" },
            { key: "preview", label: "Photo preview", icon: "visible", checked: exportPreviewVisible },
            { key: "activity", label: "Delivery activity", icon: "scope", checked: exportActivityVisible }
        ]
    }

    function handleExportLayoutMenu(key) {
        if (key === "show-main") {
            exportPreviewVisible = true
            exportActivityVisible = true
        } else if (key === "focus-settings") {
            exportPreviewVisible = false
            exportActivityVisible = false
        } else if (key === "preview") exportPreviewVisible = !exportPreviewVisible
        else if (key === "activity") exportActivityVisible = !exportActivityVisible
    }

    function comboIndexOfText(combo, value) {
        const requestedText = String(value)
        for (let index = 0; index < combo.count; ++index) {
            if (String(combo.textAt(index)) === requestedText)
                return index
        }
        return -1
    }

    Timer {
        id: saveDeliverySettingsTimer
        interval: 180
        repeat: false
        onTriggered: {
            if (!root.exportSettingsReady)
                return
            exportWorkspaceSettings.lastDeliverySettingsJson = JSON.stringify({
                preset: presetCombo.currentText,
                jpegQuality: jpegQualityField.value,
                maxEdge: maxEdgeField.value,
                sharpening: sharpeningCombo.currentText,
                colorSpace: colorSpaceCombo.currentText,
                crop: cropConstraintCombo.currentText,
                destination: destinationField.text,
                filenameTemplate: filenameTemplateField.text
            })
        }
    }

    function scheduleDeliverySettingsSave() {
        if (exportSettingsReady)
            saveDeliverySettingsTimer.restart()
    }

    readonly property bool historyMutationsEnabled: jobQueue.historyMutationsEnabled
    readonly property string recoveryMutationReason: "Resolve job recovery before changing history. Both versions are preserved."
    readonly property var unconfirmedExports: exportJobs.jobs.filter(function(job) {
        return job.status === "Saving" || job.status === "Save unconfirmed"
    })

    readonly property bool hasSingleSelection: catalogAssets.selectedAssetId.length > 0
                                               && catalogAssets.selectedFilePath.length > 0
    readonly property bool hasBatchSelection: catalogAssets.selectedAssetCount > 1
    readonly property string exportVisibilityContract: "export-visibility:v2|always:config-panel-selection-preset-destination-primary-action-layout-menu|configurable:preview-queue-panel|contextual:preflight-batch-feedback-empty-queue-progress-errors|one-expansion:export-options-queue-management-destination-reset-clear-finished|hover-focus:queue-row-actions-remove-retry-cancel|diagnostic:job-errors-preflight-blockers-active-progress|preserve:functionality"
    readonly property string exportSurfaceContract: "export-surfaces:v1|queue:jobs-and-progress|config:selection-preset-color-crop-destination|preflight:blockers-visible|primary-action:split-export|queue-management:menu|no-elide:true|no-overlap:true"
    readonly property string exportActionContract: "export-actions:v3|split-button:true|primary:start-current-or-selected|clear:queue-management-only|menu:queue-current,queue-selected,browse-destination,reset-destination|cleanup-menu:clear-finished|disabled-reasons:true|preflight-aware:true|no-clutter:true|label-intent:start-export"
    readonly property string exportActionHierarchyContract: "export-action-hierarchy:v3|primary:start-export-split|primary-menu-clean:true|secondary:queue-management-menu|clear:secondary-cleanup|retry:row-or-queue-menu|destination:secondary-menu|one-primary:true|clear-not-primary:true"
    readonly property string exportWorkflowPolishContract: "export-workflow-polish:v1|first-glance:presets-selection-queue-settings-primary-action|close-inspection:preset-destination-preflight-progress-errors-output-summary|clutter:secondary-management-in-menus-row-actions-contextual|interaction:select-preset-destination-add-queue-start-retry-cancel-clear-reveal|text:no-elide-no-overlap|functionality:preserved"
    readonly property string exportPageCompletionContract: "page-completion:v1|export|sidebar:presets-destinations|content:selected-assets-queue-progress|settings:preset-color-crop-destination-preflight|toolbar:add-assets-start-export-clear-queue|states:empty-ready-warning-blocked-running-complete-failed"
    readonly property string deliveryExportContract: "delivery-export:v1|preflight:destination-filenames-collisions-missing-unsupported-unavailable|progress:per-item-overall-nonblocking|actions:pause-resume-cancel-retry-failed-reveal|summary:profile-dimensions-format-metadata-sharpening|collision:numbered-preserve-existing"
    readonly property string persistentRenderQueueContract: "persistent-render-queue:v1|jobs:asset-export-and-timeline-render|storage:job-store|ordering:priority-then-manual-order|actions:move-priority-pause-resume-cancel-retry-duplicate-reveal|recovery:explicit-confirmation|ui:progressive-row-menu"
    readonly property string backgroundExportContract: "background-export:v1|editing:concurrent|policies:playback-first-balanced-export-first|resources:thread-limit-and-os-priority-only|preview-quality:unchanged|output-settings:unchanged|default:playback-first|ui:progressive-row-menu"
    readonly property string offlineRecoveryContract: "offline-recovery:v1|surface:export|states:drive-offline,moved-file,permission-denied,permanently-missing|preserve:metadata-thumbnail-recipe-identity|actions:recheck-relink-selected-relink-all-mark-missing|default:relink-not-delete|blocked-jobs:auto-resume-after-relink"
    property bool offlineMissingConfirmationArmed: false
    property alias deliveryDestinationPath: destinationField.text
    property alias deliveryPresetIndex: presetCombo.currentIndex

    signal presetManagementRequested(string action)
    signal assetActivated(string destination)

    readonly property var selectionMetrics: {
        catalogAssets.selectedAssetIds
        catalogAssets.assetCount
        let metrics = { count: 0, photos: 0, videos: 0, timelines: 0 }
        for (let index = 0; index < catalogAssets.assetCount; ++index) {
            const asset = catalogAssets.assetAt(index)
            const assetId = String(asset.assetId || "")
            if (assetId.length === 0 || !catalogAssets.assetSelected(assetId))
                continue
            metrics.count += 1
            const mediaType = String(asset.mediaType || "photo")
            if (mediaType === "timeline")
                metrics.timelines += 1
            else if (mediaType === "video")
                metrics.videos += 1
            else
                metrics.photos += 1
        }
        return metrics
    }

    readonly property var presetDetails: [
        {
            name: "Full Resolution",
            format: "PNG",
            bitDepth: "16-bit",
            maxEdge: "Full size",
            quality: "Lossless",
            description: "Maximum quality lossless export at full original dimensions. Best for archival, retouching handoff, and master files."
        },
        {
            name: "Archive TIFF",
            format: "TIFF",
            bitDepth: "16-bit",
            maxEdge: "Full size",
            quality: "Lossless",
            description: "Industry-standard lossless format for print production, long-term archival, and professional delivery."
        },
        {
            name: "OpenEXR Half",
            format: "OpenEXR",
            bitDepth: "16-bit half float",
            maxEdge: "Full size",
            quality: "Lossless ZIP",
            description: "Scene-linear working-space master with explicit chromaticities, extended range, alpha, and transform provenance for professional interchange."
        },
        {
            name: "ACES2065-1 OpenEXR",
            format: "OpenEXR",
            bitDepth: "16-bit half float",
            maxEdge: "Full size",
            quality: "Lossless ZIP",
            description: "Scene-linear ACES2065-1/AP0 interchange with an explicit working-to-ACES transform, D60/AP0 chromaticities, alpha, and transform provenance."
        },
        {
            name: "ACEScg OpenEXR",
            format: "OpenEXR",
            bitDepth: "16-bit half float",
            maxEdge: "Full size",
            quality: "Lossless ZIP",
            description: "Scene-linear ACEScg/AP1 interchange with an explicit working-to-ACES transform, D60/AP1 chromaticities, alpha, and transform provenance."
        },
        {
            name: "HDR PQ AVIF",
            format: "AVIF",
            bitDepth: "10-bit PQ",
            maxEdge: "Full size",
            quality: "Professional",
            description: "Rec.2100 PQ delivery mastered to 1000 nits with 203-nit reference white, nclx colorimetry, MaxCLL/MaxFALL, and mastering-display metadata."
        },
        {
            name: "HDR HLG AVIF",
            format: "AVIF",
            bitDepth: "10-bit HLG",
            maxEdge: "Full size",
            quality: "Professional",
            description: "Rec.2100 HLG delivery mastered to 1000 nits with explicit colorimetry and mastering metadata for broadcast-compatible HDR workflows."
        },
        {
            name: "Web Proof",
            format: "JPEG",
            bitDepth: "8-bit",
            maxEdge: "2560 px",
            quality: "88%",
            description: "High-quality web delivery for portfolios, galleries, and client-facing presentations."
        },
        {
            name: "Client Preview",
            format: "JPEG",
            bitDepth: "8-bit",
            maxEdge: "2048 px",
            quality: "86%",
            description: "Balanced quality and file size for client review, proofing, and approval workflows."
        },
        {
            name: "Quick Web",
            format: "JPEG",
            bitDepth: "8-bit",
            maxEdge: "1440 px",
            quality: "82%",
            description: "Optimized for fast web delivery, social media, and email. Smaller file size with good visual quality."
        },
        {
            name: "Original Copy",
            format: "Source",
            bitDepth: "—",
            maxEdge: "—",
            quality: "—",
            description: "Copies the original source file without any processing. No edits, crops, or adjustments are applied."
        }
    ]

    readonly property var colorSpaceOptions: [
        "From Edit Recipe",
        "sRGB",
        "Display P3",
        "Adobe RGB"
    ]

    function fileNameFromPath(path) {
        if (path.length === 0)
            return "";
        const parts = path.split("/");
        return parts.length > 0 ? parts[parts.length - 1] : path;
    }

    function baseNameFromPath(path) {
        const fileName = fileNameFromPath(path);
        const dotIndex = fileName.lastIndexOf(".");
        return dotIndex > 0 ? fileName.slice(0, dotIndex) : fileName;
    }

    function currentSelectionLabel() {
        if (!hasSingleSelection)
            return "No current selection";
        const baseName = baseNameFromPath(catalogAssets.selectedFilePath);
        return baseName.length > 0 ? baseName : catalogAssets.selectedAssetId;
    }

    function filenameForAsset(name, sequence) {
        const template = String(filenameTemplateField.text || "{name}").trim() || "{name}"
        return template.replace(/\{name\}/gi, String(name || "Photo"))
                       .replace(/\{sequence\}/gi, String(sequence).padStart(3, "0"))
    }

    function filenamePreviewText() {
        const preflight = currentDeliveryPreflight()
        if (hasBatchSelection) {
            const items = preflight.items || []
            if (items.length > 0)
                return "Preview: " + fileNameFromPath(String(items[0].outputPath || "Pending")) +
                       (items.length > 1 ? "  •  +" + (items.length - 1) + " more" : "")
        }
        return "Preview: " + fileNameFromPath(
                   String(preflight.outputPath || "Choose a photo and destination"))
    }

    function selectionSummaryText() {
        const m = selectionMetrics
        if (m.count === 0)
            return hasSingleSelection
                   ? "Current photo · Select multiple in Library for a batch"
                   : "Choose photos in Library to export"
        let parts = []
        if (m.photos > 0)
            parts.push(m.photos + (m.photos === 1 ? " photo" : " photos"))
        if (m.videos > 0)
            parts.push(m.videos + (m.videos === 1 ? " video" : " videos"))
        if (m.timelines > 0)
            parts.push(m.timelines + (m.timelines === 1 ? " timeline" : " timelines"))
        return parts.join(", ") + " selected"
    }

    function currentPresetDetail() {
        const presetName = presetCombo.currentText
        for (let i = 0; i < presetDetails.length; ++i) {
            if (presetDetails[i].name === presetName) {
                const detail = Object.assign({}, presetDetails[i])
                if (jpegPreset) {
                    detail.quality = jpegQualityField.value + "% JPEG"
                    detail.maxEdge = maxEdgeField.value > 0 ? maxEdgeField.value + " px long edge" : "Full size"
                }
                return detail
            }
        }
        return presetDetails[0]
    }

    readonly property bool jpegPreset: ["Web Proof", "Client Preview", "Quick Web"].indexOf(presetCombo.currentText) >= 0

    function deliveryPreset() {
        if (!jpegPreset)
            return presetCombo.currentText
        return presetCombo.currentText + "|quality=" + jpegQualityField.value + "|max-edge=" + maxEdgeField.value +
               "|sharpen=" + String(sharpeningCombo.currentText || "Off").toLowerCase()
    }

    function overrideAdjustmentColorSpace(adjustmentPayload, colorSpaceChoice) {
        if (colorSpaceChoice === "From Edit Recipe" || colorSpaceChoice.length === 0)
            return adjustmentPayload
        try {
            let patch = adjustmentPayload.length > 0 ? JSON.parse(adjustmentPayload) : {
                format: "photo-editor-adjustment-patch",
                version: 1,
                scope: "asset-bound",
                groups: ["color"],
                adjustmentState: {}
            }
            patch.adjustmentState.outputColorSpace = colorSpaceChoice
            return JSON.stringify(patch)
        } catch (e) {
            return adjustmentPayload
        }
    }

    function jobKindLabel(jobKind) {
        if (jobKind === "catalog_import")
            return "Import";
        if (jobKind === "timeline_render")
            return "Timeline Render";
        return "Export";
    }

    function jobStatusTone(status) {
        if (status === "Completed")
            return "success"
        if (status === "Failed")
            return "error"
        if (status === "Canceling")
            return "warning"
        if (status === "Running")
            return "accent"
        if (status === "Queued")
            return "info"
        return "neutral"
    }

    function jobDestination(outputPath, destinationPath, sourcePath) {
        if (outputPath && outputPath.length > 0)
            return outputPath;
        if (destinationPath && destinationPath.length > 0)
            return destinationPath;
        return sourcePath || "";
    }

    function canRetryJob(status, jobKind) {
        if (!root.historyMutationsEnabled) return false
        return (status === "Failed" || status === "Canceled")
                && (jobKind === "asset_export" || jobKind === "timeline_render");
    }

    function canCancelJob(status, jobKind) {
        if (!root.historyMutationsEnabled) return false
        if (status === "Queued")
            return true;
        if (status === "Running" || status === "Canceling")
            return jobKind === "asset_export" || jobKind === "timeline_render";
        return false;
    }

    function canPauseJob(status, jobKind) {
        if (!root.historyMutationsEnabled) return false
        return (jobKind === "asset_export" || jobKind === "timeline_render")
                && (status === "Queued" || status === "Running")
    }

    function canResumeJob(status, jobKind) {
        if (!root.historyMutationsEnabled) return false
        return (jobKind === "asset_export" || jobKind === "timeline_render")
                && status === "Paused"
    }

    function canRevealJob(status, jobKind, outputPath) {
        return (jobKind === "asset_export" || jobKind === "timeline_render")
                && status === "Completed" && String(outputPath || "").length > 0
    }

    function latestActiveJob() {
        if (jobQueue.activeJobCountForKind("asset_export") > 0)
            return jobQueue.latestJobForKind("asset_export")
        if (jobQueue.activeJobCountForKind("timeline_render") > 0)
            return jobQueue.latestJobForKind("timeline_render")
        if (jobQueue.activeJobCountForKind("catalog_import") > 0)
            return jobQueue.latestJobForKind("catalog_import")
        return ({})
    }

    function activeJobProgress() {
        const job = latestActiveJob()
        const status = job.status ? String(job.status) : ""
        const raw = job.progress !== undefined ? Number(job.progress) : 0
        if (status === "Queued")
            return 0.08
        if (raw > 0)
            return Math.max(0.05, Math.min(1, raw / 100.0))
        return -1
    }

    function buildSelectedAssetList() {
        let assets = []
        const colorSpace = colorSpaceCombo.currentText
        for (let index = 0; index < catalogAssets.assetCount; ++index) {
            const asset = catalogAssets.assetAt(index)
            const assetId = String(asset.assetId || "")
            if (assetId.length === 0 || !catalogAssets.assetSelected(assetId))
                continue
            const sourcePath = String(asset.filePath || "")
            if (sourcePath.length === 0)
                continue
            assets.push({
                assetId: assetId,
                mediaType: String(asset.mediaType || "photo"),
                filePath: sourcePath,
                adjustmentPatchJson: overrideAdjustmentColorSpace(String(asset.adjustmentPatchJson || ""), colorSpace),
                displayLabel: root.filenameForAsset(root.baseNameFromPath(sourcePath), assets.length + 1)
            })
        }
        return assets
    }

    function currentSubmissionAssetList() {
        if (!hasSingleSelection)
            return []
        return [{
            assetId: catalogAssets.selectedAssetId,
            mediaType: catalogAssets.selectedMediaType,
            filePath: catalogAssets.selectedFilePath,
            editedPreviewSource: editorPreview.assetId === catalogAssets.selectedAssetId
                                 ? editorPreview.previewSource : "",
            adjustmentPatchJson: overrideAdjustmentColorSpace(
                catalogAssets.selectedAdjustmentPatchJson, colorSpaceCombo.currentText),
            displayLabel: filenameForAsset(currentSelectionLabel(), 1)
        }]
    }

    function currentExportActionAvailable() {
        if (!hasSingleSelection || destinationField.text.trim().length === 0 ||
                exportJobs.batchSubmissionBusy)
            return false
        const report = exportJobs.singlePreflight
        // The visible review may take longer than the user wants to wait. The
        // submission path does its own fresh checks before admitting a job.
        return exportJobs.preflightBusy || report.ok === undefined ||
               Number(report.errorCount || 0) === 0
    }

    function queueBatchExports() {
        if (exportJobs.batchSubmissionBusy || exportJobs.preflightBusy || currentBatchPreflight().readyCount <= 0)
            return
        const assets = buildSelectedAssetList()
        if (assets.length === 0)
            return
        const cropPreset = cropConstraintCombo.currentText === "Current" ? "" : cropConstraintCombo.currentText
        const accepted = exportJobs.queueBatchExportAsync(
            assets,
            root.deliveryPreset(),
            destinationField.text.trim(),
            cropPreset,
            0,
            0,
            0,
            0)
        if (accepted) {
            batchQueuedFeedback.text = "Checking " + assets.length + " selected exports"
            batchQueuedFeedback.visible = true
            batchQueuedTimer.restart()
        }
    }

    function queueCurrentSelection() {
        if (!currentExportActionAvailable())
            return
        const assets = currentSubmissionAssetList()
        if (assets.length === 0)
            return
        const cropPreset = cropConstraintCombo.currentText === "Current" ? "" : cropConstraintCombo.currentText
        const accepted = exportJobs.queueBatchExportAsync(
            assets, root.deliveryPreset(), destinationField.text.trim(), cropPreset, 0, 0, 0, 0)
        if (accepted) {
            batchQueuedFeedback.text = "Checking current export"
            batchQueuedFeedback.visible = true
            batchQueuedTimer.restart()
        }
    }

    // Build each input snapshot once per relevant state change. Scope updates
    // do not rebuild the selected asset list or repeat source/profile reads.
    property int preflightCatalogRevision: 0
    readonly property var selectedDeliveryAssets: {
        catalogAssets.selectedAssetIds
        catalogAssets.assetCount
        preflightCatalogRevision
        return visible && catalogAssets.selectedAssetCount > 0 ? buildSelectedAssetList() : []
    }
    readonly property var singleDeliveryAsset: ({
        mediaType: catalogAssets.selectedMediaType || "photo",
        filePath: catalogAssets.selectedFilePath || "",
        editedPreviewSource: editorPreview.previewSource || "",
        adjustmentPatchJson: overrideAdjustmentColorSpace(
            catalogAssets.selectedAdjustmentPatchJson || "", colorSpaceCombo.currentText)
    })
    readonly property var exportPreflightInput: ({
        single: singleDeliveryAsset,
        assets: selectedDeliveryAssets,
        preset: deliveryPreset(),
        destination: destinationField.text.trim(),
        scopes: {
            histogramSamples: editorPreview.histogramSampleCount,
            shadowClip: editorPreview.histogramShadowClipCount,
            highlightClip: editorPreview.histogramHighlightClipCount,
            rawSamples: editorPreview.rawClipSampleCount,
            rawShadowClip: editorPreview.rawShadowClipCount,
            rawHighlightClip: editorPreview.rawHighlightClipCount
        }
    })
    function refreshExportPreflight(force) {
        if (visible && exportSettingsReady && !exportJobs.batchSubmissionBusy)
            exportJobs.requestPreflight(exportPreflightInput, Boolean(force))
    }
    onExportPreflightInputChanged: refreshExportPreflight(false)
    onVisibleChanged: {
        if (visible)
            refreshExportPreflight(true)
        else
            exportJobs.cancelPreflight()
    }
    Component.onDestruction: exportJobs.cancelPreflight()
    Timer {
        interval: 5000
        repeat: true
        running: root.visible && root.exportSettingsReady
        onTriggered: {
            if (!exportJobs.preflightBusy)
                root.refreshExportPreflight(true)
        }
    }
    Connections {
        target: catalogAssets
        function onPhotoRecipeChanged() { root.preflightCatalogRevision += 1 }
        function onSearchInputsChanged() { root.preflightCatalogRevision += 1 }
    }
    Connections {
        target: exportJobs
        function onBatchSubmissionFinished(queued, requested) {
            const submissionError = String(exportJobs.submissionError || "").trim()
            batchQueuedFeedback.text = queued > 0
                    ? "Saving " + queued + " of " + requested + " export jobs"
                    : submissionError.length > 0
                      ? submissionError
                      : "No export could be queued. Review the output details."
            batchQueuedFeedback.visible = true
            batchQueuedTimer.restart()
            root.refreshExportPreflight(true)
        }
    }
    function currentPreflight() {
        const report = exportJobs.singlePreflight
        return report.ok !== undefined ? report : ({ok: false, errorCount: 1, warningCount: 0,
            format: "", outputColorSpace: "", issues: []})
    }
    function currentBatchPreflight() {
        const report = exportJobs.batchPreflight
        return report.ok !== undefined ? report : ({ok: false, readyCount: 0, blockedCount: 0,
            assetCount: 0, preset: deliveryPreset(), items: []})
    }

    function currentDeliveryPreflight() {
        return hasBatchSelection ? currentBatchPreflight() : currentPreflight()
    }

    function exportWorkflowPolishSignature() {
        const preflight = currentPreflight()
        const queueCount = jobQueue ? Number(jobQueue.jobCount || 0) : 0
        const activeProgress = root.activeJobProgress()
        return "export-workflow-polish:v1" +
               "|selection:" + String(selectionMetrics.count) +
               "|single:" + String(hasSingleSelection) +
               "|preset:" + String(presetCombo.currentText || "") +
               "|destination:" + String(destinationField.text.trim().length > 0) +
               "|preflight-errors:" + String(preflight.errorCount || 0) +
               "|preflight-warnings:" + String(preflight.warningCount || 0) +
               "|queue:" + String(queueCount) +
               "|active-progress:" + String(activeProgress) +
               "|batch-disabled:" + root.exportDisabledReason(true) +
               "|current-disabled:" + root.exportDisabledReason(false) +
               "|feedback:" + String(batchQueuedFeedback.visible) +
               "|hierarchy:" + root.exportActionHierarchySignature() +
               "|preserve:preset-selection-destination-selected-preview-add-more-queue-start-retry-cancel-remove-clear-preflight-output-summary-reveal"
    }

    function activeVisibilityContract() {
        return exportVisibilityContract
    }

    function exportActionHierarchySignature() {
        const queueItems = root.queueManagementMenuItems()
        const actionItems = root.exportActionMenuItems()
        return "export-action-hierarchy:v3" +
               "|primary-visible:" + String(hasSingleSelection || hasBatchSelection) +
               "|primary:start-export-split" +
               "|primary-menu-clean:" + String(actionItems.map(function(item) { return String(item.key || "") }).indexOf("clear-finished") < 0) +
               "|batch-split:" + String(hasBatchSelection) +
               "|current-split:" + String(hasSingleSelection && !hasBatchSelection) +
               "|secondary:queue-management-menu" +
               "|queue-menu:" + queueItems.map(function(item) { return String(item.key || "") }).join(",") +
               "|action-menu:" + actionItems.map(function(item) { return String(item.key || "") }).join(",") +
               "|clear:secondary-cleanup"
    }

    function exportDisabledReason(batch) {
        if (exportJobs.batchSubmissionBusy)
            return "Checking export request."
        if (batch && exportJobs.preflightBusy)
            return "Checking export settings."
        if (batch) {
            if (selectionMetrics.count === 0)
                return "Select images before queueing a batch export."
        } else if (!hasSingleSelection) {
            return "Select one image before queueing an export."
        }
        if (destinationField.text.trim().length === 0)
            return "Choose an export destination first."
        if (batch && exportJobs.batchPreflight.ok === undefined)
            return "Preparing selected exports."
        if (!batch && exportJobs.singlePreflight.ok === undefined)
            return exportJobs.preflightBusy
                   ? "Output details are loading; Start Export will run fresh checks."
                   : "Start Export checks this delivery before adding it."
        if (batch && currentBatchPreflight().readyCount <= 0)
            return "No selected delivery item is ready to export."
        if (!batch && currentPreflight().errorCount > 0)
            return "Resolve preflight issues before queueing this export."
        return ""
    }

    function exportActionGuidance() {
        const reason = exportDisabledReason(hasBatchSelection)
        if (!hasBatchSelection && reason === "Resolve preflight issues before queueing this export.") {
            const issues = currentPreflight().issues || []
            for (let index = 0; index < issues.length; ++index) {
                if (String(issues[index].severity || "") === "error")
                    return String(issues[index].message || reason)
            }
        }
        return reason
    }

    function chooseOfflineRecoveryFolder(selectedOnly) {
        const title = selectedOnly ? "Relink Selected Original" : "Relink All Offline Originals"
        const folderUrl = catalogAssets.pickFolder(title)
        if (!folderUrl)
            return 0
        root.offlineMissingConfirmationArmed = false
        return selectedOnly
                ? catalogAssets.relinkSelectedAssetToFolder(folderUrl)
                : catalogAssets.relinkOfflineAssetsToFolder(folderUrl)
    }

    function confirmSelectedOriginalPermanentlyMissing() {
        if (!root.offlineMissingConfirmationArmed) {
            root.offlineMissingConfirmationArmed = true
            return false
        }
        root.offlineMissingConfirmationArmed = false
        return catalogAssets.markSelectedAssetPermanentlyMissing(true)
    }

    function exportActionMenuItems() {
        return [
            {
                key: "queue-current",
                label: "Start Current Export",
                icon: "export",
                enabled: currentExportActionAvailable(),
                disabledReason: exportDisabledReason(false),
                tooltip: "Start the current export with the selected preset and destination."
            },
            {
                key: "queue-selected",
                label: "Start Selected Exports",
                icon: "media",
                enabled: selectionMetrics.count > 0 && destinationField.text.trim().length > 0 && currentBatchPreflight().readyCount > 0,
                disabledReason: exportDisabledReason(true),
                tooltip: "Start exports for every selected Library image with the current export settings."
            },
            {
                key: "browse-destination",
                label: "Choose Destination",
                icon: "folder",
                enabled: true,
                tooltip: "Choose the folder where exports will be written."
            },
            {
                key: "reset-destination",
                label: "Reset Destination",
                icon: "reset",
                enabled: true,
                tooltip: "Restore the default export folder."
            }
        ]
    }

    function queueManagementMenuItems() {
        return [
            {
                key: "clear-finished",
                label: "Clear Finished Jobs",
                icon: "close",
                enabled: root.historyMutationsEnabled && jobQueue.jobCount > 0,
                tooltip: root.historyMutationsEnabled ? "Remove completed jobs from the background queue." : root.recoveryMutationReason
            },
            {
                key: "choose-destination",
                label: "Choose Destination",
                icon: "folder",
                enabled: true,
                tooltip: "Choose the folder where exports will be written."
            }
        ]
    }

    function handleQueueManagementMenuAction(key) {
        if (key === "clear-finished") {
            jobQueue.clearFinishedJobs()
            return
        }
        if (key === "choose-destination")
            chooseDestinationFolder()
    }

    function jobRowMenuItems(status, jobKind, outputPath, priority,
                             resourcePolicy, canMoveUp, canMoveDown,
                             canDuplicate, canRevealOutput) {
        const renderJob = jobKind === "asset_export" || jobKind === "timeline_render"
        const canChangePriority = renderJob && (status === "Queued" || status === "Paused")
        return [
            {
                key: "move-earlier",
                label: "Move Earlier",
                icon: "move-up",
                enabled: canMoveUp === true,
                tooltip: canMoveUp === true ? "Render this job before the previous job at the same priority." : "The job is already first at this priority."
            },
            {
                key: "move-later",
                label: "Move Later",
                icon: "move-down",
                enabled: canMoveDown === true,
                tooltip: canMoveDown === true ? "Render this job after the next job at the same priority." : "The job is already last at this priority."
            },
            {
                key: "priority-high",
                label: "High Priority",
                icon: "focus-frame",
                checked: priority === "high",
                enabled: canChangePriority,
                separatorBefore: true,
                tooltip: "Schedule this delivery ahead of normal and low priority jobs."
            },
            {
                key: "priority-normal",
                label: "Normal Priority",
                icon: "minus",
                checked: priority === "normal",
                enabled: canChangePriority,
                tooltip: "Use standard render queue priority."
            },
            {
                key: "priority-low",
                label: "Low Priority",
                icon: "move-down",
                checked: priority === "low",
                enabled: canChangePriority,
                tooltip: "Defer this delivery behind higher priority jobs."
            },
            {
                key: "pause",
                label: "Pause Render",
                icon: "pause",
                separatorBefore: true,
                enabled: canPauseJob(status, jobKind),
                tooltip: canPauseJob(status, jobKind) ? "Pause safely and discard an incomplete output." : "Pause is available for queued or running renders."
            },
            {
                key: "resume",
                label: "Resume Render",
                icon: "play",
                enabled: canResumeJob(status, jobKind),
                tooltip: canResumeJob(status, jobKind) ? "Resume this paused render." : "Resume is available after a render is paused."
            },
            {
                key: "resources-playback-first",
                label: "Resources · Playback First",
                icon: "play",
                checked: resourcePolicy === "playback-first",
                enabled: canChangePriority,
                separatorBefore: true,
                tooltip: "Use one render thread and low process priority while preserving preview and delivery quality."
            },
            {
                key: "resources-balanced",
                label: "Resources · Balanced",
                icon: "adjust",
                checked: resourcePolicy === "balanced",
                enabled: canChangePriority,
                tooltip: "Use two render threads at a moderate background priority without changing quality."
            },
            {
                key: "resources-export-first",
                label: "Resources · Export First",
                icon: "export",
                checked: resourcePolicy === "export-first",
                enabled: canChangePriority,
                tooltip: "Let the render use available threads; delivery and preview settings remain unchanged."
            },
            {
                key: "retry",
                label: "Retry Job",
                icon: "reset",
                enabled: canRetryJob(status, jobKind),
                tooltip: canRetryJob(status, jobKind) ? "Retry this failed job." : "Retry is available after a job fails."
            },
            {
                key: "cancel",
                label: status === "Canceling" ? "Canceling Job" : "Cancel Job",
                icon: "close",
                enabled: canCancelJob(status, jobKind) && status !== "Canceling",
                tooltip: canCancelJob(status, jobKind) ? "Cancel this queued or running job." : "Only queued or running jobs can be canceled."
            },
            {
                key: "duplicate",
                label: status === "Completed" && jobKind === "asset_export" ? "Repeat Export" : "Duplicate Job",
                icon: "copy",
                enabled: canDuplicate === true,
                separatorBefore: true,
                tooltip: canDuplicate === true
                         ? (status === "Completed" && jobKind === "asset_export"
                            ? "Queue this photo again with the same edit, format, and delivery settings."
                            : "Queue an independent copy with the same delivery settings.")
                         : "Wait for the active render to stop before duplicating it."
            },
            {
                key: "reveal",
                label: "Reveal in Finder",
                icon: "folder",
                enabled: canRevealOutput === true && canRevealJob(status, jobKind, outputPath),
                tooltip: canRevealOutput === true ? "Show the completed file in Finder." : "Reveal is available after the output is written."
            }
        ].map(function(item) {
            if (!root.historyMutationsEnabled && item.key !== "reveal") {
                item.enabled = false
                item.tooltip = root.recoveryMutationReason
            }
            return item
        })
    }

    function handleJobRowMenuAction(key, jobId, status, jobKind, outputPath) {
        if (key === "move-earlier") {
            jobQueue.moveRenderJob(jobId, -1)
            return
        }
        if (key === "move-later") {
            jobQueue.moveRenderJob(jobId, 1)
            return
        }
        if (key.indexOf("priority-") === 0) {
            jobQueue.setJobPriority(jobId, key.slice("priority-".length))
            return
        }
        if (key.indexOf("resources-") === 0) {
            jobQueue.setRenderResourcePolicy(jobId, key.slice("resources-".length))
            return
        }
        if (key === "pause" && canPauseJob(status, jobKind)) {
            jobQueue.pauseJob(jobId)
            return
        }
        if (key === "resume" && canResumeJob(status, jobKind)) {
            jobQueue.resumeJob(jobId)
            return
        }
        if (key === "retry" && canRetryJob(status, jobKind)) {
            jobQueue.retryJob(jobId)
            return
        }
        if (key === "cancel" && canCancelJob(status, jobKind) && status !== "Canceling") {
            jobQueue.cancelJob(jobId)
            return
        }
        if (key === "duplicate") {
            jobQueue.duplicateRenderJob(jobId)
            return
        }
        if (key === "reveal" && canRevealJob(status, jobKind, outputPath))
            jobQueue.revealJobOutput(jobId)
    }

    function chooseDestinationFolder() {
        const selectedFolder = exportJobs.pickDestinationFolder()
        if (selectedFolder.length > 0)
            destinationField.text = selectedFolder
    }

    function handleExportMenuAction(key) {
        if (key === "queue-current") {
            queueCurrentSelection()
        } else if (key === "queue-selected") {
            queueBatchExports()
        } else if (key === "browse-destination") {
            chooseDestinationFolder()
        } else if (key === "reset-destination") {
            destinationField.text = exportJobs.defaultDestinationDirectory()
        } else if (key === "clear-finished") {
            jobQueue.clearFinishedJobs()
        }
    }

    Component.onCompleted: {
        exportPreviewVisible = exportWorkspaceSettings.previewVisible
        exportActivityVisible = exportWorkspaceSettings.activityVisible
        let saved = null
        try {
            saved = JSON.parse(String(exportWorkspaceSettings.lastDeliverySettingsJson || ""))
        } catch (error) {
            saved = null
        }
        if (saved && typeof saved === "object") {
            const presetIndex = root.comboIndexOfText(presetCombo, String(saved.preset || ""))
            if (presetIndex >= 0)
                presetCombo.currentIndex = presetIndex
            if (Number.isFinite(Number(saved.jpegQuality)))
                jpegQualityField.value = Math.max(1, Math.min(100, Number(saved.jpegQuality)))
            if (Number.isFinite(Number(saved.maxEdge)))
                maxEdgeField.value = Math.max(0, Math.min(100000, Number(saved.maxEdge)))
            const sharpeningIndex = root.comboIndexOfText(sharpeningCombo, String(saved.sharpening || "Off"))
            if (sharpeningIndex >= 0)
                sharpeningCombo.currentIndex = sharpeningIndex
            const colorIndex = root.comboIndexOfText(colorSpaceCombo, String(saved.colorSpace || ""))
            if (colorIndex >= 0)
                colorSpaceCombo.currentIndex = colorIndex
            const cropIndex = root.comboIndexOfText(cropConstraintCombo, String(saved.crop || ""))
            if (cropIndex >= 0)
                cropConstraintCombo.currentIndex = cropIndex
            if (String(saved.filenameTemplate || "").length > 0)
                filenameTemplateField.text = String(saved.filenameTemplate)
            if (String(saved.destination || "").trim().length > 0)
                destinationField.text = String(saved.destination)
        }
        if (destinationField.text.length === 0)
            destinationField.text = exportJobs.defaultDestinationDirectory()
        exportSettingsReady = true
        refreshExportPreflight(true)
        scheduleDeliverySettingsSave()
    }

    Connections {
        target: presetCombo
        function onCurrentIndexChanged() {
            if (presetCombo.currentText === "Quick Web") {
                jpegQualityField.value = 88
                maxEdgeField.value = 1440
            } else if (presetCombo.currentText === "Client Preview") {
                jpegQualityField.value = 92
                maxEdgeField.value = 2048
            } else if (presetCombo.currentText === "Web Proof") {
                jpegQualityField.value = 94
                maxEdgeField.value = 2560
            }
            root.scheduleDeliverySettingsSave()
        }
    }
    Connections {
        target: colorSpaceCombo
        function onCurrentIndexChanged() { root.scheduleDeliverySettingsSave() }
    }
    Connections {
        target: cropConstraintCombo
        function onCurrentIndexChanged() { root.scheduleDeliverySettingsSave() }
    }
    Connections {
        target: destinationField
        function onTextChanged() { root.scheduleDeliverySettingsSave() }
    }
    Connections {
        target: filenameTemplateField
        function onTextChanged() { root.scheduleDeliverySettingsSave() }
    }
    Connections {
        target: jpegQualityField
        function onValueChanged() { root.scheduleDeliverySettingsSave() }
    }
    Connections {
        target: maxEdgeField
        function onValueChanged() { root.scheduleDeliverySettingsSave() }
    }
    Connections {
        target: sharpeningCombo
        function onCurrentIndexChanged() { root.scheduleDeliverySettingsSave() }
    }

    Timer {
        id: batchQueuedTimer
        interval: 3000
        onTriggered: batchQueuedFeedback.visible = false
    }

    RowLayout {
        objectName: "exportMainWorkspace"
        readonly property string surfaceContract: "export-main-workspace:v2|preview:configurable|queue-panel:configurable|configuration-panel:always|primary-action:always|progress-contextual:true|failure-contextual:true|no-elide:true|no-overlap:true"
        anchors.fill: parent
        spacing: 14

        ColumnLayout {
            visible: root.exportPreviewVisible || root.exportActivityVisible
            Layout.fillWidth: visible
            Layout.fillHeight: true
            spacing: 10

            BrandPanel {
                objectName: "exportPreviewPanel"
                visible: root.exportPreviewVisible
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: root.hasSingleSelection ? root.currentSelectionLabel() : "Prepare your delivery"
                subtitle: root.hasBatchSelection ? root.selectionSummaryText()
                          : root.hasSingleSelection ? "Current edit · output settings are applied during export"
                          : "Choose photos in Library to start a delivery"

                Rectangle {
                    id: deliveryImageStage
                    readonly property bool currentEditorAsset: root.hasSingleSelection && editorPreview.assetId === catalogAssets.selectedAssetId
                    readonly property bool nativePreviewAvailable: currentEditorAsset && editorPreview.displayPreviewNativeAvailable
                    readonly property real nativeWidth: Math.max(1, Number(editorPreview.displayPreviewNativeSize.width || 1))
                    readonly property real nativeHeight: Math.max(1, Number(editorPreview.displayPreviewNativeSize.height || 1))
                    readonly property real nativeScale: Math.max(0, Math.min((width - 24) / nativeWidth, (height - 24) / nativeHeight))
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: theme.mediaCanvas
                    radius: theme.radius4
                    clip: true

                    Image {
                        objectName: "exportPhotoPreview"
                        anchors.fill: parent
                        anchors.margins: 12
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        source: root.hasSingleSelection
                                ? (editorPreview.assetId === catalogAssets.selectedAssetId && editorPreview.displayPreviewSource.length > 0
                                   ? editorPreview.displayPreviewSource : catalogAssets.selectedThumbnailSource)
                                : ""
                        visible: root.hasSingleSelection && !deliveryImageStage.nativePreviewAvailable
                    }
                    EditorPreviewTextureItem {
                        objectName: "exportNativePhotoPreview"
                        anchors.centerIn: parent
                        width: deliveryImageStage.nativeWidth * deliveryImageStage.nativeScale
                        height: deliveryImageStage.nativeHeight * deliveryImageStage.nativeScale
                        visible: deliveryImageStage.nativePreviewAvailable
                        controller: editorPreview
                    }
                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: !root.hasSingleSelection
                        spacing: 14
                        Label {
                            text: "Choose the photographs you want to deliver"
                            color: theme.textStrong
                            font.pixelSize: theme.typeSection
                        }
                        ActionButton {
                            Layout.alignment: Qt.AlignHCenter
                            text: "Select in Library"
                            onClicked: root.assetActivated("library")
                        }
                    }
                }
            }
        BrandPanel {
            id: deliveryActivityPanel
            objectName: "exportQueuePanel"
            visible: root.exportActivityVisible
            readonly property string surfaceContract: "export-queue-panel:v1|queue-list:always|empty-state:contextual|management:one-expansion|row-actions:hover-focus|progress:contextual|errors:diagnostic|no-elide:true"
            Layout.fillWidth: true
            Layout.fillHeight: !root.exportPreviewVisible
            Layout.preferredHeight: jobQueue.jobCount > 1 || jobQueue.activeJobCount > 0 || root.unconfirmedExports.length > 0
                                    ? 280 : jobQueue.jobCount > 0 ? 210 : 120
            Layout.minimumHeight: 110
            headerVisible: false
            bodyMargin: theme.panelPadding
            bodySpacing: theme.gapControl
            title: "Delivery activity"
            subtitle: exportJobs.pendingAdmissionCount > 0 || exportJobs.admissionError.length > 0 || exportJobs.submissionError.length > 0
                      ? exportJobs.status : jobQueue.status

            ActionButton {
                objectName: "exportRetryAdmissionButton"
                text: "Retry saving export jobs"
                iconName: "retry"
                visible: exportJobs.admissionError.length > 0
                enabled: exportJobs.pendingAdmissionCount === 0 && exportJobs.historyMutationsEnabled
                onClicked: exportJobs.retryUnconfirmedExports()
            }

            Item {
                objectName: "exportPresetsPanel"
                width: 1
                height: 1
                visible: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                SectionHeader {
                    Layout.fillWidth: true
                    title: deliveryActivityPanel.title
                    subtitle: deliveryActivityPanel.subtitle
                    accent: deliveryActivityPanel.accent
                }

                MenuButton {
                    objectName: "exportQueueManagementMenuButton"
                    visible: jobQueue.jobCount > 0
                    text: "Clear Done"
                    iconName: "close"
                    tone: "secondary"
                    compact: true
                    active: false
                    enabled: root.historyMutationsEnabled && jobQueue.jobCount > 0
                    tooltipText: root.historyMutationsEnabled ? "Clear completed jobs. Open the menu for queue options." : root.recoveryMutationReason
                    menuItems: root.queueManagementMenuItems()
                    menuWidth: 190
                    onClicked: jobQueue.clearFinishedJobs()
                    onMenuItemTriggered: function(key) { root.handleQueueManagementMenuAction(key) }
                }
            }

            ColumnLayout {
                id: exportOverallProgress
                objectName: "exportOverallProgress"
                readonly property var summary: {
                    jobQueue.jobCount
                    jobQueue.status
                    return jobQueue.progressSummaryForKind("asset_export")
                }
                Layout.fillWidth: true
                visible: Number(summary.total || 0) > 0
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: "Delivery  " + Number(exportOverallProgress.summary.terminal || 0) + " of " +
                              Number(exportOverallProgress.summary.total || 0) + " finished"
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: 9
                    }

                    Label {
                        text: Number(exportOverallProgress.summary.failed || 0) > 0
                              ? Number(exportOverallProgress.summary.failed) + " failed" : ""
                        color: theme.error
                        font.family: theme.fontMono
                        font.pixelSize: 9
                    }
                }

                LooksyLoadingScene {
                    objectName: "exportDeliveryLoadingScene"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.min(280, parent.width)
                    scene: "export"
                    compact: true
                    active: Number(exportOverallProgress.summary.terminal || 0) < Number(exportOverallProgress.summary.total || 0)
                    failed: !active && Number(exportOverallProgress.summary.failed || 0) > 0
                    progress: Number(exportOverallProgress.summary.overallPercent || 0) / 100
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    Item {
                        objectName: "exportQueueEmptyState"
                        readonly property string visibilityRole: "contextual|empty-queue-guidance"
                        Layout.fillWidth: true
                        visible: jobQueue.jobCount === 0 && root.unconfirmedExports.length === 0
                        implicitHeight: emptyQueueContent.implicitHeight + theme.space16

                        ColumnLayout {
                            id: emptyQueueContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: theme.space4

                            Label {
                                objectName: "jobHistoryEmptyTitle"
                                text: jobQueue.loading ? "Loading job history…"
                                    : jobQueue.historyError ? "Job history unavailable"
                                    : "No background jobs"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeBody
                                font.weight: Font.Medium
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }

                            Label {
                                text: jobQueue.loading ? "Your saved jobs will appear here."
                                    : jobQueue.historyError ? "Reconnect to the catalog and refresh. Existing history is preserved."
                                    : "Exports, imports, and timeline renders appear here."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeBody
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Repeater {
                        model: root.unconfirmedExports
                        delegate: ExportQueueRow {
                            required property var modelData
                            objectName: "exportUnconfirmedRow"
                            title: modelData.displayLabel || "Export"
                            subtitle: "Waiting for durable catalog confirmation"
                            destination: modelData.destinationDirectory || ""
                            preset: modelData.preset || ""
                            statusText: modelData.status
                            statusTone: modelData.status === "Save unconfirmed" ? "error" : "info"
                            busy: modelData.status === "Saving"
                            errorText: modelData.errorMessage || ""
                        }
                    }

                    Repeater {
                        objectName: "exportQueueRepeater"
                        readonly property string visibilityRole: "always-visible|jobs-progress-retry-cancel-remove"
                        model: jobQueue

                        delegate: ExportQueueRow {
                            objectName: "exportQueueRow"
                            required property string jobId
                            required property string jobKind
                            required property string displayLabel
                            required property string sourcePath
                            required property string presetName
                            required property string destinationPath
                            required property string outputPath
                            required property string status
                            required property string errorMessage
                            required property int progress
                            required property var metadata
                            required property string priority
                            required property bool canMoveUp
                            required property bool canMoveDown
                            required property bool canDuplicate
                            required property bool canRevealOutput

                            readonly property var deliverySummary: metadata && metadata.deliverySummary
                                                                        ? metadata.deliverySummary : ({})
                            readonly property string resourcePolicy: metadata && metadata.resourcePolicy
                                                                     ? String(metadata.resourcePolicy)
                                                                     : "playback-first"

                            Layout.fillWidth: true
                            fallbackIconName: jobKind === "timeline_render" ? "timeline" : "export"
                            title: displayLabel
                            subtitle: jobKindLabel(jobKind)
                                      + (deliverySummary.outputProfile
                                         ? "  \u2022  " + deliverySummary.outputProfile : "")
                            destination: jobDestination(outputPath, destinationPath, sourcePath)
                            preset: presetName
                            format: String(deliverySummary.format || "")
                            sizeLabel: Number(deliverySummary.width || 0) > 0
                                       ? String(deliverySummary.width) + " × " + String(deliverySummary.height)
                                       : ""
                            statusText: status
                            statusTone: root.jobStatusTone(status)
                            busy: status === "Running" || status === "Canceling"
                            showProgress: status === "Queued"
                                          || status === "Running"
                                          || status === "Canceling"
                            progressValue: status === "Queued"
                                           ? 0.08
                                           : Math.max(0.05, Math.min(1.0, progress / 100.0))
                            progressAccent: status === "Completed"
                                            ? theme.accentCool500
                                            : status === "Canceling"
                                              ? theme.alpha("#d96a5b", 0.88)
                                              : status === "Failed"
                                                ? "#d96a5b"
                                                : theme.accentWarm500
                            errorText: errorMessage
                            menuVisible: canRetryJob(status, jobKind) ||
                                         canCancelJob(status, jobKind) ||
                                         canPauseJob(status, jobKind) ||
                                         canResumeJob(status, jobKind) ||
                                         canRevealJob(status, jobKind, outputPath) ||
                                         canMoveUp || canMoveDown || canDuplicate
                            menuEnabled: menuVisible
                            menuItems: root.jobRowMenuItems(status, jobKind, outputPath, priority,
                                                            resourcePolicy, canMoveUp, canMoveDown,
                                                            canDuplicate, canRevealOutput)
                            menuWidth: 268
                            menuTooltipText: "Job actions."
                            showRemoveButton: true
                            removeEnabled: root.historyMutationsEnabled && status !== "Running" && status !== "Canceling"
                            removeHelperText: root.historyMutationsEnabled ? "Remove this job from the queue" : root.recoveryMutationReason
                            onPrimaryActionRequested: {
                                if (canRetryJob(status, jobKind))
                                    jobQueue.retryJob(jobId)
                                else if (canCancelJob(status, jobKind) && status !== "Canceling")
                                    jobQueue.cancelJob(jobId)
                            }
                            onMenuItemTriggered: function(key) {
                                root.handleJobRowMenuAction(key, jobId, status, jobKind, outputPath)
                            }
                            onRemoveRequested: jobQueue.removeJob(jobId)
                        }
                    }
                }
            }
        }

        }

        BrandPanel {
            objectName: "exportConfigurationPanel"
            readonly property string surfaceContract: "export-configuration-panel:v1|selection:always|preset:always|color-space:always|crop:always|destination:always|preflight:contextual|primary-actions:always|no-elide:true|no-overlap:true"
            Layout.preferredWidth: 400
            Layout.fillWidth: !root.exportPreviewVisible && !root.exportActivityVisible
            Layout.fillHeight: true
            title: "Delivery settings"
            subtitle: root.selectionSummaryText()

            Item {
                objectName: "exportSettingsPanel"
                width: 1
                height: 1
                visible: true
            }

            MenuButton {
                objectName: "exportWorkspaceLayoutButton"
                Layout.alignment: Qt.AlignRight
                compact: true
                text: "Layout"
                iconName: "panel-right"
                primaryOpensMenu: true
                tooltipText: "Choose visible Export panels"
                menuItems: root.exportLayoutMenuItems()
                onMenuItemTriggered: function(key) { root.handleExportLayoutMenu(key) }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.maximumWidth: root.exportPreviewVisible || root.exportActivityVisible ? 16777215 : 640
                Layout.alignment: Qt.AlignHCenter
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    InspectorSection {
                        objectName: "exportSelectionSection"
                        readonly property string visibilityRole: "always-visible|selection-summary-media-mix-warning"
                        Layout.fillWidth: true
                        title: "Selection"
                        visible: selectionMetrics.videos > 0 || selectionMetrics.timelines > 0

                        Label {
                            Layout.fillWidth: true
                            text: root.selectionSummaryText()
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeBody
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: selectionMetrics.videos > 0 || selectionMetrics.timelines > 0
                            text: "Videos and timelines export as original copies regardless of preset."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WordWrap
                        }
                    }

                    InspectorSection {
                        objectName: "exportPresetSection"
                        readonly property string visibilityRole: "always-visible|preset-summary-quality-format"
                        Layout.fillWidth: true
                        title: "Format Preset"

                        ToneComboBox {
                            id: presetCombo
                            objectName: "exportPresetCombo"
                            Layout.fillWidth: true
                            model: ["Full Resolution", "Archive TIFF", "OpenEXR Half", "ACES2065-1 OpenEXR", "ACEScg OpenEXR", "HDR PQ AVIF", "HDR HLG AVIF", "Web Proof", "Client Preview", "Quick Web", "Original Copy"]
                        }

                        ExportPresetSummary {
                            format: currentPresetDetail().format
                            bitDepth: currentPresetDetail().bitDepth
                            maxEdge: currentPresetDetail().maxEdge
                            quality: currentPresetDetail().quality
                            description: currentPresetDetail().description
                        }

                        Label {
                            objectName: "exportOutputKindGuidance"
                            Layout.fillWidth: true
                            text: selectionMetrics.videos > 0 || selectionMetrics.timelines > 0
                                  ? "Photos render to image files; videos and timelines copy as originals. This does not save an editable project."
                                  : "Rendered image files · your editable photo recipe and Carousel document stay in Library."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.jpegPreset
                            spacing: 8

                            Label {
                                text: "JPEG quality"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                            }
                            ToneSpinBox {
                                id: jpegQualityField
                                objectName: "exportJpegQualityField"
                                accessibleLabel: qsTr("JPEG quality")
                                from: 1
                                to: 100
                                value: 94
                                valueUnit: "%"
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: "Long edge"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                            }
                            ToneSpinBox {
                                id: maxEdgeField
                                objectName: "exportMaxEdgeField"
                                accessibleLabel: qsTr("Maximum long edge")
                                from: 0
                                to: 100000
                                stepSize: 128
                                value: 2560
                                valueUnit: "px"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.jpegPreset
                            spacing: 8
                            Label {
                                text: "Output sharpening"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                            }
                            ToneComboBox {
                                id: sharpeningCombo
                                objectName: "exportOutputSharpeningCombo"
                                accessibleLabel: qsTr("Output sharpening")
                                Layout.fillWidth: true
                                model: ["Off", "Screen", "Print"]
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "Applied after resizing"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }

                    InspectorSection {
                        objectName: "exportFilenameSection"
                        Layout.fillWidth: true
                        title: "Filename"

                        ToneTextField {
                            id: filenameTemplateField
                            objectName: "exportFilenameTemplateField"
                            accessibleLabel: qsTr("Export filename template")
                            Layout.fillWidth: true
                            text: "{name}"
                            placeholderText: "{name}_{sequence}"
                        }

                        Label {
                            objectName: "exportFilenamePreviewLabel"
                            Layout.fillWidth: true
                            text: root.filenamePreviewText()
                            color: theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeLabel
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Tokens: {name}, {sequence}"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 9
                        }
                    }

                    Item {
                        id: outputGeometryRow
                        Layout.fillWidth: true
                        implicitHeight: Math.max(outputColorSection.implicitHeight, outputCropSection.implicitHeight)

                        InspectorSection {
                            id: outputColorSection
                            width: (outputGeometryRow.width - 12) / 2
                            height: implicitHeight
                            objectName: "exportColorSpaceSection"
                            readonly property string visibilityRole: "always-visible|color-space-choice-explanation"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            title: "Color space"

                            ToneComboBox {
                                id: colorSpaceCombo
                                accessibleLabel: qsTr("Export color space")
                                Layout.fillWidth: true
                                model: root.colorSpaceOptions
                            }

                            Label {
                                text: {
                                    const choice = colorSpaceCombo.currentText
                                    if (choice === "sRGB")
                                        return "For web and general display."
                                    if (choice === "Display P3")
                                        return "For wide-gamut displays."
                                    if (choice === "Adobe RGB")
                                        return "For compatible print workflows."
                                    return "Use each image’s color profile."
                                }
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        InspectorSection {
                            id: outputCropSection
                            x: outputColorSection.width + 12
                            width: outputColorSection.width
                            height: implicitHeight
                            objectName: "exportCropSection"
                            readonly property string visibilityRole: "always-visible|crop-constraint"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            title: "Crop"

                            ToneComboBox {
                                id: cropConstraintCombo
                                accessibleLabel: qsTr("Export crop constraint")
                                Layout.fillWidth: true
                                model: ["Current", "Original", "1:1", "4:5", "5:4", "8x10 in", "10x8 in", "A4", "A4 Landscape"]
                            }
                        }

                    }

                    InspectorSection {
                        objectName: "exportDestinationSection"
                        readonly property string visibilityRole: "always-visible|destination-field-browse-reset"
                        Layout.fillWidth: true
                        title: "Destination"

                        ToneTextField {
                            id: destinationField
                            objectName: "exportDestinationField"
                            Layout.fillWidth: true
                            placeholderText: exportJobs.defaultDestinationDirectory()
                            Accessible.name: "Export destination"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Browse"
                                onClicked: root.chooseDestinationFolder()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Reset"
                                onClicked: destinationField.text = exportJobs.defaultDestinationDirectory()
                            }
                        }
                    }

                    InspectorSection {
                        id: exportPreflightSection
                        objectName: "exportPreflightSection"
                        readonly property string visibilityRole: "contextual|delivery-destination-filenames-collisions-blockers-profile-metadata-sharpening"
                        Layout.fillWidth: true
                        visible: root.hasSingleSelection || root.hasBatchSelection
                        title: "Output details"
                        expandable: true
                        expanded: !exportJobs.preflightBusy &&
                                  Number(batch ? preflight.blockedCount : preflight.errorCount) > 0

                        readonly property bool batch: root.hasBatchSelection
                        property var preflight: root.currentDeliveryPreflight()

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                objectName: "exportPreflightStatusLabel"
                                text: (exportJobs.preflightBusy || exportJobs.batchSubmissionBusy) ? "Checking export settings" : exportPreflightSection.batch
                                      ? (exportPreflightSection.preflight.blockedCount === 0
                                         ? exportPreflightSection.preflight.readyCount + " ready"
                                         : exportPreflightSection.preflight.blockedCount + " blocked")
                                      : (exportPreflightSection.preflight.ok
                                         ? "Ready"
                                         : exportPreflightSection.preflight.errorCount + " blocking")
                                color: exportPreflightSection.preflight.ok ? theme.accentCool400 : theme.accentWarm400
                                font.family: theme.fontMono
                                font.pixelSize: 11
                                font.weight: Font.Medium
                            }

                            Label {
                                Layout.fillWidth: true
                                text: exportPreflightSection.batch
                                      ? exportPreflightSection.preflight.preset + "  •  " +
                                        exportPreflightSection.preflight.assetCount + " files"
                                      : exportPreflightSection.preflight.format + "  " +
                                      (exportPreflightSection.preflight.bitDepth > 0
                                       ? exportPreflightSection.preflight.bitDepth + "-bit  "
                                       : "") +
                                      exportPreflightSection.preflight.outputColorSpace + "  " +
                                      (exportPreflightSection.preflight.estimatedPixelWidth > 0
                                       ? exportPreflightSection.preflight.estimatedPixelWidth + " x " +
                                         exportPreflightSection.preflight.estimatedPixelHeight
                                       : "Size pending")
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                horizontalAlignment: Text.AlignRight
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 8
                                elide: Text.ElideNone
                            }
                        }

                        Label {
                            objectName: "exportPreflightDestinationLabel"
                            Layout.fillWidth: true
                            text: exportPreflightSection.batch
                                  ? "Destination  " + String(exportPreflightSection.preflight.destinationPath || "Choose a folder")
                                  : "Output  " + String(exportPreflightSection.preflight.outputPath || "Choose a destination")
                            color: theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            maximumLineCount: 3
                            elide: Text.ElideNone
                        }

                        Label {
                            objectName: "exportPreflightCollisionSummary"
                            Layout.fillWidth: true
                            visible: exportPreflightSection.batch
                            text: Number(exportPreflightSection.preflight.collisionCount || 0) > 0
                                  ? exportPreflightSection.preflight.collisionCount + " filename collision" +
                                    (exportPreflightSection.preflight.collisionCount === 1 ? "" : "s") +
                                    " will use numbered names; existing files stay untouched."
                                  : "No existing output will be overwritten."
                            color: Number(exportPreflightSection.preflight.collisionCount || 0) > 0
                                   ? theme.warning : theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }

                        Repeater {
                            objectName: "exportBatchPreflightRepeater"
                            model: exportPreflightSection.batch
                                   ? (exportPreflightSection.preflight.items || []) : []

                            delegate: Rectangle {
                                id: exportBatchPreflightItem
                                objectName: "exportBatchPreflightItem"
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: batchPreflightItemContent.implicitHeight + 12
                                radius: 4
                                color: theme.alpha(theme.ink900, 0.56)
                                border.width: 1
                                border.color: Number(modelData.errorCount || 0) > 0
                                              ? theme.alpha(theme.error, 0.48)
                                              : modelData.collision
                                                ? theme.alpha(theme.warning, 0.44)
                                                : theme.alpha(theme.glassEdge, 0.22)

                                RowLayout {
                                    id: batchPreflightItemContent
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 7

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            Layout.fillWidth: true
                                            text: String(exportBatchPreflightItem.modelData.outputFileName ||
                                                         exportBatchPreflightItem.modelData.displayLabel || "Export")
                                            color: theme.textMain
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            elide: Text.ElideNone
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: (Number(exportBatchPreflightItem.modelData.bitDepth || 0) > 0
                                                  ? Number(exportBatchPreflightItem.modelData.bitDepth) + "-bit"
                                                  : "Original copy") +
                                                  (Number(exportBatchPreflightItem.modelData.estimatedPixelWidth || 0) > 0
                                                   ? " · " + exportBatchPreflightItem.modelData.estimatedPixelWidth + " × " +
                                                     exportBatchPreflightItem.modelData.estimatedPixelHeight + " px" : "") +
                                                  (String(exportBatchPreflightItem.modelData.outputColorSpace || "").length > 0
                                                   ? " · " + exportBatchPreflightItem.modelData.outputColorSpace : "")
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 8
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            elide: Text.ElideNone
                                        }
                                    }

                                    Label {
                                        text: Number(exportBatchPreflightItem.modelData.errorCount || 0) > 0
                                              ? "BLOCKED"
                                              : exportBatchPreflightItem.modelData.collision ? "RENAMED" : "READY"
                                        color: Number(exportBatchPreflightItem.modelData.errorCount || 0) > 0
                                               ? theme.error
                                               : exportBatchPreflightItem.modelData.collision ? theme.warning : theme.accentCool400
                                        font.family: theme.fontMono
                                        font.pixelSize: 8
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }

                        Label {
                            visible: !exportPreflightSection.batch &&
                                     Boolean(exportPreflightSection.preflight.softProofEnabled)
                            text: "Soft proof " + exportPreflightSection.preflight.softProofProfile
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            Layout.fillWidth: true
                        }

                        Repeater {
                            objectName: "exportPreflightIssueRepeater"
                            model: exportPreflightSection.batch ? [] : (exportPreflightSection.preflight.issues || [])

                            delegate: ExportPreflightIssueRow {
                                required property var modelData
                                issue: modelData
                            }
                        }

                        Label {
                            objectName: "exportDeliveryPolicyLabel"
                            Layout.fillWidth: true
                            text: String(exportPreflightSection.preflight.metadataPolicy || "") + "\n" +
                                  String(exportPreflightSection.preflight.sharpeningPolicy || "")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 9
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }
                    }


                }
            }

            Rectangle {
                id: exportDeliveryReviewSummaryCard
                objectName: "exportDeliveryReviewSummaryCard"
                readonly property var summary: root.currentDeliveryPreflight()
                readonly property var firstItem: root.hasBatchSelection && (summary.items || []).length > 0
                                                     ? summary.items[0] : ({})
                Layout.fillWidth: true
                Layout.preferredHeight: deliverySummaryContent.implicitHeight + 20
                visible: (root.hasSingleSelection || root.hasBatchSelection) &&
                         !exportJobs.preflightBusy &&
                         (root.hasBatchSelection ? exportJobs.batchPreflight.ok !== undefined
                                                 : exportJobs.singlePreflight.ok !== undefined)
                radius: 6
                color: theme.alpha(theme.ink900, 0.76)
                border.width: 1
                border.color: Number(root.hasBatchSelection ? summary.blockedCount : summary.errorCount) > 0
                              ? theme.alpha(theme.error, 0.52)
                              : theme.alpha(theme.interaction400, 0.32)

                ColumnLayout {
                    id: deliverySummaryContent
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            objectName: "exportDeliveryReviewSummaryStatus"
                            Layout.fillWidth: true
                            text: root.hasBatchSelection
                                  ? Number(exportDeliveryReviewSummaryCard.summary.blockedCount || 0) > 0
                                    ? exportDeliveryReviewSummaryCard.summary.blockedCount + " blocked  •  " +
                                      exportDeliveryReviewSummaryCard.summary.readyCount + " ready"
                                    : exportDeliveryReviewSummaryCard.summary.readyCount + " files ready"
                                  : Number(exportDeliveryReviewSummaryCard.summary.errorCount || 0) > 0
                                    ? exportDeliveryReviewSummaryCard.summary.errorCount + " blocking issue"
                                    : "Ready to deliver"
                            color: Number(root.hasBatchSelection
                                          ? exportDeliveryReviewSummaryCard.summary.blockedCount
                                          : exportDeliveryReviewSummaryCard.summary.errorCount) > 0
                                   ? theme.error : theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }

                        Label {
                            text: Number(root.hasBatchSelection
                                         ? exportDeliveryReviewSummaryCard.summary.collisionCount
                                         : (exportDeliveryReviewSummaryCard.summary.collision ? 1 : 0)) > 0
                                  ? "NUMBERED COPY" : "NO OVERWRITE"
                            color: theme.interaction400
                            font.family: theme.fontMono
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                    }

                    Label {
                        objectName: "exportDeliveryReviewSummaryPath"
                        Layout.fillWidth: true
                        text: root.hasBatchSelection
                              ? String(exportDeliveryReviewSummaryCard.firstItem.outputFileName || "") +
                                (Number(exportDeliveryReviewSummaryCard.summary.assetCount || 0) > 1
                                 ? "  + " + (exportDeliveryReviewSummaryCard.summary.assetCount - 1) + " more" : "") +
                                "  →  " + String(exportDeliveryReviewSummaryCard.summary.destinationPath || "")
                              : String(exportDeliveryReviewSummaryCard.summary.outputPath || "").split(/[\\/]/).pop()
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }
                }
            }

            Label {
                id: batchQueuedFeedback
                visible: false
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                color: theme.accentCool400
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            RowLayout {
                objectName: "exportActionGuidanceRow"
                Layout.fillWidth: true
                visible: (root.hasSingleSelection || root.hasBatchSelection) &&
                         root.exportActionGuidance().length > 0
                spacing: theme.space8

                Label {
                    objectName: "exportActionGuidanceLabel"
                    Layout.fillWidth: true
                    text: root.exportActionGuidance()
                    color: exportJobs.preflightBusy || exportJobs.batchSubmissionBusy
                           ? theme.textMuted : theme.warning
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    wrapMode: Text.WordWrap
                    horizontalAlignment: exportBrowseBlockedDestinationButton.visible
                                         ? Text.AlignLeft : Text.AlignHCenter
                    Accessible.role: Accessible.StaticText
                }

                ActionButton {
                    id: exportBrowseBlockedDestinationButton
                    objectName: "exportBrowseBlockedDestinationButton"
                    visible: root.exportActionGuidance().toLowerCase().indexOf("destination") >= 0
                    text: "Change Folder"
                    iconName: "folder"
                    compact: true
                    onClicked: root.chooseDestinationFolder()
                }
            }

            SplitButton {
                objectName: "exportBatchSplitButton"
                Layout.fillWidth: true
                visible: hasBatchSelection
                text: root.currentBatchPreflight().blockedCount > 0
                      ? "Start " + root.currentBatchPreflight().readyCount + " Ready"
                      : "Start Selected"
                tone: "primary"
                iconName: "export"
                tooltipText: root.currentBatchPreflight().blockedCount > 0
                             ? "Start ready exports now; blocked items remain in Delivery Review."
                             : "Start " + selectionMetrics.count + (selectionMetrics.count === 1 ? " selected export." : " selected exports.")
                primaryEnabled: selectionMetrics.count > 0 && destinationField.text.trim().length > 0
                                && root.currentBatchPreflight().readyCount > 0
                disabledReason: root.exportActionGuidance()
                menuItems: root.exportActionMenuItems()
                menuWidth: 210
                menuTooltipText: "Export options"
                onClicked: root.queueBatchExports()
                onMenuItemTriggered: function(key) { root.handleExportMenuAction(key) }
            }

            SplitButton {
                objectName: "exportCurrentSplitButton"
                Layout.fillWidth: true
                visible: hasSingleSelection && !hasBatchSelection
                text: "Start Export"
                tone: "primary"
                iconName: "export"
                tooltipText: "Start the current export with the selected preset and destination."
                primaryEnabled: root.currentExportActionAvailable()
                disabledReason: root.exportActionGuidance()
                menuItems: root.exportActionMenuItems()
                menuWidth: 210
                menuTooltipText: "Export options"
                onClicked: root.queueCurrentSelection()
                onMenuItemTriggered: function(key) { root.handleExportMenuAction(key) }
            }

            ActionButton {
                Layout.fillWidth: true
                visible: !hasSingleSelection && !hasBatchSelection
                text: "Select in Library"
                tooltipText: "Choose photos in Library to export."
                onClicked: root.assetActivated("library")
            }
        }
    }

    StudioOfflineRecoveryBanner {
        id: exportOfflineRecoveryBanner
        studioTheme: theme
        pageName: "export"
        selectedAssetId: catalogAssets.selectedAssetId
        availabilityState: catalogAssets.selectedAvailabilityState
        availabilityTitle: catalogAssets.selectedAvailabilityTitle
        availabilityDetail: catalogAssets.selectedAvailabilityDetail
        importModeOpen: false
        missingConfirmationArmed: root.offlineMissingConfirmationArmed
        accentColor: theme.accentCool400
        dimTextColor: theme.textMuted
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        z: 125
        onRelinkSelectedRequested: root.chooseOfflineRecoveryFolder(true)
        onRelinkAllRequested: root.chooseOfflineRecoveryFolder(false)
        onRecheckRequested: {
            root.offlineMissingConfirmationArmed = false
            catalogAssets.refreshOnlineState()
        }
        onMarkMissingRequested: root.confirmSelectedOriginalPermanentlyMissing()
    }
}
