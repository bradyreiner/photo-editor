import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    objectName: "exportWorkspace"
    color: "transparent"

    Theme { id: theme }

    readonly property bool hasSingleSelection: catalogAssets.selectedAssetId.length > 0
                                               && catalogAssets.selectedFilePath.length > 0
    readonly property bool hasBatchSelection: catalogAssets.selectedAssetCount > 1
    readonly property string exportVisibilityContract: "export-visibility:v1|always:queue-panel-config-panel-selection-preset-destination-primary-action|contextual:preflight-current-image-batch-feedback-empty-queue-progress-errors|one-expansion:export-options-queue-management-destination-reset-clear-finished|hover-focus:queue-row-actions-remove-retry-cancel|diagnostic:job-errors-preflight-blockers-active-progress|preserve:functionality"
    readonly property string exportSurfaceContract: "export-surfaces:v1|queue:jobs-and-progress|config:selection-preset-color-crop-destination|preflight:blockers-visible|primary-action:split-export|queue-management:menu|no-elide:true|no-overlap:true"
    readonly property string exportActionContract: "export-actions:v3|split-button:true|primary:start-current-or-selected|clear:queue-management-only|menu:queue-current,queue-selected,browse-destination,reset-destination|cleanup-menu:clear-finished|disabled-reasons:true|preflight-aware:true|no-clutter:true|label-intent:start-export"
    readonly property string exportActionHierarchyContract: "export-action-hierarchy:v3|primary:start-export-split|primary-menu-clean:true|secondary:queue-management-menu|clear:secondary-cleanup|retry:row-or-queue-menu|destination:secondary-menu|one-primary:true|clear-not-primary:true"
    readonly property string exportWorkflowPolishContract: "export-workflow-polish:v1|first-glance:presets-selection-queue-settings-primary-action|close-inspection:preset-destination-preflight-progress-errors-output-summary|clutter:secondary-management-in-menus-row-actions-contextual|interaction:select-preset-destination-add-queue-start-retry-cancel-clear-reveal|text:no-elide-no-overlap|functionality:preserved"
    readonly property string exportPageCompletionContract: "page-completion:v1|export|sidebar:presets-destinations|content:selected-assets-queue-progress|settings:preset-color-crop-destination-preflight-current-image|toolbar:add-assets-start-export-clear-queue|states:empty-ready-warning-blocked-running-complete-failed"

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

    function selectionSummaryText() {
        const m = selectionMetrics
        if (m.count === 0)
            return "No images selected. Select assets in Library to export."
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
            if (presetDetails[i].name === presetName)
                return presetDetails[i]
        }
        return presetDetails[0]
    }

    function overrideRecipeColorSpace(recipeJson, colorSpaceChoice) {
        if (colorSpaceChoice === "From Edit Recipe" || colorSpaceChoice.length === 0)
            return recipeJson
        try {
            let recipe = recipeJson.length > 0 ? JSON.parse(recipeJson) : {}
            recipe.outputColorSpace = colorSpaceChoice
            return JSON.stringify(recipe)
        } catch (e) {
            return recipeJson
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
        return status === "Failed"
                && (jobKind === "asset_export" || jobKind === "timeline_render");
    }

    function canCancelJob(status, jobKind) {
        if (status === "Queued")
            return true;
        if (status === "Running" || status === "Canceling")
            return jobKind === "asset_export" || jobKind === "timeline_render";
        return false;
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
                recipeJson: overrideRecipeColorSpace(String(asset.recipeJson || ""), colorSpace),
                displayLabel: root.baseNameFromPath(sourcePath)
            })
        }
        return assets
    }

    function queueBatchExports() {
        const assets = buildSelectedAssetList()
        if (assets.length === 0)
            return
        const cropPreset = cropConstraintCombo.currentText === "Current" ? "" : cropConstraintCombo.currentText
        const queued = exportJobs.queueBatchExportWithCropConstraint(
            assets,
            presetCombo.currentText,
            destinationField.text.trim(),
            cropPreset,
            0,
            0,
            0,
            0)
        if (queued > 0) {
            batchQueuedFeedback.text = "Queued " + queued + (queued === 1 ? " export." : " exports.")
            batchQueuedFeedback.visible = true
            batchQueuedTimer.restart()
        }
    }

    function queueCurrentSelection() {
        const colorSpace = colorSpaceCombo.currentText
        const recipeJson = overrideRecipeColorSpace(
            catalogAssets.selectedRecipeJson, colorSpace)
        const cropPreset = cropConstraintCombo.currentText === "Current" ? "" : cropConstraintCombo.currentText
        exportJobs.queueExportWithCropConstraint(
                    catalogAssets.selectedAssetId,
                    catalogAssets.selectedMediaType,
                    currentSelectionLabel(),
                    catalogAssets.selectedFilePath,
                    editorPreview.previewSource,
                    recipeJson,
                    presetCombo.currentText,
                    destinationField.text,
                    cropPreset,
                    0,
                    0,
                     0,
                     0)
    }

    function currentPreflight() {
        const colorSpace = colorSpaceCombo.currentText
        const recipeJson = overrideRecipeColorSpace(
            catalogAssets.selectedRecipeJson || "", colorSpace)
        return exportJobs.exportPreflight(
            catalogAssets.selectedMediaType || "photo",
            catalogAssets.selectedFilePath || "",
            editorPreview.previewSource || "",
            recipeJson,
            presetCombo.currentText,
            destinationField.text,
            editorPreview.histogramSampleCount,
            editorPreview.histogramShadowClipCount,
            editorPreview.histogramHighlightClipCount,
            editorPreview.rawClipSampleCount,
            editorPreview.rawShadowClipCount,
            editorPreview.rawHighlightClipCount)
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
        if (batch) {
            if (selectionMetrics.count === 0)
                return "Select images before queueing a batch export."
        } else if (!hasSingleSelection) {
            return "Select one image before queueing an export."
        }
        if (destinationField.text.trim().length === 0)
            return "Choose an export destination first."
        if (!batch && currentPreflight().errorCount > 0)
            return "Resolve preflight issues before queueing this export."
        return ""
    }

    function exportActionMenuItems() {
        return [
            {
                key: "queue-current",
                label: "Start Current Export",
                icon: "export",
                enabled: hasSingleSelection && destinationField.text.trim().length > 0 && currentPreflight().errorCount === 0,
                disabledReason: exportDisabledReason(false),
                tooltip: "Start the current export with the selected preset and destination."
            },
            {
                key: "queue-selected",
                label: "Start Selected Exports",
                icon: "media",
                enabled: selectionMetrics.count > 0 && destinationField.text.trim().length > 0,
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
                enabled: jobQueue.jobCount > 0,
                tooltip: "Remove completed jobs from the background queue."
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

    function jobRowMenuItems(status, jobKind) {
        return [
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
            }
        ]
    }

    function handleJobRowMenuAction(key, jobId, status, jobKind) {
        if (key === "retry" && canRetryJob(status, jobKind)) {
            jobQueue.retryJob(jobId)
            return
        }
        if (key === "cancel" && canCancelJob(status, jobKind) && status !== "Canceling")
            jobQueue.cancelJob(jobId)
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
        if (destinationField.text.length === 0)
            destinationField.text = exportJobs.defaultDestinationDirectory();
    }

    Timer {
        id: batchQueuedTimer
        interval: 3000
        onTriggered: batchQueuedFeedback.visible = false
    }

    RowLayout {
        objectName: "exportMainWorkspace"
        readonly property string surfaceContract: "export-main-workspace:v1|queue-panel:always|configuration-panel:always|primary-action:always|progress-contextual:true|failure-contextual:true|no-elide:true|no-overlap:true"
        anchors.fill: parent
        spacing: 14

        BrandPanel {
            objectName: "exportQueuePanel"
            readonly property string surfaceContract: "export-queue-panel:v1|queue-list:always|empty-state:contextual|management:one-expansion|row-actions:hover-focus|progress:contextual|errors:diagnostic|no-elide:true"
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Background Jobs"
            subtitle: jobQueue.status

            Item {
                objectName: "exportPresetsPanel"
                width: 1
                height: 1
                visible: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: jobQueue.jobCount + (jobQueue.jobCount === 1 ? " job" : " jobs")
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                }

                Item { Layout.fillWidth: true }

                MenuButton {
                    objectName: "exportQueueManagementMenuButton"
                    text: "Clear Done"
                    iconName: "close"
                    tone: "secondary"
                    compact: true
                    active: false
                    enabled: jobQueue.jobCount > 0
                    tooltipText: "Clear completed jobs. Open the menu for queue options."
                    menuItems: root.queueManagementMenuItems()
                    menuWidth: 190
                    onClicked: jobQueue.clearFinishedJobs()
                    onMenuItemTriggered: function(key) { root.handleQueueManagementMenuAction(key) }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        objectName: "exportQueueEmptyState"
                        readonly property string visibilityRole: "contextual|empty-queue-guidance"
                        Layout.fillWidth: true
                        visible: jobQueue.jobCount === 0
                        implicitHeight: 132
                        radius: theme.radius12
                        color: theme.ink900
                        border.width: 1
                        border.color: theme.lineSoft

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 8

                            Label {
                                text: "No background jobs"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 16
                                font.weight: Font.Medium
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Label {
                                text: "Queue exports, imports, and timeline renders all land here."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }

                    Repeater {
                        objectName: "exportQueueRepeater"
                        readonly property string visibilityRole: "always-visible|jobs-progress-retry-cancel-remove"
                        model: jobQueue

                        delegate: ExportQueueRow {
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

                            Layout.fillWidth: true
                            fallbackIconName: jobKind === "timeline_render" ? "timeline" : "export"
                            title: displayLabel
                            subtitle: jobKindLabel(jobKind)
                                      + (presetName && presetName.length > 0 ? "  \u2022  " + presetName : "")
                            destination: jobDestination(outputPath, destinationPath, sourcePath)
                            preset: presetName
                            statusText: status
                            statusTone: root.jobStatusTone(status)
                            busy: status === "Running" || status === "Canceling"
                            showProgress: status === "Queued"
                                          || status === "Running"
                                          || status === "Canceling"
                                          || status === "Completed"
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
                            menuVisible: canRetryJob(status, jobKind) || canCancelJob(status, jobKind)
                            menuEnabled: menuVisible
                            menuItems: root.jobRowMenuItems(status, jobKind)
                            menuWidth: 176
                            menuTooltipText: "Job actions."
                            showRemoveButton: true
                            removeEnabled: status !== "Running" && status !== "Canceling"
                            removeHelperText: "Remove this job from the queue"
                            onPrimaryActionRequested: {
                                if (canRetryJob(status, jobKind))
                                    jobQueue.retryJob(jobId)
                                else if (canCancelJob(status, jobKind) && status !== "Canceling")
                                    jobQueue.cancelJob(jobId)
                            }
                            onMenuItemTriggered: function(key) {
                                root.handleJobRowMenuAction(key, jobId, status, jobKind)
                            }
                            onRemoveRequested: jobQueue.removeJob(jobId)
                        }
                    }
                }
            }
        }

        BrandPanel {
            objectName: "exportConfigurationPanel"
            readonly property string surfaceContract: "export-configuration-panel:v1|selection:always|preset:always|color-space:always|crop:always|destination:always|preflight:contextual|current-image:contextual|primary-actions:always|no-elide:true|no-overlap:true"
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            title: "Export Configuration"
            subtitle: "Format, quality, and destination settings."

            Item {
                objectName: "exportSettingsPanel"
                width: 1
                height: 1
                visible: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
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

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: selectionSummaryColumn.implicitHeight + 20
                            radius: theme.radius12
                            color: theme.alpha(theme.ink900, 0.7)
                            border.width: 1
                            border.color: selectionMetrics.count > 0
                                          ? theme.alpha(theme.accentCool500, 0.3)
                                          : theme.lineSoft

                            ColumnLayout {
                                id: selectionSummaryColumn
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 4

                                Label {
                                    text: selectionMetrics.count > 0
                                          ? selectionMetrics.count + (selectionMetrics.count === 1 ? " asset" : " assets")
                                          : "No selection"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                }

                                Label {
                                    text: root.selectionSummaryText()
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: {
                                        if (selectionMetrics.videos > 0 || selectionMetrics.timelines > 0)
                                            return "Videos and timelines export as original copies regardless of preset."
                                        return ""
                                    }
                                    visible: text.length > 0
                                    color: theme.alpha(theme.accentWarm400, 0.8)
                                    font.family: theme.fontUi
                                    font.pixelSize: 10
                                    font.italic: true
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    InspectorSection {
                        objectName: "exportPresetSection"
                        readonly property string visibilityRole: "always-visible|preset-summary-quality-format"
                        Layout.fillWidth: true
                        title: "Format Preset"

                        ToneComboBox {
                            id: presetCombo
                            Layout.fillWidth: true
                            model: ["Full Resolution", "Archive TIFF", "Web Proof", "Client Preview", "Quick Web", "Original Copy"]
                        }

                        ExportPresetSummary {
                            format: currentPresetDetail().format
                            bitDepth: currentPresetDetail().bitDepth
                            maxEdge: currentPresetDetail().maxEdge
                            quality: currentPresetDetail().quality
                            description: currentPresetDetail().description
                        }
                    }

                    InspectorSection {
                        objectName: "exportColorSpaceSection"
                        readonly property string visibilityRole: "always-visible|color-space-choice-explanation"
                        Layout.fillWidth: true
                        title: "Color Space"

                        ToneComboBox {
                            id: colorSpaceCombo
                            Layout.fillWidth: true
                            model: root.colorSpaceOptions
                        }

                        Label {
                            text: {
                                const choice = colorSpaceCombo.currentText
                                if (choice === "sRGB")
                                    return "Standard for web, email, and screen display. Widest compatibility across devices and browsers."
                                if (choice === "Display P3")
                                    return "Wide color gamut for Apple devices and modern displays. Best for vibrant, color-accurate output on supported screens."
                                if (choice === "Adobe RGB")
                                    return "Wide gamut standard for print production. Use when delivering files to print shops or prepress workflows."
                                return "Uses the color space set in each image's edit recipe. Most images default to sRGB."
                            }
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    InspectorSection {
                        objectName: "exportCropSection"
                        readonly property string visibilityRole: "always-visible|crop-constraint"
                        Layout.fillWidth: true
                        title: "Export Crop"

                        ToneComboBox {
                            id: cropConstraintCombo
                            Layout.fillWidth: true
                            model: ["Current", "Original", "1:1", "4:5", "5:4", "8x10 in", "10x8 in", "A4", "A4 Landscape"]
                        }
                    }

                    InspectorSection {
                        objectName: "exportDestinationSection"
                        readonly property string visibilityRole: "always-visible|destination-field-browse-reset"
                        Layout.fillWidth: true
                        title: "Destination"

                        ToneTextField {
                            id: destinationField
                            Layout.fillWidth: true
                            placeholderText: exportJobs.defaultDestinationDirectory()
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
                        readonly property string visibilityRole: "contextual|single-selection-readiness-blockers-soft-proof"
                        Layout.fillWidth: true
                        visible: hasSingleSelection && !hasBatchSelection
                        title: "Preflight"

                        property var preflight: root.currentPreflight()

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                objectName: "exportPreflightStatusLabel"
                                text: exportPreflightSection.preflight.ok
                                      ? "Ready"
                                      : exportPreflightSection.preflight.errorCount + " blocking"
                                color: exportPreflightSection.preflight.ok ? theme.accentCool400 : theme.accentWarm400
                                font.family: theme.fontMono
                                font.pixelSize: 11
                                font.weight: Font.Medium
                            }

                            Label {
                                Layout.fillWidth: true
                                text: exportPreflightSection.preflight.format + "  " +
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
                            visible: exportPreflightSection.preflight.softProofEnabled
                            text: "Soft proof " + exportPreflightSection.preflight.softProofProfile
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            Layout.fillWidth: true
                        }

                        Repeater {
                            objectName: "exportPreflightIssueRepeater"
                            model: exportPreflightSection.preflight.issues || []

                            delegate: ExportPreflightIssueRow {
                                issue: modelData
                            }
                        }
                    }

                    InspectorSection {
                        objectName: "exportCurrentImageSection"
                        readonly property string visibilityRole: "contextual|current-selection-name-media-type"
                        Layout.fillWidth: true
                        visible: hasSingleSelection && !hasBatchSelection
                        title: "Current Image"

                        Label {
                            text: currentSelectionLabel()
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Label {
                            text: catalogAssets.selectedMediaType
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            Layout.fillWidth: true
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
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            SplitButton {
                objectName: "exportBatchSplitButton"
                Layout.fillWidth: true
                visible: hasBatchSelection
                text: "Start Selected"
                tone: "primary"
                iconName: "export"
                tooltipText: "Start " + selectionMetrics.count + (selectionMetrics.count === 1 ? " selected export." : " selected exports.")
                primaryEnabled: selectionMetrics.count > 0 && destinationField.text.trim().length > 0
                disabledReason: root.exportDisabledReason(true)
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
                primaryEnabled: hasSingleSelection && destinationField.text.trim().length > 0
                                && root.currentPreflight().errorCount === 0
                disabledReason: root.exportDisabledReason(false)
                menuItems: root.exportActionMenuItems()
                menuWidth: 210
                menuTooltipText: "Export options"
                onClicked: root.queueCurrentSelection()
                onMenuItemTriggered: function(key) { root.handleExportMenuAction(key) }
            }

            ActionButton {
                Layout.fillWidth: true
                visible: !hasSingleSelection && !hasBatchSelection
                text: "Select Images"
                tooltipText: "Select images in Library to export."
                enabled: false
            }
        }
    }
}
