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
        property bool activityVisible: false
    }
    property bool exportSettingsReady: false
    property bool exportPreviewVisible: true
    property bool exportActivityVisible: false
    readonly property bool compactDeliveryLayout: width < theme.scaled(860)
    readonly property bool deliveryActivityNeedsAttention: {
        if (!jobQueue)
            return false
        jobQueue.jobCount
        jobQueue.status
        const jobs = Array.from(jobQueue.jobsForCenter())
        const jobNeedsAttention = jobs.some(function(job) {
            return job.status === "Running" || job.status === "Canceling" ||
                   job.status === "Failed" || job.status === "Save unconfirmed"
        })
        return jobQueue.activeJobCount > 0 || jobNeedsAttention || root.unconfirmedExports.length > 0 ||
               exportJobs.admissionError.length > 0 || exportJobs.submissionError.length > 0 ||
               jobQueue.historyError
    }
    readonly property bool deliveryHasFailures: {
        if (!jobQueue) return false
        jobQueue.jobCount; jobQueue.status
        return Array.from(jobQueue.jobsForCenter()).some(function(job) {
            return job.status === "Failed" || job.status === "Save unconfirmed"
        }) || exportJobs.admissionError.length > 0 || exportJobs.submissionError.length > 0
    }
    readonly property bool exportActivityContextOpen: deliveryActivityNeedsAttention
    readonly property bool exportActivityOpen: exportActivityVisible || exportActivityContextOpen
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
            { key: "activity", label: "Keep delivery activity open", icon: "scope", checked: exportActivityVisible,
              tooltip: "Active jobs and errors open Delivery activity automatically. This choice controls its idle visibility." }
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
            if (String(combo.valueAt(index)) === requestedText ||
                    String(combo.textAt(index)) === requestedText)
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
                preset: presetCombo.currentValue,
                jpegQuality: jpegQualityField.value,
                maxEdge: maxEdgeField.value,
                sharpening: sharpeningCombo.currentText,
                colorSpace: colorSpaceCombo.currentText,
                crop: cropConstraintCombo.currentText,
                destination: root.deliveryDestinationPath,
                filenameTemplate: filenameTemplateField.text
            })
        }
    }

    function scheduleDeliverySettingsSave() {
        if (exportSettingsReady)
            saveDeliverySettingsTimer.restart()
    }

    function destinationMenuItems() {
        return [
            {
                key: "edit-path",
                label: "Edit full path",
                icon: "edit",
                enabled: deliveryDestinationPath.length > 0,
                disabledReason: "Choose a destination folder before editing its path."
            },
            {
                key: "copy-path",
                label: "Copy full path",
                icon: "copy",
                enabled: deliveryDestinationPath.length > 0,
                disabledReason: "Choose a destination folder before copying its path."
            },
            {
                key: "reset-destination",
                label: "Reset destination",
                icon: "reset",
                enabled: true,
                tooltip: "Restore the default export folder."
            }
        ]
    }

    function handleDestinationMenuAction(key) {
        if (key === "edit-path")
            beginDestinationEdit(destinationEditButton.focusedMenuOpener())
        else if (key === "copy-path")
            appShell.copyTextToClipboard(deliveryDestinationPath)
        else if (key === "reset-destination")
            resetDestination()
    }

    function beginDestinationEdit(opener) {
        destinationEditDraft = deliveryDestinationPath
        destinationField.text = destinationEditDraft
        destinationEditFocusTarget = opener || destinationEditButton.focusedMenuOpener()
        destinationEditing = true
        Qt.callLater(function() {
            destinationField.forceActiveFocus()
            destinationField.selectAll()
        })
    }

    function cancelDestinationEdit() {
        destinationEditing = false
        destinationEditDraft = deliveryDestinationPath
        destinationField.text = destinationEditDraft
        returnDestinationEditFocus()
    }

    function returnDestinationEditFocus() {
        const target = destinationEditFocusTarget || destinationEditButton.focusedMenuOpener()
        destinationEditFocusTarget = null
        Qt.callLater(function() {
            if (target && target.visible && target.enabled)
                target.forceActiveFocus(Qt.TabFocusReason)
            else
                destinationChangeButton.forceActiveFocus(Qt.TabFocusReason)
        })
    }

    function commitDestinationEdit() {
        deliveryDestinationPath = String(destinationEditDraft || "").trim()
        destinationEditing = false
        destinationEditDraft = deliveryDestinationPath
        destinationField.text = destinationEditDraft
        destinationField.focus = false
        returnDestinationEditFocus()
        scheduleDeliverySettingsSave()
    }

    function resetDestination() {
        deliveryDestinationPath = exportJobs.defaultDestinationDirectory()
        cancelDestinationEdit()
    }

    readonly property bool historyMutationsEnabled: jobQueue.historyMutationsEnabled
    readonly property string recoveryMutationReason: "Resolve job recovery before changing history. Both versions are preserved."
    readonly property var unconfirmedExports: exportJobs.jobs.filter(function(job) {
        return job.status === "Saving" || job.status === "Save unconfirmed"
    })

    readonly property bool hasSingleSelection: catalogAssets.selectedAssetId.length > 0
                                               && catalogAssets.selectedFilePath.length > 0
    readonly property bool hasBatchSelection: catalogAssets.selectedAssetCount > 1
    readonly property string exportVisibilityContract: "export-visibility:v3|always:config-panel-selection-preset-destination-primary-action-layout-menu|activity:idle-collapsed-active-or-errors-auto-open|activity-preference:persisted-user-choice-not-mutated-by-context|contextual:preflight-batch-feedback-empty-queue-progress-errors|one-expansion:export-options-queue-management-destination-reset-clear-finished|hover-focus:queue-row-actions-remove-retry-cancel|diagnostic:job-errors-preflight-blockers-active-progress|preserve:functionality"
    readonly property string exportSurfaceContract: "export-surfaces:v4|queue:jobs-and-progress|config:selection-preset-destination|advanced:filename-color-crop|preset-summary:one-compact-line|destination:folder-name-change-overflow|paths:basename-tooltip-full|preflight:blockers-visible|primary-action:split-export|queue-management:menu|no-overlap:true"
    readonly property string exportActionContract: "export-actions:v3|split-button:true|primary:start-current-or-selected|clear:queue-management-only|menu:queue-current,queue-selected,browse-destination,reset-destination|cleanup-menu:clear-finished|disabled-reasons:true|preflight-aware:true|no-clutter:true|label-intent:start-export"
    readonly property string exportActionHierarchyContract: "export-action-hierarchy:v4|primary:single-action-batch-alternative|primary-menu-clean:true|secondary:queue-management-menu|clear:secondary-cleanup|retry:row-or-queue-menu|destination:secondary-menu|one-primary:true|clear-not-primary:true"
    readonly property string exportWorkflowPolishContract: "export-workflow-polish:v5|first-glance:photo-preset-destination-primary-action|advanced:filename-color-crop|preset:one-compact-line|destination:folder-name-change-overflow-full-path-edit-reset-copy|activity:idle-collapsed-active-errors-context-open|secondary:preview-queue-progress|inspection:preflight-errors-output-summary|paths:basename-at-rest-explicit-full-path-edit|text:ui-font-readable-full-path-access-no-overlap|functionality:preserved"
    readonly property string exportPageCompletionContract: "page-completion:v5|export|leading:photo-activity|trailing:preset-destination-primary|advanced:filename-color-crop|preset:one-compact-line|secondary:preview-queue-progress|activity:idle-collapsed-active-errors-open|settings:preflight-errors-summary|actions:start-export-queue-retry-cancel-clear-reveal|destination:basename-change-overflow-edit-path-reset|states:empty-ready-warning-blocked-running-complete-failed"
    readonly property string deliveryExportContract: "delivery-export:v1|preflight:destination-filenames-collisions-missing-unsupported-unavailable|progress:per-item-overall-nonblocking|actions:pause-resume-cancel-retry-failed-reveal|summary:profile-dimensions-format-metadata-sharpening|collision:numbered-preserve-existing"
    readonly property string persistentRenderQueueContract: "persistent-render-queue:v1|jobs:asset-export-and-timeline-render|storage:job-store|ordering:priority-then-manual-order|actions:move-priority-pause-resume-cancel-retry-duplicate-reveal|recovery:explicit-confirmation|ui:progressive-row-menu"
    readonly property string backgroundExportContract: "background-export:v1|editing:concurrent|policies:playback-first-balanced-export-first|resources:thread-limit-and-os-priority-only|preview-quality:unchanged|output-settings:unchanged|default:playback-first|ui:progressive-row-menu"
    readonly property string offlineRecoveryContract: "offline-recovery:v1|surface:export|states:drive-offline,moved-file,permission-denied,permanently-missing|preserve:metadata-thumbnail-recipe-identity|actions:recheck-relink-selected-relink-all-mark-missing|default:relink-not-delete|blocked-jobs:auto-resume-after-relink"
    property bool offlineMissingConfirmationArmed: false
    property string deliveryDestinationPath: ""
    property string destinationEditDraft: ""
    property bool destinationEditing: false
    property Item destinationEditFocusTarget: null
    property alias deliveryPresetIndex: presetCombo.currentIndex

    onDeliveryDestinationPathChanged: {
        if (!destinationEditing)
            destinationEditDraft = deliveryDestinationPath
        scheduleDeliverySettingsSave()
    }

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
        const normalizedPath = String(path || "").replace(/[\\/]+$/, "")
        if (normalizedPath.length === 0)
            return "";
        const parts = normalizedPath.split(/[\\/]/);
        return parts.length > 0 ? parts[parts.length - 1] : normalizedPath;
    }

    function folderNameFromPath(path) {
        const folderName = fileNameFromPath(path)
        return folderName.length > 0 ? folderName : String(path || "")
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
                return "Example: " + fileNameFromPath(String(items[0].outputPath || "Pending")) +
                       (items.length > 1 ? "  •  +" + (items.length - 1) + " more" : "")
        }
        return "Example: " + fileNameFromPath(
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
        const presetName = presetCombo.currentValue
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

    readonly property bool jpegPreset: presetCombo.currentValue === "Web Proof" ||
                                       presetCombo.currentValue === "Client Preview" ||
                                       presetCombo.currentValue === "Quick Web"

    function deliveryPreset() {
        if (!jpegPreset)
            return presetCombo.currentValue
        return presetCombo.currentValue + "|quality=" + jpegQualityField.value + "|max-edge=" + maxEdgeField.value +
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
        if (!catalogAssets.selectedAssetsRecipesReady)
            return []
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

    function requestSelectedExportRecipes(retryErrors) {
        if (catalogAssets.selectedAssetsRecipesReady)
            return true
        const pending = []
        const selectedIds = catalogAssets.selectedAssetIds || []
        for (let index = 0; index < selectedIds.length; ++index) {
            const assetId = String(selectedIds[index] || "")
            if (assetId.length === 0 || catalogAssets.assetRecipeReady(assetId) ||
                    (!retryErrors && catalogAssets.assetRecipeLoadError(assetId).length > 0))
                continue
            pending.push(assetId)
        }
        if (pending.length > 0)
            catalogAssets.requestAssetRecipes(pending)
        return false
    }

    function selectedRecipeLoadFailed() {
        const selectedIds = catalogAssets.selectedAssetIds || []
        for (let index = 0; index < selectedIds.length; ++index) {
            const assetId = String(selectedIds[index] || "")
            if (!catalogAssets.assetRecipeReady(assetId) &&
                    catalogAssets.assetRecipeLoadError(assetId).length > 0)
                return true
        }
        return false
    }

    function currentSubmissionAssetList() {
        if (!hasSingleSelection)
            return []
        if (catalogAssets.selectedMediaType === "photo" && !catalogAssets.selectedRecipeReady) {
            catalogAssets.requestAssetRecipes([catalogAssets.selectedAssetId])
            return []
        }
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
        if (destinationEditing || !hasSingleSelection || deliveryDestinationPath.trim().length === 0 ||
                exportJobs.batchSubmissionBusy)
            return false
        if (catalogAssets.selectedMediaType === "photo" && !catalogAssets.selectedRecipeReady)
            return false
        const report = exportJobs.singlePreflight
        // The visible review may take longer than the user wants to wait. The
        // submission path does its own fresh checks before admitting a job.
        return exportJobs.preflightBusy || report.ok === undefined ||
               Number(report.errorCount || 0) === 0
    }

    function queueBatchExports() {
        if (destinationEditing || exportJobs.batchSubmissionBusy || exportJobs.preflightBusy)
            return
        if (!requestSelectedExportRecipes(true)) {
            return
        }
        if (currentBatchPreflight().readyCount <= 0)
            return
        const assets = buildSelectedAssetList()
        if (assets.length === 0)
            return
        const cropPreset = cropConstraintCombo.currentText === "Current" ? "" : cropConstraintCombo.currentText
        const accepted = exportJobs.queueBatchExportAsync(
            assets,
            root.deliveryPreset(),
            deliveryDestinationPath.trim(),
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
            assets, root.deliveryPreset(), deliveryDestinationPath.trim(), cropPreset, 0, 0, 0, 0)
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
        catalogAssets.selectedAssetsRecipesReady
        preflightCatalogRevision
        return visible && catalogAssets.selectedAssetCount > 0 && catalogAssets.selectedAssetsRecipesReady
               ? buildSelectedAssetList() : []
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
        destination: deliveryDestinationPath.trim(),
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
        if (visible && exportSettingsReady && !requestSelectedExportRecipes(false)) {
            return
        }
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
        function onRecipeReadinessChanged() { root.preflightCatalogRevision += 1 }
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
        return "export-workflow-polish:v4" +
               "|selection:" + String(selectionMetrics.count) +
               "|single:" + String(hasSingleSelection) +
               "|preset:" + String(presetCombo.currentValue || "") +
               "|destination:" + String(deliveryDestinationPath.trim().length > 0) +
               "|destination-editing:" + String(destinationEditing) +
               "|destination-draft-pending:" + String(destinationEditing &&
                                                        destinationEditDraft !== deliveryDestinationPath) +
               "|activity-preference:" + String(exportActivityVisible) +
               "|activity-context-open:" + String(exportActivityContextOpen) +
               "|activity-open:" + String(exportActivityOpen) +
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
        return "export-action-hierarchy:v4" +
               "|primary-visible:" + String(hasSingleSelection || hasBatchSelection) +
               "|primary:single-action-batch-alternative" +
               "|primary-menu-clean:" + String(actionItems.map(function(item) { return String(item.key || "") }).indexOf("clear-finished") < 0) +
               "|batch-split:" + String(hasBatchSelection) +
               "|current-action:" + String(hasSingleSelection && !hasBatchSelection) +
               "|secondary:queue-management-menu" +
               "|queue-menu:" + queueItems.map(function(item) { return String(item.key || "") }).join(",") +
               "|action-menu:" + actionItems.map(function(item) { return String(item.key || "") }).join(",") +
               "|clear:secondary-cleanup"
    }

    function exportDisabledReason(batch) {
        if (exportJobs.batchSubmissionBusy)
            return "Checking export request."
        if (destinationEditing)
            return "Finish editing the destination path before exporting."
        if (batch && exportJobs.preflightBusy)
            return "Checking export settings."
        if (batch) {
            if (selectionMetrics.count === 0)
                return "Select images before queueing a batch export."
            if (!catalogAssets.selectedAssetsRecipesReady)
                return selectedRecipeLoadFailed()
                       ? "A saved edit could not be loaded. Retry the affected photos in Library."
                       : "Loading saved edit recipes for the selected photos."
        } else if (!hasSingleSelection) {
            return "Select one image before queueing an export."
        } else if (catalogAssets.selectedMediaType === "photo" && !catalogAssets.selectedRecipeReady) {
            return catalogAssets.assetRecipeLoadError(catalogAssets.selectedAssetId).length > 0
                   ? "The saved edit could not be loaded. Retry this photo in Library."
                   : "Loading the photo's saved edit recipe."
        }
        if (deliveryDestinationPath.trim().length === 0)
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

    function exportActionHint() {
        const hint = exportActionGuidance()
        if (hint.length === 0)
            return ""
        if (hasSingleSelection && !hasBatchSelection && Number(currentPreflight().errorCount || 0) > 0)
            return "Review Preflight checks for the blocking issue."
        if (hasBatchSelection && Number(currentBatchPreflight().blockedCount || 0) > 0 &&
                hint === "No selected delivery item is ready to export.")
            return "Review blocked files in Preflight checks."
        return hint
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
        // Batch export is the primary action; its menu offers the single-photo
        // alternative. Destination commands stay with the destination field.
        return root.hasBatchSelection ? [{
            key: "queue-current",
            label: "Export current photo",
            icon: "export",
            enabled: currentExportActionAvailable(),
            disabledReason: exportDisabledReason(false),
            tooltip: "Export only the current photo with these settings."
        }] : []
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
        if (selectedFolder.length > 0) {
            deliveryDestinationPath = selectedFolder
            if (destinationEditing) {
                destinationEditFocusTarget = destinationChangeButton
                cancelDestinationEdit()
            } else {
                Qt.callLater(function() { destinationChangeButton.forceActiveFocus(Qt.TabFocusReason) })
            }
        }
    }

    function reviewFailedJob() {
        const flickable = exportQueueScroll.contentItem
        for (let index = 0; index < exportQueueRows.count; ++index) {
            const row = exportQueueRows.itemAt(index)
            if (!row || row.status !== "Failed") continue
            if (flickable && flickable.contentY !== undefined)
                flickable.contentY = Math.max(0, Math.min(row.y, flickable.contentHeight - flickable.height))
            row.forceActiveFocus()
            return
        }
    }

    function handleExportMenuAction(key) {
        if (key === "queue-current") {
            queueCurrentSelection()
        } else if (key === "queue-selected") {
            queueBatchExports()
        } else if (key === "browse-destination") {
            chooseDestinationFolder()
        } else if (key === "reset-destination") {
            resetDestination()
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
                deliveryDestinationPath = String(saved.destination)
        }
        if (deliveryDestinationPath.length === 0)
            deliveryDestinationPath = exportJobs.defaultDestinationDirectory()
        destinationEditDraft = deliveryDestinationPath
        exportSettingsReady = true
        refreshExportPreflight(true)
        scheduleDeliverySettingsSave()
    }

    Connections {
        target: presetCombo
        function onCurrentIndexChanged() {
            if (presetCombo.currentValue === "Quick Web") {
                jpegQualityField.value = 88
                maxEdgeField.value = 1440
            } else if (presetCombo.currentValue === "Client Preview") {
                jpegQualityField.value = 92
                maxEdgeField.value = 2048
            } else if (presetCombo.currentValue === "Web Proof") {
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

    GridLayout {
        objectName: "exportMainWorkspace"
        readonly property string surfaceContract: "export-main-workspace:v4|preview-queue:leading-column|settings:trailing-column|compact:stacked|preview:configurable|queue-panel:configurable|configuration-panel:always|progress-contextual:true|failure-contextual:true|paths:basename-tooltip-full|no-overlap:true"
        anchors.fill: parent
        columns: root.compactDeliveryLayout ? 1 : 2
        columnSpacing: theme.space16
        rowSpacing: 0

        ColumnLayout {
            Layout.row: 0
            Layout.column: 0
            visible: root.exportPreviewVisible || root.exportActivityOpen
            Layout.fillWidth: visible
            Layout.fillHeight: !root.compactDeliveryLayout
            Layout.preferredHeight: root.compactDeliveryLayout ? Math.min(theme.scaled(220), root.height * 0.3) : -1
            spacing: 10

            BrandPanel {
                objectName: "exportPreviewPanel"
                visible: root.exportPreviewVisible && (!root.compactDeliveryLayout || !root.exportActivityOpen)
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: root.hasSingleSelection ? root.currentSelectionLabel() : root.hasBatchSelection ? "Selected photos" : "Export"
                subtitle: root.hasBatchSelection ? root.selectionSummaryText() : ""

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
                        id: exportPreviewImage
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
                    OpticalActivityIndicator {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 14
                        width: 18
                        height: 18
                        visible: exportPreviewImage.visible && exportPreviewImage.status === Image.Loading
                        active: visible
                        Accessible.name: "Loading export preview"
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: exportPreviewImage.visible && exportPreviewImage.status === Image.Error
                        text: "Preview unavailable"
                        color: theme.textMuted
                        font.pixelSize: theme.typeCaption
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
                            text: "Choose photos to export"
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
            visible: root.exportActivityOpen
            readonly property string surfaceContract: "export-queue-panel:v1|queue-list:always|empty-state:contextual|management:one-expansion|row-actions:hover-focus|progress:contextual|errors:diagnostic|no-elide:true"
            Layout.fillWidth: true
            Layout.fillHeight: !root.exportPreviewVisible || root.compactDeliveryLayout
            Layout.preferredHeight: root.deliveryHasFailures ? theme.scaled(260)
                                    : jobQueue.jobCount > 1 || jobQueue.activeJobCount > 0 || root.unconfirmedExports.length > 0
                                    ? theme.scaled(220) : jobQueue.jobCount > 0 ? theme.scaled(170) : theme.scaled(100)
            Layout.minimumHeight: 110
            headerVisible: false
            bodyMargin: theme.panelPadding
            bodySpacing: theme.gapControl
            title: "Activity"
            subtitle: ""

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

                ActionButton {
                    objectName: "exportReviewFailedJobsButton"
                    text: "Review failed"
                    iconName: "warning"
                    compact: true
                    visible: {
                        jobQueue.jobCount
                        jobQueue.status
                        return Array.from(jobQueue.jobsForCenter()).some(function(job) { return job.status === "Failed" })
                    }
                    onClicked: root.reviewFailedJob()
                }

                MenuButton {
                    objectName: "exportQueueManagementMenuButton"
                    visible: jobQueue.jobCount > 0
                    text: "Clear finished"
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
                        text: Number(exportOverallProgress.summary.terminal || 0) + " of " +
                              Number(exportOverallProgress.summary.total || 0) + " finished"
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                    }

                    Label {
                        text: Number(exportOverallProgress.summary.failed || 0) > 0
                              ? Number(exportOverallProgress.summary.failed) + " failed" : ""
                        color: theme.error
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                    }
                }



                ProgressStrip {
                    objectName: "exportDeliveryProgressStrip"
                    Layout.fillWidth: true
                    visible: active || failed
                    active: Number(exportOverallProgress.summary.terminal || 0) < Number(exportOverallProgress.summary.total || 0)
                    failed: Number(exportOverallProgress.summary.failed || 0) > 0
                    progress: Number(exportOverallProgress.summary.overallPercent || 0) / 100
                }
            }

            ScrollView {
                id: exportQueueScroll
                objectName: "exportQueueScrollView"
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
                        id: exportQueueRows
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
            readonly property string surfaceContract: "export-configuration-panel:v4|selection:always|preset:always|color-space:more-options|crop:more-options|filename:more-options|destination:basename-at-rest-explicit-full-path-edit|preflight:contextual-path:basename-tooltip|primary-actions:always|no-overlap:true"
            Layout.row: root.compactDeliveryLayout ? 1 : 0
            Layout.column: root.compactDeliveryLayout ? 0 : 1
            Layout.preferredWidth: root.compactDeliveryLayout ? -1 : theme.scaled(360)
            Layout.fillWidth: root.compactDeliveryLayout || (!root.exportPreviewVisible && !root.exportActivityOpen)
            Layout.fillHeight: true
            title: "Export photos"
            subtitle: root.hasBatchSelection ? root.selectionSummaryText() : ""

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
                text: "View"
                iconName: "panel-right"
                primaryOpensMenu: true
                tooltipText: "Choose visible Export panels"
                menuItems: root.exportLayoutMenuItems()
                onMenuItemTriggered: function(key) { root.handleExportLayoutMenu(key) }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.maximumWidth: root.exportPreviewVisible || root.exportActivityOpen ? 16777215 : 640
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
                            text: "Videos and timelines are copied as originals."
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
                        title: "Format & size"

                        ToneComboBox {
                            id: presetCombo
                            objectName: "exportPresetCombo"
                            Layout.fillWidth: true
                            accessibleLabel: qsTr("Export preset")
                            textRole: "label"
                            valueRole: "value"
                            model: [
                                { label: "Full size PNG", value: "Full Resolution" },
                                { label: "Archive TIFF", value: "Archive TIFF" },
                                { label: "EXR Half", value: "OpenEXR Half" },
                                { label: "ACES2065-1", value: "ACES2065-1 OpenEXR" },
                                { label: "ACEScg", value: "ACEScg OpenEXR" },
                                { label: "HDR PQ", value: "HDR PQ AVIF" },
                                { label: "HDR HLG", value: "HDR HLG AVIF" },
                                { label: "Web Proof", value: "Web Proof" },
                                { label: "Client Preview", value: "Client Preview" },
                                { label: "Quick Web", value: "Quick Web" },
                                { label: "Original Copy", value: "Original Copy" }
                            ]
                        }

                        ExportPresetSummary {
                            visible: !root.jpegPreset
                            format: currentPresetDetail().format
                            bitDepth: currentPresetDetail().bitDepth
                            maxEdge: currentPresetDetail().maxEdge
                            quality: currentPresetDetail().quality
                            description: currentPresetDetail().description
                            showDescription: false
                            compactLine: true
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            visible: root.jpegPreset
                            columns: 2
                            columnSpacing: theme.space12
                            rowSpacing: theme.space6

                            Label {
                                Layout.fillWidth: true
                                text: "JPEG quality"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                            }
                            ToneSpinBox {
                                id: jpegQualityField
                                objectName: "exportJpegQualityField"
                                Layout.preferredWidth: theme.scaled(112)
                                accessibleLabel: qsTr("JPEG quality")
                                from: 1
                                to: 100
                                value: 94
                                valueUnit: "%"
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "Long edge"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                            }
                            ToneSpinBox {
                                id: maxEdgeField
                                objectName: "exportMaxEdgeField"
                                Layout.preferredWidth: theme.scaled(112)
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
                                font.pixelSize: theme.typeMicro
                            }
                            ToneComboBox {
                                id: sharpeningCombo
                                objectName: "exportOutputSharpeningCombo"
                                accessibleLabel: qsTr("Output sharpening")
                                Layout.fillWidth: true
                                model: ["Off", "Screen", "Print"]
                            }
                        }
                    }





                    InspectorSection {
                        objectName: "exportDestinationSection"
                        readonly property string visibilityRole: "always-visible|folder-name-browse-reset|explicit-full-path-edit"
                        Layout.fillWidth: true
                        title: "Destination"

                        RowLayout {
                            objectName: "exportDestinationDisplayRow"
                            Layout.fillWidth: true
                            visible: !root.destinationEditing

                            Label {
                                id: destinationNameLabel
                                objectName: "exportDestinationNameLabel"
                                Layout.fillWidth: true
                                text: root.folderNameFromPath(root.deliveryDestinationPath) || "Choose a folder"
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeControl
                                wrapMode: Text.NoWrap
                                elide: Text.ElideMiddle
                                Accessible.name: "Export destination folder"
                                Accessible.description: root.deliveryDestinationPath

                                HoverHandler { id: destinationNameHover }
                                AppToolTip {
                                    targetItem: destinationNameLabel
                                    visible: destinationNameHover.hovered && root.deliveryDestinationPath.length > 0
                                    text: root.deliveryDestinationPath
                                }
                            }

                            ActionButton {
                                id: destinationChangeButton
                                objectName: "exportDestinationChangeButton"
                                compact: true
                                text: "Change"
                                tooltipText: "Choose a different export folder."
                                onClicked: root.chooseDestinationFolder()
                            }

                            MenuButton {
                                id: destinationEditButton
                                objectName: "exportDestinationMoreMenuButton"
                                primaryOpensMenu: true
                                showMenuIndicator: false
                                secondaryActionMenu: true
                                compact: true
                                text: "More"
                                showText: false
                                iconName: "more"
                                tooltipText: "Destination options"
                                menuTooltipText: "Destination options"
                                menuItems: root.destinationMenuItems()
                                menuWidth: 210
                                onMenuItemTriggered: function(key) { root.handleDestinationMenuAction(key) }
                            }
                        }

                        RowLayout {
                            objectName: "exportDestinationEditRow"
                            Layout.fillWidth: true
                            visible: root.destinationEditing

                            ToneTextField {
                                id: destinationField
                                objectName: "exportDestinationField"
                                Layout.fillWidth: true
                                text: ""
                                placeholderText: root.folderNameFromPath(root.deliveryDestinationPath)
                                Accessible.name: "Full export destination path"
                                Accessible.description: "Full output path. Select and copy to reuse it. Press Return to apply or Escape to cancel."
                                onTextChanged: {
                                    if (root.destinationEditing)
                                        root.destinationEditDraft = text
                                }
                                Keys.onEscapePressed: function(event) {
                                    root.cancelDestinationEdit()
                                    event.accepted = true
                                }
                                Keys.onReturnPressed: function(event) {
                                    root.commitDestinationEdit()
                                    event.accepted = true
                                }
                                Keys.onEnterPressed: function(event) {
                                    root.commitDestinationEdit()
                                    event.accepted = true
                                }

                                AppToolTip {
                                    targetItem: destinationField
                                    visible: destinationField.hovered && destinationField.text.length > 0
                                    text: destinationField.text
                                }
                            }

                            ColumnLayout {
                                spacing: theme.space4

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space6

                                    ActionButton {
                                        objectName: "exportDestinationDoneButton"
                                        compact: true
                                        text: "Done"
                                        onClicked: root.commitDestinationEdit()
                                    }

                                    ActionButton {
                                        objectName: "exportDestinationCancelButton"
                                        compact: true
                                        text: "Cancel"
                                        onClicked: root.cancelDestinationEdit()
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Return applies this path · Escape cancels"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeMicro
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                            }
                        }
                    }
                    InspectorSection {
                        objectName: "exportAdvancedSection"
                        Layout.fillWidth: true
                        title: "More options"
                        subtitle: ""
                        expandable: true
                        expanded: false
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

                            AppToolTip {
                                targetItem: filenameTemplateField
                                visible: filenameTemplateField.hovered
                                text: "Use {name} and {sequence} in the filename pattern."
                            }
                        }

                        Label {
                            objectName: "exportFilenamePreviewLabel"
                            Layout.fillWidth: true
                            text: root.filenamePreviewText()
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
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
                            readonly property string visibilityRole: "always-visible|color-space-choice"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            title: "Color space"

                            ToneComboBox {
                                id: colorSpaceCombo
                                accessibleLabel: qsTr("Export color space")
                                Layout.fillWidth: true
                                model: root.colorSpaceOptions
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
                    }


                    InspectorSection {
                        id: exportPreflightSection
                        objectName: "exportPreflightSection"
                        readonly property string visibilityRole: "contextual|delivery-destination-filenames-collisions-blockers-profile-metadata-sharpening"
                        Layout.fillWidth: true
                        visible: root.hasSingleSelection || root.hasBatchSelection
                        title: "Preflight checks"
                        expandable: true
                        expanded: !exportJobs.preflightBusy &&
                                  Number(batch ? preflight.blockedCount : preflight.errorCount) > 0

                        readonly property bool batch: root.hasBatchSelection
                        property var preflight: root.currentDeliveryPreflight()

                        // The pinned delivery summary owns visible readiness.
                        // Keep the named preflight probes for workflow automation
                        // without repeating readiness and target names here.
                        Label {
                            objectName: "exportPreflightStatusLabel"
                            visible: false
                            Accessible.ignored: true
                            text: (exportJobs.preflightBusy || exportJobs.batchSubmissionBusy) ? "Checking export settings" : exportPreflightSection.batch
                                  ? (exportPreflightSection.preflight.blockedCount === 0
                                     ? exportPreflightSection.preflight.readyCount + " ready"
                                     : exportPreflightSection.preflight.blockedCount + " blocked")
                                  : (exportPreflightSection.preflight.ok
                                     ? "Ready"
                                     : exportPreflightSection.preflight.errorCount + " blocking")
                        }

                        Label {
                            id: exportPreflightDestinationLabel
                            objectName: "exportPreflightDestinationLabel"
                            visible: false
                            Accessible.ignored: true
                            readonly property string fullPath: exportPreflightSection.batch
                                                                ? String(exportPreflightSection.preflight.destinationPath || "")
                                                                : String(exportPreflightSection.preflight.outputPath || "")
                            Accessible.description: fullPath
                            text: exportPreflightSection.batch
                                      ? "Folder  ·  " + (root.folderNameFromPath(fullPath) || "Choose a folder")
                                      : (root.fileNameFromPath(fullPath) || "Choose a destination")
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                objectName: "exportPreflightTechnicalSummary"
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
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                elide: Text.ElideNone
                            }
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
                            font.pixelSize: theme.typeMicro
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
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeMicro
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
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeMicro
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
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeMicro
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
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
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
                            font.pixelSize: theme.typeMicro
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
                Layout.fillWidth: true
                Layout.preferredHeight: deliverySummaryContent.implicitHeight + theme.space12
                visible: root.hasSingleSelection || root.hasBatchSelection
                radius: 6
                color: "transparent"
                border.width: 0

                ColumnLayout {
                    id: deliverySummaryContent
                    anchors.fill: parent
                    anchors.margins: theme.space6
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            objectName: "exportDeliveryReviewSummaryStatus"
                            Layout.fillWidth: true
                            text: exportJobs.preflightBusy ? "Checking export settings…" : root.hasBatchSelection
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
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                        }

                        Label {
                            text: Number(root.hasBatchSelection
                                         ? exportDeliveryReviewSummaryCard.summary.collisionCount
                                         : (exportDeliveryReviewSummaryCard.summary.collision ? 1 : 0)) > 0
                                  ? "Numbered names" : "No overwrite"
                            color: theme.interaction400
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.DemiBold
                        }
                    }

                    Label {
                        id: exportDeliveryReviewSummaryPath
                        objectName: "exportDeliveryReviewSummaryPath"
                        readonly property string fullPath: root.hasBatchSelection
                                                           ? String(exportDeliveryReviewSummaryCard.summary.destinationPath || "")
                                                           : String(exportDeliveryReviewSummaryCard.summary.outputPath || "")
                        Accessible.description: fullPath
                        Layout.fillWidth: true
                        text: root.hasBatchSelection
                              ? Number(exportDeliveryReviewSummaryCard.summary.assetCount || 0) +
                                (Number(exportDeliveryReviewSummaryCard.summary.assetCount || 0) === 1 ? " file" : " files") +
                                "  →  " + root.folderNameFromPath(fullPath)
                              : root.fileNameFromPath(fullPath)
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.NoWrap
                        elide: Text.ElideMiddle

                        HoverHandler { id: summaryPathHover }
                        AppToolTip {
                            targetItem: exportDeliveryReviewSummaryPath
                            visible: summaryPathHover.hovered
                            text: exportDeliveryReviewSummaryPath.fullPath
                        }
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
                font.pixelSize: theme.typeCaption
                font.weight: Font.Medium
            }

            RowLayout {
                objectName: "exportActionGuidanceRow"
                Layout.fillWidth: true
                visible: (root.hasSingleSelection || root.hasBatchSelection) &&
                         root.exportActionHint().length > 0
                spacing: theme.space8

                Label {
                    objectName: "exportActionGuidanceLabel"
                    Layout.fillWidth: true
                    text: root.exportActionHint()
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
                    visible: !root.destinationEditing && root.exportActionHint().toLowerCase().indexOf("destination") >= 0
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
                      : "Export " + selectionMetrics.count + " photos"
                tone: "primary"
                iconName: "export"
                tooltipText: root.currentBatchPreflight().blockedCount > 0
                             ? "Start ready exports now; blocked items remain in Delivery Review."
                             : "Start " + selectionMetrics.count + (selectionMetrics.count === 1 ? " selected export." : " selected exports.")
                primaryEnabled: !root.destinationEditing && selectionMetrics.count > 0 &&
                                root.deliveryDestinationPath.trim().length > 0
                                && root.currentBatchPreflight().readyCount > 0
                disabledReason: root.exportActionGuidance()
                menuItems: root.exportActionMenuItems()
                menuWidth: 210
                menuTooltipText: "Export options"
                onClicked: root.queueBatchExports()
                onMenuItemTriggered: function(key) { root.handleExportMenuAction(key) }
            }

            ActionButton {
                objectName: "exportCurrentSplitButton"
                Layout.fillWidth: true
                visible: hasSingleSelection && !hasBatchSelection
                text: "Export photo"
                tone: "primary"
                iconName: "export"
                tooltipText: "Start the current export with the selected preset and destination."
                enabled: root.currentExportActionAvailable()
                disabledReason: root.exportActionGuidance()
                onClicked: root.queueCurrentSelection()
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
