import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root

    color: "transparent"
    focus: true

    Theme { id: theme }

    property real manualZoom: 1.0
    property bool fitToView: true
    property bool inspectorCollapsed: false
    property string transientMaskTool: ""
    readonly property bool objectPickArmed: transientMaskTool === "smart" ||
                                            transientMaskTool === "smart-subtract" ||
                                            transientMaskTool === "smart-new" ||
                                            transientMaskTool === "remove" ||
                                            transientMaskTool === "remove-new"
    readonly property bool objectPickCreateNew: transientMaskTool === "smart-new" ||
                                                transientMaskTool === "remove-new"
    readonly property string objectPickMode: transientMaskTool === "smart-subtract"
                                             ? "selection-subtract"
                                             : (transientMaskTool === "remove" || transientMaskTool === "remove-new"
                                                ? "remove"
                                                : "selection")
    readonly property bool polygonDrawArmed: transientMaskTool === "polygon"
    readonly property bool lassoDrawArmed: transientMaskTool === "lasso"
    readonly property bool cloneSourcePickArmed: transientMaskTool === "clone-source"
    property string maskFallbackMode: ""
    property real selectionOutlinePhase: 0
    property string cropGuideMode: "Thirds"
    property string editedDisplaySource: ""
    property string referenceDisplaySource: ""
    property string presetDraftName: ""
    property string selectedPresetName: ""
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
    property bool batchToolsExpanded: false
    onInspectorModeChanged: {
        if (inspectorMode === "crop" && editorPreview.hasAsset)
            editorPreview.cropEditing = true
        else if (inspectorMode !== "crop" && editorPreview.cropEditing)
            editorPreview.cropEditing = false
    }

    function clampZoom(value) {
        return Math.max(0.25, Math.min(4.0, value));
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function clearTransientTools() {
        transientMaskTool = ""
        maskFallbackMode = ""
        editorPreview.clearSmartSelectPreview()
    }

    function deselectMaskEditing() {
        clearTransientTools()
        editorPreview.deselectActiveMask()
    }

    function handleMaskPickOutcome(outcome) {
        maskFallbackMode = ""
        if (outcome === "selection-fallback-polygon") {
            transientMaskTool = "polygon"
            maskFallbackMode = "polygon"
        } else if (outcome === "remove-fallback-brush") {
            transientMaskTool = ""
            maskFallbackMode = "brush"
        }
    }

    function selectTool(mode) {
        clearTransientTools()
        if (mode === "smart" || mode === "smart-subtract" || mode === "smart-new" ||
                mode === "remove" || mode === "remove-new" || mode === "polygon" ||
                mode === "lasso" || mode === "clone-source") {
            transientMaskTool = mode
            inspectorMode = "masks"
        }
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
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "remove")
            return editorPreview.activeMaskCommitted ? "Remove Brush" : "Pending Remove Brush"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskKind === "object" &&
                editorPreview.activeMaskRetouchMode === "remove")
            return editorPreview.activeMaskCommitted ? "Remove Target" : "Pending Remove Target"
        if (editorPreview.activeMaskKind === "polygon")
            return "Polygon Selection"
        if (editorPreview.activeMaskKind === "object")
            return "Object Selection"
        if (editorPreview.activeMaskKind === "subject")
            return "Main Subject"
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
               Math.abs(editorPreview.activeMaskColorizeStrength) > 0.001 ||
               Math.abs(editorPreview.activeMaskRemoveStrength) > 0.001
    }

    function activeMaskSupportsSelectionRefine() {
        return editorPreview.hasActiveMask &&
               (editorPreview.activeMaskKind === "linear" ||
                editorPreview.activeMaskKind === "radial" ||
                editorPreview.activeMaskKind === "brush" ||
                editorPreview.activeMaskKind === "polygon" ||
                editorPreview.activeMaskKind === "subject" ||
                editorPreview.activeMaskKind === "object" ||
                editorPreview.activeMaskKind === "luminance" ||
                editorPreview.activeMaskKind === "color")
    }

    function maskAccent(mask) {
        if (!mask)
            return "#8ab8d8"
        if (mask.operation === "subtract")
            return "#d36a5e"
        if (mask.operation === "intersect")
            return "#6fa96d"
        if (mask.toolRole === "retouch" && mask.retouchMode === "clone")
            return "#7cb5ff"
        if (mask.toolRole === "retouch" && mask.retouchMode === "heal")
            return "#72cbb2"
        if (mask.toolRole === "retouch" && mask.retouchMode === "remove")
            return "#d98962"
        if (mask.kind === "radial")
            return "#8ab8d8"
        if (mask.kind === "brush")
            return "#cf8f4d"
        if (mask.kind === "polygon")
            return "#7ec97a"
        if (mask.kind === "luminance")
            return "#d8d27a"
        if (mask.kind === "color")
            return "#c77dd8"
        if (mask.kind === "subject")
            return "#5fc9b5"
        if (mask.kind === "object")
            return "#5e95d3"
        return "#c99a52"
    }

    function activeMaskResetFeather() {
        if (editorPreview.activeMaskKind === "radial")
            return 0.25
        if (editorPreview.activeMaskKind === "brush")
            return 0.55
        if (editorPreview.activeMaskKind === "polygon")
            return 0.06
        if (editorPreview.activeMaskKind === "subject" || editorPreview.activeMaskKind === "object")
            return 0.5
        return 0.22
    }

    function nudgeActiveSelectionExpand(delta) {
        editorPreview.activeMaskSelectionExpand = clamp(editorPreview.activeMaskSelectionExpand + delta, -100, 100)
    }

    function nudgeActiveMaskFeather(delta) {
        const minimum = editorPreview.activeMaskKind === "polygon" ? 0.0 : 0.05
        const maximum = editorPreview.activeMaskKind === "polygon" ? 0.2 : 1.0
        editorPreview.activeMaskFeather = clamp(
                    editorPreview.activeMaskFeather + delta,
                    minimum,
                    maximum)
    }

    function maxPanX() {
        return Math.max(0, zoomPreviewContent.width - zoomPreviewFlick.width);
    }

    function maxPanY() {
        return Math.max(0, zoomPreviewContent.height - zoomPreviewFlick.height);
    }

    function clampPanPosition() {
        zoomPreviewFlick.contentX = Math.max(0, Math.min(maxPanX(), zoomPreviewFlick.contentX));
        zoomPreviewFlick.contentY = Math.max(0, Math.min(maxPanY(), zoomPreviewFlick.contentY));
    }

    function centerZoomPreview() {
        zoomPreviewFlick.contentX = maxPanX() / 2;
        zoomPreviewFlick.contentY = maxPanY() / 2;
    }

    function updatePreviewTargetEdge() {
        const viewportWidth = Math.max(1, previewSurface.width - 4);
        const viewportHeight = Math.max(1, previewSurface.height - 4);
        const compareWidth = editorPreview.compareMode ? Math.max(1, (viewportWidth - 8) / 2) : viewportWidth;
        const zoomMultiplier = fitToView ? 1.0 : Math.min(2.0, Math.max(1.0, manualZoom));
        editorPreview.previewTargetEdge = Math.round(Math.max(compareWidth, viewportHeight) * Screen.devicePixelRatio * zoomMultiplier);
    }

    function updatePreviewViewport() {
        if (!editorPreview.hasAsset || fitToView || editorPreview.compareMode || !zoomPreviewFlick.visible || !editedPreviewZoom.visible) {
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

    function fitZoomLevel() {
        if (editedDisplaySource.length === 0)
            return 1.0;

        const imageWidth = imagePixelWidth(editedPreviewZoom);
        const imageHeight = imagePixelHeight(editedPreviewZoom);
        const availableWidth = editorPreview.compareMode
                               ? Math.max(1, (fitPreviewRow.width - 8) / 2)
                               : Math.max(1, fitPreviewRow.width);
        const availableHeight = Math.max(1, fitPreviewRow.height);
        return Math.min(availableWidth / imageWidth, availableHeight / imageHeight);
    }

    function currentZoomLevel() {
        return fitToView ? fitZoomLevel() : manualZoom;
    }

    NumberAnimation on selectionOutlinePhase {
        from: 0
        to: 24
        duration: 900
        loops: Animation.Infinite
        running: editorPreview.hasActiveMask || editorPreview.smartSelectPreviewActive
    }

    onManualZoomChanged: {
        updatePreviewTargetEdge()
        updatePreviewViewport()
    }
    onFitToViewChanged: {
        updatePreviewTargetEdge()
        updatePreviewViewport()
    }

    Connections {
        target: editorPreview
        function onCompareModeChanged() {
            updatePreviewTargetEdge();
            updatePreviewViewport();
        }
    }

    function contentWidthForZoom(zoomLevel) {
        const editedWidth = editedDisplaySource.length > 0 ? imagePixelWidth(editedPreviewZoom) * zoomLevel : 0;
        const referenceWidth = editorPreview.compareMode && referenceDisplaySource.length > 0
                               ? imagePixelWidth(referencePreviewZoom) * zoomLevel
                               : 0;
        const combinedWidth = editorPreview.compareMode && referenceWidth > 0
                              ? editedWidth + referenceWidth + 16
                              : editedWidth;
        return Math.max(zoomPreviewFlick.width, combinedWidth);
    }

    function contentHeightForZoom(zoomLevel) {
        const editedHeight = editedDisplaySource.length > 0 ? imagePixelHeight(editedPreviewZoom) * zoomLevel : 0;
        const referenceHeight = editorPreview.compareMode && referenceDisplaySource.length > 0
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

        const viewportPivotX = pivotX === undefined ? zoomPreviewFlick.width / 2 : pivotX;
        const viewportPivotY = pivotY === undefined ? zoomPreviewFlick.height / 2 : pivotY;
        const oldContentWidth = fitToView ? zoomPreviewFlick.width : contentWidthForZoom(currentZoom);
        const oldContentHeight = fitToView ? zoomPreviewFlick.height : contentHeightForZoom(currentZoom);
        const oldFocusX = (fitToView ? 0 : zoomPreviewFlick.contentX) + viewportPivotX;
        const oldFocusY = (fitToView ? 0 : zoomPreviewFlick.contentY) + viewportPivotY;
        const widthRatio = oldFocusX / Math.max(1, oldContentWidth);
        const heightRatio = oldFocusY / Math.max(1, oldContentHeight);

        fitToView = false;
        manualZoom = nextZoom;

        const newContentWidth = contentWidthForZoom(nextZoom);
        const newContentHeight = contentHeightForZoom(nextZoom);
        zoomPreviewFlick.contentX = widthRatio * newContentWidth - viewportPivotX;
        zoomPreviewFlick.contentY = heightRatio * newContentHeight - viewportPivotY;
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
        const hue = ((hueValue / 100.0) % 1.0 + 1.0) % 1.0;
        const saturation = Math.max(0.18, Math.min(1.0, Math.abs(saturationValue) / 100.0));
        const lightness = Math.max(0.38, Math.min(0.76, 0.56 + luminanceValue / 280.0));
        return Qt.hsla(hue, saturation, lightness, 1.0);
    }

    function activeAdjustmentGroups() {
        return selectedAdjustmentGroups.slice(0);
    }

    function hasAdjustmentGroup(groupKey) {
        return selectedAdjustmentGroups.indexOf(groupKey) >= 0;
    }

    function toggleAdjustmentGroup(groupKey) {
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
        catalogAssets.applyRecipeJsonToAssetIds(targetIds, recipeJson, groups);
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
        const recipeJson = editorPreview.presetRecipeJson(presetName);
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
        if (editorPreview.savePreset(presetName, true)) {
            presetDraftName = presetName;
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
            updatePreviewViewport();
        }

        function onCompareModeChanged() {
            if (!fitToView)
                centerZoomPreview();
        }

        function onPresetsChanged() {
            if (selectedPresetName.length > 0 && editorPreview.presetNames.indexOf(selectedPresetName) >= 0)
                return

            selectedPresetName = editorPreview.presetNames.length > 0 ? editorPreview.presetNames[0] : ""
            if (presetDraftName.length === 0 || selectedPresetName.length === 0)
                presetDraftName = selectedPresetName
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

    Component.onCompleted: {
        updatePreviewTargetEdge();
        editedDisplaySource = editorPreview.displayPreviewSource;
        referenceDisplaySource = editorPreview.displayReferencePreviewSource;
        if (editorPreview.presetNames.length > 0) {
            selectedPresetName = editorPreview.presetNames[0];
            presetDraftName = selectedPresetName;
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

            BrandPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                headerVisible: false
                bodyMargin: 2
                bodySpacing: 4
                title: "Editor"

                Rectangle {
                    id: previewSurface
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius12
                    color: theme.ink900
                    clip: true
                    onWidthChanged: {
                        updatePreviewTargetEdge()
                        updatePreviewViewport()
                    }
                    onHeightChanged: {
                        updatePreviewTargetEdge()
                        updatePreviewViewport()
                    }

                    Row {
                        id: canvasToolbar
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.topMargin: 8
                        z: 120
                        spacing: 4
                        visible: inspectorMode === "masks" && editorPreview.hasAsset

                        Rectangle {
                            width: selectToolRow.implicitWidth + 16
                            height: 36
                            radius: 10
                            color: Qt.alpha("#101419", 0.92)
                            border.width: 1
                            border.color: "#2d3339"

                            Row {
                                id: selectToolRow
                                anchors.centerIn: parent
                                spacing: 4

                                ActionButton {
                                    compact: true
                                    text: "Object Select"
                                    active: objectPickArmed && objectPickMode === "selection" && !objectPickCreateNew
                                    enabled: editorPreview.hasAsset
                                    onClicked: selectTool(objectPickArmed && objectPickMode === "selection" && !objectPickCreateNew ? "" : "smart")
                                }

                                ActionButton {
                                    compact: true
                                    text: "Subtract Object"
                                    active: objectPickArmed && objectPickMode === "selection-subtract"
                                    enabled: editorPreview.hasAsset && editorPreview.hasActiveMask
                                    onClicked: selectTool(objectPickArmed && objectPickMode === "selection-subtract" ? "" : "smart-subtract")
                                }

                                ActionButton {
                                    compact: true
                                    text: "Lasso"
                                    active: lassoDrawArmed
                                    onClicked: selectTool(lassoDrawArmed ? "" : "lasso")
                                }

                                ActionButton {
                                    compact: true
                                    text: "Polygon"
                                    active: polygonDrawArmed
                                    onClicked: {
                                        if (!polygonDrawArmed && editorPreview.activeMaskKind !== "polygon")
                                            editorPreview.startPolygonSelection()
                                        selectTool(polygonDrawArmed ? "" : "polygon")
                                    }
                                }

                                Rectangle { width: 1; height: 22; color: "#3a424a"; radius: 1 }

                                ActionButton {
                                    compact: true
                                    text: "Brush Add"
                                    active: editorPreview.activeMaskKind === "brush" &&
                                            editorPreview.activeMaskOperation === "add" &&
                                            editorPreview.activeMaskRetouchMode.length === 0
                                    onClicked: {
                                        clearTransientTools()
                                        editorPreview.startSelectionBrush(false)
                                    }
                                }

                                ActionButton {
                                    compact: true
                                    text: "Brush Erase"
                                    active: editorPreview.activeMaskKind === "brush" &&
                                            editorPreview.activeMaskOperation === "subtract" &&
                                            editorPreview.activeMaskRetouchMode.length === 0
                                    onClicked: {
                                        clearTransientTools()
                                        editorPreview.startSelectionBrush(true)
                                    }
                                }

                            }
                        }

                        Rectangle {
                            visible: editorPreview.hasActiveMask
                            width: canvasToolLabel.implicitWidth + 14
                            height: 36
                            radius: 10
                            color: Qt.alpha("#101419", 0.82)
                            border.width: 1
                            border.color: "#2d3339"

                            Label {
                                id: canvasToolLabel
                                anchors.centerIn: parent
                                text: activeToolLabel()
                                color: "#f2f4f6"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                font.weight: Font.Medium
                            }
                        }
                    }

                    Item {
                        id: fitPreviewRow
                        anchors.fill: parent
                        anchors.margins: 2
                        visible: fitToView

                    Image {
                        id: editedPreviewFit
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: editorPreview.compareMode ? compareFitDivider.left : parent.right
                        anchors.rightMargin: editorPreview.compareMode ? 8 : 0
                        source: editedDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: false
                        visible: editedDisplaySource.length > 0
                    }

                    Rectangle {
                        id: compareFitDivider
                        visible: editorPreview.compareMode && editedPreviewFit.visible
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
                        asynchronous: false
                        cache: false
                        visible: editorPreview.compareMode && referenceDisplaySource.length > 0
                    }

                    CropOverlay {
                        anchors.fill: parent
                        active: fitToView && editorPreview.cropEditing && !editorPreview.compareMode && editedPreviewFit.visible
                        imageX: editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2
                        imageY: editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2
                        imageWidth: Math.max(0, editedPreviewFit.paintedWidth)
                        imageHeight: Math.max(0, editedPreviewFit.paintedHeight)
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

                    MaskCanvasOverlay {
                        anchors.fill: parent
                        visible: fitToView && !editorPreview.compareMode && !editorPreview.cropEditing && editedPreviewFit.visible
                        editRoot: root
                        imageX: editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2
                        imageY: editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2
                        imageWidth: Math.max(0, editedPreviewFit.paintedWidth)
                        imageHeight: Math.max(0, editedPreviewFit.paintedHeight)
                    }
                }

                MouseArea {
                    anchors.fill: fitPreviewRow
                    visible: fitToView && editorPreview.whiteBalanceSampling && !editorPreview.cropEditing && editedPreviewFit.visible
                    cursorShape: Qt.CrossCursor
                    onClicked: function (mouse) {
                        const imageX = editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2;
                        const imageY = editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2;
                        const imageWidth = editedPreviewFit.paintedWidth;
                        const imageHeight = editedPreviewFit.paintedHeight;

                        if (mouse.x < imageX || mouse.x > imageX + imageWidth || mouse.y < imageY || mouse.y > imageY + imageHeight) {
                            editorPreview.whiteBalanceSampling = false;
                            return;
                        }

                        editorPreview.sampleWhiteBalanceAt((mouse.x - imageX) / imageWidth, (mouse.y - imageY) / imageHeight);
                    }
                }

                Flickable {
                    id: zoomPreviewFlick
                    anchors.fill: parent
                    anchors.margins: 2
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    visible: !fitToView
                    contentWidth: zoomPreviewContent.width
                    contentHeight: zoomPreviewContent.height
                    onContentWidthChanged: clampPanPosition()
                    onContentHeightChanged: clampPanPosition()
                    onContentXChanged: updatePreviewViewport()
                    onContentYChanged: updatePreviewViewport()
                    onWidthChanged: updatePreviewViewport()
                    onHeightChanged: updatePreviewViewport()

                    Item {
                        id: zoomPreviewContent
                        width: editorPreview.compareMode
                               ? Math.max(zoomPreviewFlick.width, editedPreviewZoom.width + referencePreviewZoom.width + 16)
                               : Math.max(zoomPreviewFlick.width, editedPreviewZoom.width)
                        height: Math.max(zoomPreviewFlick.height, Math.max(editedPreviewZoom.height, referencePreviewZoom.height))

                        Image {
                            id: editedPreviewZoom
                            x: editorPreview.compareMode
                               ? Math.max(0, (zoomPreviewContent.width - (editedPreviewZoom.width + referencePreviewZoom.width + 16)) / 2)
                               : Math.max(0, (zoomPreviewContent.width - editedPreviewZoom.width) / 2)
                            y: Math.max(0, (zoomPreviewContent.height - editedPreviewZoom.height) / 2)
                            width: imagePixelWidth(editedPreviewZoom) * manualZoom
                            height: imagePixelHeight(editedPreviewZoom) * manualZoom
                            source: editedDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: editedDisplaySource.length > 0
                        }

                        Image {
                            x: editedPreviewZoom.x + editorPreview.displayPreviewOverlayRect.x * editedPreviewZoom.width
                            y: editedPreviewZoom.y + editorPreview.displayPreviewOverlayRect.y * editedPreviewZoom.height
                            width: editorPreview.displayPreviewOverlayRect.width * editedPreviewZoom.width
                            height: editorPreview.displayPreviewOverlayRect.height * editedPreviewZoom.height
                            source: editorPreview.displayPreviewOverlaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: !fitToView && editedPreviewZoom.visible &&
                                     editorPreview.displayPreviewOverlaySource.length > 0 &&
                                     width > 1 && height > 1
                        }

                        Image {
                            id: referencePreviewZoom
                            x: editorPreview.compareMode ? editedPreviewZoom.x + editedPreviewZoom.width + 16 : 0
                            y: Math.max(0, (zoomPreviewContent.height - referencePreviewZoom.height) / 2)
                            width: imagePixelWidth(referencePreviewZoom) * manualZoom
                            height: imagePixelHeight(referencePreviewZoom) * manualZoom
                            source: referenceDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: editorPreview.compareMode && referenceDisplaySource.length > 0
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: editorPreview.whiteBalanceSampling && !editorPreview.cropEditing && editedPreviewZoom.visible
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
                            active: !fitToView && editorPreview.cropEditing && !editorPreview.compareMode && editedPreviewZoom.visible
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

                        MaskCanvasOverlay {
                            anchors.fill: parent
                            visible: !fitToView && !editorPreview.compareMode && !editorPreview.cropEditing && editedPreviewZoom.visible
                            editRoot: root
                            imageX: editedPreviewZoom.x
                            imageY: editedPreviewZoom.y
                            imageWidth: editedPreviewZoom.width
                            imageHeight: editedPreviewZoom.height
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    propagateComposedEvents: true
                    enabled: editorPreview.hasAsset
                    onWheel: function (wheel) {
                        const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y;
                        if (delta === 0)
                            return;

                        const zoomFactor = delta > 0 ? 1.05 : (1 / 1.05);
                        setZoomLevel(currentZoomLevel() * zoomFactor, wheel.x, wheel.y);
                        wheel.accepted = true;
                    }
                }

                MouseArea {
                    anchors.fill: zoomPreviewFlick
                    visible: !fitToView && !editorPreview.whiteBalanceSampling && !editorPreview.cropEditing
                             && !(inspectorMode === "masks" && editorPreview.hasActiveMask && editorPreview.activeMaskKind === "brush")
                             && !objectPickArmed && !polygonDrawArmed && !lassoDrawArmed && !cloneSourcePickArmed
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    cursorShape: maxPanX() > 0 || maxPanY() > 0 ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor

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
                            width: 420
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 74
                radius: theme.radius8
                color: theme.alpha(theme.ink800, 0.82)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            Layout.fillWidth: true
                            text: editorPreview.processing ? "Rendering preview..." : editorPreview.status
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 24
                            radius: theme.radius8
                            color: fitToView ? theme.alpha(theme.accentWarm500, 0.12) : theme.ink850

                            Label {
                                anchors.centerIn: parent
                                text: "Fit"
                                color: fitToView ? theme.textStrong : theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.Medium
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    fitToView = true;
                                    zoomPreviewFlick.contentX = 0;
                                    zoomPreviewFlick.contentY = 0;
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 24
                            radius: theme.radius8
                            color: !fitToView && Math.abs(manualZoom - 1.0) < 0.01 ? theme.alpha(theme.accentWarm500, 0.12) : theme.ink850

                            Label {
                                anchors.centerIn: parent
                                text: "1:1"
                                color: !fitToView && Math.abs(manualZoom - 1.0) < 0.01 ? theme.textStrong : theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    setZoomLevel(1.0);
                                    centerZoomPreview();
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 74
                            Layout.preferredHeight: 24
                            radius: theme.radius8
                            color: editorPreview.compareMode ? theme.alpha(theme.accentWarm500, 0.12) : theme.ink850
                            opacity: editorPreview.hasAsset ? 1.0 : 0.55

                            Label {
                                anchors.centerIn: parent
                                text: "Compare"
                                color: editorPreview.compareMode ? theme.textStrong : theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.Medium
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: editorPreview.hasAsset
                                onClicked: editorPreview.compareMode = !editorPreview.compareMode
                            }
                        }

                        Label {
                            text: fitToView ? "Fit" : (Math.round(manualZoom * 100) + "%")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }
                    }

                    ListView {
                        id: filmstripView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        orientation: ListView.Horizontal
                        boundsBehavior: Flickable.StopAtBounds
                        spacing: 8
                        model: catalogAssets
                        currentIndex: catalogAssets.selectedIndex

                        delegate: Rectangle {
                            required property int index
                            required property string assetId
                            required property string thumbnailSource

                            readonly property bool isCurrent: catalogAssets.selectedIndex === index
                            readonly property bool isInSelection: catalogAssets.assetSelected(assetId)

                            width: 52
                            height: filmstripView.height
                            radius: theme.radius6
                            color: isCurrent
                                   ? theme.alpha(theme.ink700, 0.82)
                                   : (isInSelection ? theme.alpha(theme.ink750, 0.62) : theme.ink900)
                            border.width: isInSelection ? (isCurrent ? 2 : 1) : 0
                            border.color: isCurrent ? theme.accentWarm400 : theme.accentCool500

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 3
                                radius: theme.radius4
                                color: theme.ink950
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: thumbnailSource
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: false
                                    visible: thumbnailSource.length > 0
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    color: theme.ink800
                                    visible: thumbnailSource.length === 0
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: function(mouse) {
                                    if (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier))
                                        catalogAssets.toggleSelectionIndex(index)
                                    else
                                        catalogAssets.selectIndex(index)
                                }
                            }
                        }

                        footer: Item {
                            width: 10
                            height: filmstripView.height
                        }
                    }
                }
            }
        }

        Item {
            Layout.preferredWidth: inspectorCollapsed ? 24 : 324
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
                bodyMargin: 4
                bodySpacing: 4
                title: "Inspector"

                Rectangle {
                    implicitHeight: inspectorSummary.implicitHeight + 16
                    Layout.fillWidth: true
                    radius: 8
                    color: theme.alpha(theme.ink800, 0.82)

                    ColumnLayout {
                        id: inspectorSummary
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Rectangle {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: 7
                                color: theme.ink850

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

                            ActionButton {
                                text: "Adjust"
                                compact: true
                                active: inspectorMode === "adjust"
                                font.pixelSize: 10
                                implicitHeight: 28
                                onClicked: inspectorMode = "adjust"
                            }

                            ActionButton {
                                text: "Masks"
                                compact: true
                                active: inspectorMode === "masks"
                                font.pixelSize: 10
                                implicitHeight: 28
                                onClicked: inspectorMode = "masks"
                            }

                            ActionButton {
                                text: "Crop"
                                compact: true
                                active: inspectorMode === "crop"
                                font.pixelSize: 10
                                implicitHeight: 28
                                onClicked: inspectorMode = "crop"
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ActionButton {
                                text: "Reset"
                                compact: true
                                enabled: editorPreview.hasAsset
                                font.pixelSize: 10
                                implicitHeight: 28
                                onClicked: editorPreview.resetAdjustments()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            ActionButton {
                                text: editorPreview.whiteBalanceSampling ? "Cancel WB" : "Pick WB"
                                active: editorPreview.whiteBalanceSampling
                                compact: true
                                enabled: editorPreview.hasAsset
                                font.pixelSize: 10
                                implicitHeight: 24
                                onClicked: editorPreview.whiteBalanceSampling = !editorPreview.whiteBalanceSampling
                            }

                            ActionButton {
                                text: editorPreview.compareMode ? "Compare On" : "Compare"
                                active: editorPreview.compareMode
                                compact: true
                                enabled: editorPreview.hasAsset
                                font.pixelSize: 10
                                implicitHeight: 24
                                onClicked: editorPreview.compareMode = !editorPreview.compareMode
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                text: (catalogAssets.selectedRecipeJson.length > 0 ? "Edited" : "Base")
                                      + (editorPreview.maskCount > 0 ? " · Masks" : "")
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 9
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: inspectorMode === "adjust"
                            radius: 7
                            color: theme.alpha(theme.ink900, 0.72)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.28)
                            implicitHeight: batchToolsColumn.implicitHeight + 14

                            ColumnLayout {
                                id: batchToolsColumn
                                anchors.fill: parent
                                anchors.margins: 7
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
                                            model: adjustmentGroups

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
                                            }
                                        }

                                        ActionButton {
                                            text: "Apply"
                                            compact: true
                                            enabled: selectedPresetName.length > 0 && editorPreview.presetRecipeJson(selectedPresetName).length > 0
                                            onClicked: applyPresetToSelection(selectedPresetName)
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        ToneTextField {
                                            Layout.fillWidth: true
                                            placeholderText: "Preset name"
                                            text: presetDraftName
                                            onTextEdited: presetDraftName = text
                                            onAccepted: saveCurrentPreset()
                                        }

                                        ActionButton {
                                            text: "Save"
                                            compact: true
                                            enabled: editorPreview.hasAsset && presetDraftName.trim().length > 0
                                            onClicked: saveCurrentPreset()
                                        }

                                        ActionButton {
                                            text: "Delete"
                                            compact: true
                                            enabled: selectedPresetName.length > 0 && editorPreview.presetRecipeJson(selectedPresetName).length > 0
                                            onClicked: {
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

                        HistogramDisplay {
                            width: parent.width
                            height: 80
                            redData: editorPreview.histogramRed
                            greenData: editorPreview.histogramGreen
                            blueData: editorPreview.histogramBlue
                            luminanceData: editorPreview.histogramLuminance
                        }

                        MaskInspectorSections {
                            width: parent.width
                            visible: inspectorMode === "masks"
                            editRoot: root
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust" && editorPreview.rawControlsAvailable
                            title: "RAW Decode"
                            subtitle: "Camera-stage decode controls before the main grade."
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW Exposure"
                                valueText: editorPreview.rawExposure.toFixed(2) + " EV"
                                from: -4
                                to: 4
                                value: editorPreview.rawExposure
                                onValueDragged: editorPreview.rawExposure = value
                                onResetRequested: editorPreview.rawExposure = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Boost"
                                valueText: editorPreview.rawBoost.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawBoost
                                onValueDragged: editorPreview.rawBoost = value
                                onResetRequested: editorPreview.rawBoost = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadow Boost"
                                valueText: editorPreview.rawShadowBoost.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawShadowBoost
                                onValueDragged: editorPreview.rawShadowBoost = value
                                onResetRequested: editorPreview.rawShadowBoost = 1
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW Detail"
                                valueText: editorPreview.rawDetail.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawDetail
                                onValueDragged: editorPreview.rawDetail = value
                                onResetRequested: editorPreview.rawDetail = 0
                            }

                            CheckBox {
                                text: "Highlight Recovery"
                                checked: editorPreview.rawHighlightRecovery
                                onToggled: editorPreview.rawHighlightRecovery = checked
                            }

                            CheckBox {
                                text: "Gamut Mapping"
                                checked: editorPreview.rawGamutMapping
                                onToggled: editorPreview.rawGamutMapping = checked
                            }

                            CheckBox {
                                text: "Lens Correction"
                                checked: editorPreview.rawLensCorrection
                                onToggled: editorPreview.rawLensCorrection = checked
                            }

                            CheckBox {
                                text: "Override White Balance"
                                checked: editorPreview.rawWhiteBalanceOverride
                                onToggled: editorPreview.rawWhiteBalanceOverride = checked
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                enabled: editorPreview.rawWhiteBalanceOverride
                                label: "RAW Temp"
                                valueText: Math.round(editorPreview.rawWhiteBalanceTemperature).toString() + " K"
                                from: 2000
                                to: 15000
                                value: editorPreview.rawWhiteBalanceTemperature
                                onValueDragged: editorPreview.rawWhiteBalanceTemperature = value
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
                                onValueDragged: editorPreview.rawWhiteBalanceTint = value
                                onResetRequested: editorPreview.rawWhiteBalanceTint = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Light"
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Exposure"
                                valueText: editorPreview.exposure.toFixed(2) + " EV"
                                from: -2
                                to: 2
                                value: editorPreview.exposure
                                onValueDragged: editorPreview.exposure = value
                                onResetRequested: editorPreview.exposure = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Contrast"
                                valueText: Math.round(editorPreview.contrast).toString()
                                from: -100
                                to: 100
                                value: editorPreview.contrast
                                onValueDragged: editorPreview.contrast = value
                                onResetRequested: editorPreview.contrast = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Highlights"
                                valueText: Math.round(editorPreview.highlights).toString()
                                from: -100
                                to: 100
                                value: editorPreview.highlights
                                onValueDragged: editorPreview.highlights = value
                                onResetRequested: editorPreview.highlights = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadows"
                                valueText: Math.round(editorPreview.shadows).toString()
                                from: -100
                                to: 100
                                value: editorPreview.shadows
                                onValueDragged: editorPreview.shadows = value
                                onResetRequested: editorPreview.shadows = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Whites"
                                valueText: Math.round(editorPreview.whites).toString()
                                from: -100
                                to: 100
                                value: editorPreview.whites
                                onValueDragged: editorPreview.whites = value
                                onResetRequested: editorPreview.whites = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Blacks"
                                valueText: Math.round(editorPreview.blacks).toString()
                                from: -100
                                to: 100
                                value: editorPreview.blacks
                                onValueDragged: editorPreview.blacks = value
                                onResetRequested: editorPreview.blacks = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Color"
                            subtitle: "White balance and global color intensity."
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Temperature"
                                valueText: Math.round(editorPreview.temperature).toString()
                                trackColors: ["#4c78cb", "#c8d0d8", "#e6a14f"]
                                from: -100
                                to: 100
                                value: editorPreview.temperature
                                onValueDragged: editorPreview.temperature = value
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
                                onValueDragged: editorPreview.tint = value
                                onResetRequested: editorPreview.tint = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Vibrance"
                                valueText: Math.round(editorPreview.vibrance).toString()
                                from: -100
                                to: 100
                                value: editorPreview.vibrance
                                onValueDragged: editorPreview.vibrance = value
                                onResetRequested: editorPreview.vibrance = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.saturation).toString()
                                from: -100
                                to: 100
                                value: editorPreview.saturation
                                onValueDragged: editorPreview.saturation = value
                                onResetRequested: editorPreview.saturation = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Presence"
                            subtitle: "Texture, local contrast, and atmosphere."
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Texture"
                                valueText: Math.round(editorPreview.texture).toString()
                                from: -100
                                to: 100
                                value: editorPreview.texture
                                onValueDragged: editorPreview.texture = value
                                onResetRequested: editorPreview.texture = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Clarity"
                                valueText: Math.round(editorPreview.clarity).toString()
                                from: -100
                                to: 100
                                value: editorPreview.clarity
                                onValueDragged: editorPreview.clarity = value
                                onResetRequested: editorPreview.clarity = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Dehaze"
                                valueText: Math.round(editorPreview.dehaze).toString()
                                from: -100
                                to: 100
                                value: editorPreview.dehaze
                                onValueDragged: editorPreview.dehaze = value
                                onResetRequested: editorPreview.dehaze = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Detail"
                            subtitle: "Sharpening and noise cleanup. Judge at 1:1."
                            expandable: true
                            expanded: false

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Sharpening"
                                valueText: Math.round(editorPreview.sharpening).toString()
                                from: 0
                                to: 100
                                value: editorPreview.sharpening
                                onValueDragged: editorPreview.sharpening = value
                                onResetRequested: editorPreview.sharpening = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Masking"
                                valueText: Math.round(editorPreview.sharpenMasking).toString()
                                from: 0
                                to: 100
                                value: editorPreview.sharpenMasking
                                onValueDragged: editorPreview.sharpenMasking = value
                                onResetRequested: editorPreview.sharpenMasking = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luma NR"
                                valueText: Math.round(editorPreview.luminanceNr).toString()
                                from: 0
                                to: 100
                                value: editorPreview.luminanceNr
                                onValueDragged: editorPreview.luminanceNr = value
                                onResetRequested: editorPreview.luminanceNr = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Color NR"
                                valueText: Math.round(editorPreview.colorNr).toString()
                                from: 0
                                to: 100
                                value: editorPreview.colorNr
                                onValueDragged: editorPreview.colorNr = value
                                onResetRequested: editorPreview.colorNr = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Optics"
                            subtitle: "Lens cleanup and edge behavior."
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
                                onValueDragged: editorPreview.lensDistortion = value
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
                                onValueDragged: editorPreview.lensVignette = value
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
                                onValueDragged: editorPreview.chromaticAberration = value
                                onResetRequested: editorPreview.chromaticAberration = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Tone Curve"
                            subtitle: "Point curves now drag continuously across the graph."
                            expandable: true
                            expanded: true

                            ToneCurveEditor {
                                Layout.fillWidth: true
                                accent: editorPreview.curveChannel === "Red"
                                        ? "#d45a62"
                                        : (editorPreview.curveChannel === "Green"
                                           ? "#61b56f"
                                           : (editorPreview.curveChannel === "Blue"
                                              ? "#4e86d9"
                                              : "#c99a52"))
                                selectedChannel: editorPreview.curveChannel
                                pointsModel: editorPreview.activeCurvePoints
                                onChannelSelected: editorPreview.curveChannel = channel
                                onPointsEdited: editorPreview.activeCurvePoints = points
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Color Mixer"
                            subtitle: "Target specific hue ranges without shifting the whole frame."
                            expandable: true
                            expanded: true

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
                                onValueDragged: editorPreview.hueAdjustment = value
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
                                onValueDragged: editorPreview.saturationAdjustment = value
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
                                onValueDragged: editorPreview.luminanceAdjustment = value
                                onResetRequested: editorPreview.luminanceAdjustment = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Color Grading"
                            subtitle: "Tint shadows, midtones, and highlights separately."
                            expandable: true
                            expanded: true

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Shadows"
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.gradingShadowsHue).toString()
                                trackColors: rainbowTrack()
                                from: -100
                                to: 100
                                value: editorPreview.gradingShadowsHue
                                onValueDragged: editorPreview.gradingShadowsHue = value
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
                                onValueDragged: editorPreview.gradingShadowsSaturation = value
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
                                onValueDragged: editorPreview.gradingShadowsLuminance = value
                                onResetRequested: editorPreview.gradingShadowsLuminance = 0
                            }

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Midtones"
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.gradingMidtonesHue).toString()
                                trackColors: rainbowTrack()
                                from: -100
                                to: 100
                                value: editorPreview.gradingMidtonesHue
                                onValueDragged: editorPreview.gradingMidtonesHue = value
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
                                onValueDragged: editorPreview.gradingMidtonesSaturation = value
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
                                onValueDragged: editorPreview.gradingMidtonesLuminance = value
                                onResetRequested: editorPreview.gradingMidtonesLuminance = 0
                            }

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Highlights"
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.gradingHighlightsHue).toString()
                                trackColors: rainbowTrack()
                                from: -100
                                to: 100
                                value: editorPreview.gradingHighlightsHue
                                onValueDragged: editorPreview.gradingHighlightsHue = value
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
                                onValueDragged: editorPreview.gradingHighlightsSaturation = value
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
                                onValueDragged: editorPreview.gradingHighlightsLuminance = value
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
                                model: ["Original", "Square", "4x5", "3x2", "16x9"]
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
                                model: ["Off", "Thirds", "Center", "Diagonal"]
                                currentIndex: model.indexOf(cropGuideMode)
                                onActivated: cropGuideMode = currentText
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zoom"
                                valueText: editorPreview.cropZoom.toFixed(2) + "x"
                                from: 1
                                to: 3
                                stepSize: 0.01
                                value: editorPreview.cropZoom
                                onValueDragged: editorPreview.cropZoom = value
                                onResetRequested: editorPreview.cropZoom = 1
                            }
                        }
                    }
                }
            }
        }
    }
}
