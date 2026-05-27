import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    color: "transparent"

    Theme { id: theme }

    readonly property bool hasSingleSelection: catalogAssets.selectedAssetId.length > 0
                                               && catalogAssets.selectedFilePath.length > 0
    readonly property bool hasBatchSelection: catalogAssets.selectedAssetCount > 1

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
        anchors.fill: parent
        spacing: 14

        BrandPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Background Jobs"
            subtitle: jobQueue.status

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

                ActionButton {
                    text: "Clear Finished"
                    enabled: jobQueue.jobCount > 0
                    onClicked: jobQueue.clearFinishedJobs()
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
                        model: jobQueue

                        delegate: Rectangle {
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
                            implicitHeight: detailsColumn.implicitHeight + 22
                            radius: theme.radius12
                            color: theme.ink900
                            border.width: 1
                            border.color: status === "Completed"
                                          ? theme.alpha(theme.accentCool500, 0.55)
                                          : status === "Failed"
                                            ? theme.alpha("#d96a5b", 0.68)
                                            : theme.lineSoft

                            ColumnLayout {
                                id: detailsColumn
                                anchors.fill: parent
                                anchors.margins: 11
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            text: displayLabel
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 14
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            text: jobKindLabel(jobKind)
                                                  + (presetName && presetName.length > 0 ? "  \u2022  " + presetName : "")
                                                  + "  \u2022  " + status
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                    }

                                    ActionButton {
                                        text: "Retry"
                                        visible: canRetryJob(status, jobKind)
                                        onClicked: jobQueue.retryJob(jobId)
                                    }

                                    ActionButton {
                                        text: status === "Canceling" ? "Canceling" : "Cancel"
                                        visible: canCancelJob(status, jobKind)
                                        enabled: status !== "Canceling"
                                        onClicked: jobQueue.cancelJob(jobId)
                                    }

                                    ConfirmActionButton {
                                        text: "Remove"
                                        enabled: status !== "Running" && status !== "Canceling"
                                        helperText: "Remove this job from the queue"
                                        onConfirmed: jobQueue.removeJob(jobId)
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: jobDestination(outputPath, destinationPath, sourcePath)
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                }

                                ProgressStrip {
                                    Layout.fillWidth: true
                                    visible: status === "Queued"
                                             || status === "Running"
                                             || status === "Canceling"
                                             || status === "Completed"
                                    progress: status === "Queued"
                                              ? 0.08
                                              : Math.max(0.05, Math.min(1.0, progress / 100.0))
                                    accent: status === "Completed"
                                            ? theme.accentCool500
                                            : status === "Canceling"
                                              ? theme.alpha("#d96a5b", 0.88)
                                            : status === "Failed"
                                              ? "#d96a5b"
                                              : theme.accentWarm500
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: errorMessage.length > 0
                                    text: errorMessage
                                    color: "#f4b7af"
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }

        BrandPanel {
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            title: "Export Configuration"
            subtitle: "Format, quality, and destination settings."

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    InspectorSection {
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
                        Layout.fillWidth: true
                        title: "Format Preset"

                        ToneComboBox {
                            id: presetCombo
                            Layout.fillWidth: true
                            model: ["Full Resolution", "Archive TIFF", "Web Proof", "Client Preview", "Quick Web", "Original Copy"]
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: presetInfoColumn.implicitHeight + 16
                            radius: theme.radius12
                            color: theme.alpha(theme.ink900, 0.5)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.5)

                            ColumnLayout {
                                id: presetInfoColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    ColumnLayout {
                                        spacing: 1
                                        Label {
                                            text: "Format"
                                            color: theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                        }
                                        Label {
                                            text: currentPresetDetail().format
                                            color: theme.textMain
                                            font.family: theme.fontMono
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                        }
                                    }

                                    ColumnLayout {
                                        spacing: 1
                                        Label {
                                            text: "Depth"
                                            color: theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                        }
                                        Label {
                                            text: currentPresetDetail().bitDepth
                                            color: theme.textMain
                                            font.family: theme.fontMono
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                        }
                                    }

                                    ColumnLayout {
                                        spacing: 1
                                        Label {
                                            text: "Max Size"
                                            color: theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                        }
                                        Label {
                                            text: currentPresetDetail().maxEdge
                                            color: theme.textMain
                                            font.family: theme.fontMono
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                        }
                                    }

                                    ColumnLayout {
                                        spacing: 1
                                        Label {
                                            text: "Quality"
                                            color: theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                        }
                                        Label {
                                            text: currentPresetDetail().quality
                                            color: theme.textMain
                                            font.family: theme.fontMono
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                        }
                                    }
                                }

                                Label {
                                    text: currentPresetDetail().description
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    InspectorSection {
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
                        Layout.fillWidth: true
                        title: "Export Crop"

                        ToneComboBox {
                            id: cropConstraintCombo
                            Layout.fillWidth: true
                            model: ["Current", "Original", "1:1", "4:5", "5:4", "8x10 in", "10x8 in", "A4", "A4 Landscape"]
                        }
                    }

                    InspectorSection {
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
                                onClicked: {
                                    const selectedFolder = exportJobs.pickDestinationFolder()
                                    if (selectedFolder.length > 0)
                                        destinationField.text = selectedFolder
                                }
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
                                      exportPreflightSection.preflight.outputColorSpace + "  " +
                                      (exportPreflightSection.preflight.estimatedPixelWidth > 0
                                       ? exportPreflightSection.preflight.estimatedPixelWidth + " x " +
                                         exportPreflightSection.preflight.estimatedPixelHeight
                                       : "Size pending")
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
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

                            delegate: Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: issueText.implicitHeight + 10
                                radius: 6
                                color: modelData.severity === "error" ? "#2d1718" : "#262214"
                                border.width: 1
                                border.color: modelData.severity === "error" ? "#7f2d34" : "#6f5a25"

                                Label {
                                    id: issueText
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    text: modelData.message
                                    color: modelData.severity === "error" ? "#ffb0b8" : "#f2d18a"
                                    font.family: theme.fontUi
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }

                    InspectorSection {
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

            ActionButton {
                Layout.fillWidth: true
                visible: hasBatchSelection
                text: "Queue " + selectionMetrics.count + " Selected Exports"
                tone: "primary"
                enabled: selectionMetrics.count > 0 && destinationField.text.trim().length > 0
                onClicked: root.queueBatchExports()
            }

            ActionButton {
                Layout.fillWidth: true
                visible: hasSingleSelection && !hasBatchSelection
                text: "Queue Export"
                tone: "primary"
                enabled: hasSingleSelection && destinationField.text.trim().length > 0
                         && root.currentPreflight().errorCount === 0
                onClicked: root.queueCurrentSelection()
            }

            ActionButton {
                Layout.fillWidth: true
                visible: !hasSingleSelection && !hasBatchSelection
                text: "Select images in Library to export"
                enabled: false
            }
        }
    }
}
