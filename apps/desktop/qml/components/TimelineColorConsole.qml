pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

GlassSurface {
    id: root

    property bool embeddedDockHeader: false
    required property var theme
    required property var timelineDocument
    required property var currentClip
    property var sharedEditController: null
    property var catalogModel: null
    property string requestedSharedTab: "adjust"
    property var videoColorSignalMetadata: ({})
    property string activePage: "primaries"
    property string curveChannel: "RGB"
    property string comparisonMode: "wipe"
    property string message: ""
    property bool continuousEditOpen: false
    property bool curveDraftActive: false
    property string curveDraftChannel: ""
    property var curveDraftPoints: []
    property var sequenceTrackRowsSnapshot: []
    property bool sequenceTrackRowsRefreshQueued: false

    signal dockCloseRequested()
    signal detailedColorRequested(string tab)
    signal comparisonRequested(string mode)

    function showSharedTab(tab) {
        root.requestedSharedTab = String(tab || "adjust")
        root.activePage = "edit"
        if (sharedEditLoader.item)
            sharedEditLoader.item.activateTab(root.requestedSharedTab)
    }

    function sampleProgramPressed(x, y, pointerX, pointerY) {
        if (sharedEditLoader.item) sharedEditLoader.item.samplePressed(x, y, pointerX, pointerY)
    }
    function sampleProgramMoved(x, y, width, height) {
        if (sharedEditLoader.item) sharedEditLoader.item.sampleMoved(x, y, width, height)
    }
    function sampleProgramReleased() {
        if (sharedEditLoader.item) sharedEditLoader.item.sampleReleased()
    }

    readonly property string timelineColorConsoleContract:
        "timeline-color-console:v2|target:single-photo-video-or-adjustment|photo-grade:static-pointwise-shared-photo-engine-recipe|primaries:exposure-contrast-highlights-shadows-whites-blacks-temperature-tint-vibrance-saturation|wheels:three-way|curves:master-luma-rgb|lut:cube-validated|input:camera-transform-before-finishing-lut|comparison:program-session|state:undo-readback-lock-aware|surface:retained-dock"
    readonly property string selectedClipId: String(currentClip && currentClip.clipId || "")
    readonly property string colorLutDisplayName: {
        const friendlyName = String(root.currentClip.colorLutName || "").trim()
        if (friendlyName.length > 0)
            return friendlyName.replace(/\\/g, "/").split("/").pop()
        const path = String(root.currentClip.colorLutPath || "")
        if (path.length === 0)
            return "No finishing LUT assigned."
        return path.replace(/\\/g, "/").split("/").pop()
    }
    readonly property bool hasSingleSelection: Number(timelineDocument.selectedClipCount || 0) === 1
                                                   && selectedClipId.length > 0
    readonly property bool isEditablePicture: hasSingleSelection
            && (String(currentClip.mediaType || "") === "photo"
                || String(currentClip.mediaType || "") === "video"
                || String(currentClip.mediaType || "") === "adjustment")
    readonly property bool isVideoClip: isEditablePicture
            && String(currentClip.mediaType || "") === "video"
    readonly property bool selectedTrackLocked: {
        if (root.sharedEditController)
            return !root.sharedEditController.editable
        if (!isEditablePicture)
            return false
        const tracks = root.sequenceTrackRowsSnapshot || []
        const trackId = String(currentClip.trackId || "")
        for (let index = 0; index < tracks.length; ++index) {
            if (String(tracks[index].trackId || "") === trackId)
                return tracks[index].locked === true
        }
        return true
    }
    readonly property bool recipeUnavailable: String(currentClip.colorGradeRecipeError || "").length > 0
    readonly property bool gradeDeliveryPending:
        currentClip.colorGradeSequenceExportPending === true
    readonly property bool gradeDeliveryUnsupported:
        currentClip.colorGradeSequenceExportSupported === false
    readonly property bool canEditGrade: isEditablePicture && !selectedTrackLocked && !recipeUnavailable
    readonly property string targetLabel: hasSingleSelection
                                          ? String(currentClip.displayLabel || "Selected clip")
                                          : Number(timelineDocument.selectedClipCount || 0) > 1
                                            ? Number(timelineDocument.selectedClipCount) + " clips selected"
                                            : "Select one photo, video, or adjustment clip"
    readonly property var parameterDefinitions: [
        {"key": "exposure", "label": "Exposure", "minimum": -5, "maximum": 5,
         "step": 0.01, "unit": " EV", "precision": 2},
        {"key": "contrast", "label": "Contrast", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0},
        {"key": "highlights", "label": "Highlights", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0},
        {"key": "shadows", "label": "Shadows", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0},
        {"key": "whites", "label": "Whites", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0},
        {"key": "blacks", "label": "Blacks", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0},
        {"key": "temperature", "label": "Temperature", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0},
        {"key": "tint", "label": "Tint", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0},
        {"key": "vibrance", "label": "Vibrance", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0},
        {"key": "saturation", "label": "Saturation", "minimum": -100, "maximum": 100,
         "step": 1, "unit": "", "precision": 0}
    ]

    function refreshSequenceTrackRowsSnapshot() {
        if (root.sharedEditController)
            return
        if (!root.visible || !root.timelineDocument)
            return
        sequenceTrackRowsSnapshot = timelineDocument.snapshotSequenceTracks()
    }

    function scheduleSequenceTrackRowsSnapshotRefresh() {
        if (root.sharedEditController || !root.visible || sequenceTrackRowsRefreshQueued)
            return
        sequenceTrackRowsRefreshQueued = true
        Qt.callLater(function() {
            root.sequenceTrackRowsRefreshQueued = false
            root.refreshSequenceTrackRowsSnapshot()
        })
    }

    onVisibleChanged: {
        if (root.sharedEditController) root.sharedEditController.active = root.visible
        else if (visible) refreshSequenceTrackRowsSnapshot()
        else endContinuousEdit()
    }
    onTimelineDocumentChanged: if (visible) scheduleSequenceTrackRowsSnapshotRefresh()
    Component.onCompleted: {
        if (root.sharedEditController) root.sharedEditController.active = root.visible
        else if (visible) refreshSequenceTrackRowsSnapshot()
    }

    Connections {
        target: root.timelineDocument
        enabled: root.visible

        function onClipsChanged() { root.scheduleSequenceTrackRowsSnapshotRefresh() }
        function onSequenceChanged() { root.scheduleSequenceTrackRowsSnapshotRefresh() }
    }

    radius: 0
    accent: theme.accentCool400
    accentRail: false
    elevated: false
    edgeOpacity: 0
    implicitWidth: theme.scaled(380)
    implicitHeight: theme.scaled(580)
    activeFocusOnTab: true

    function gradeValue(parameter) {
        const values = currentClip.colorGradeParameters || ({})
        const value = values[String(parameter || "")]
        return value === undefined || value === null ? 0 : Number(value)
    }

    function keyframesFor(parameter) {
        const source = currentClip.keyframes || []
        const expected = "grade:" + String(parameter || "")
        const result = []
        for (let index = 0; index < source.length; ++index) {
            if (String(source[index].parameter || "") === expected)
                result.push(source[index])
        }
        return result
    }

    function currentKeyframeLocalSeconds() {
        if (!hasSingleSelection)
            return -1
        const local = Number(timelineDocument.sequencePlayheadSeconds || 0)
                - Number(currentClip.startSeconds || 0)
        return local >= 0 && local < Number(currentClip.trimmedDurationSeconds || 0)
                ? local : -1
    }

    function gradeCurvePoints(channel) {
        const requestedChannel = String(channel || "RGB")
        if (curveDraftActive && curveDraftChannel === requestedChannel)
            return curveDraftPoints
        const curves = currentClip.colorGradeCurves || ({})
        const points = curves[requestedChannel]
        return points && points.length >= 2
                ? points : [{"x": 0, "y": 0}, {"x": 1, "y": 1}]
    }

    function setGradeValue(parameter, value) {
        if (!canEditGrade || !Number.isFinite(Number(value)))
            return false
        const key = String(parameter || "")
        const localSeconds = currentKeyframeLocalSeconds()
        if (keyframesFor(key).length > 0 && localSeconds >= 0) {
            const result = timelineDocument.editClipKeyframeForAutomation(
                        selectedClipId, "set", "grade:" + key, localSeconds,
                        Number(value), "")
            return result && result.ok === true
        }
        const result = timelineDocument.setClipColorGradeParameterForAutomation(
                    selectedClipId, key, Number(value))
        if (!result || result.ok !== true) {
            message = String(result && result.error || "The grade could not be updated.")
            return false
        }
        message = ""
        return true
    }

    function setCurvePoints(channel, points) {
        if (!canEditGrade)
            return false
        const requestedChannel = String(channel || "RGB")
        const requestedPoints = points || []
        const gestureIsActive = continuousEditOpen
        if (gestureIsActive) {
            // Keep the in-progress handle while the controller normalizes an
            // identity curve back to the default empty recipe. This lets an
            // inserted point remain draggable until it changes the curve.
            curveDraftChannel = requestedChannel
            curveDraftPoints = requestedPoints
            curveDraftActive = true
        }
        const result = timelineDocument.setClipColorGradeCurveForAutomation(
                    selectedClipId, "", requestedChannel, requestedPoints)
        if (!result || result.ok !== true) {
            message = String(result && result.error || "The curve could not be updated.")
            return false
        }
        if (!gestureIsActive) {
            curveDraftActive = false
            curveDraftPoints = []
        }
        message = ""
        return true
    }

    function resetCurvePoints(channel) {
        if (!canEditGrade)
            return false
        beginContinuousEdit()
        try {
            return setCurvePoints(channel, [])
        } finally {
            finishCurveEdit()
        }
    }

    function finishCurveEdit() {
        endContinuousEdit()
        curveDraftActive = false
        curveDraftPoints = []
    }

    function suspendForRelease() {
        // Slider and curve gestures write through to the document while an
        // undo group is open. Close that group before a hidden panel unloads.
        endContinuousEdit()
    }

    function inputTransformMenuItems() {
        const selected = String(currentClip.videoInputTransform || "")
        const automatic = String(currentClip.videoInputTransformSource || "automatic") === "automatic"
        return [
            {"key": "auto", "label": "Automatic from camera metadata", "checked": automatic},
            {"key": "none", "label": "No camera input transform",
             "checked": !automatic && selected.length === 0},
            {"key": "slog2-sgamut", "label": "Sony S-Log2 / S-Gamut to Rec.709",
             "checked": !automatic && selected === "slog2-sgamut"},
            {"key": "slog3-sgamut3-cine", "label": "Sony S-Log3 / S-Gamut3.Cine to Rec.709",
             "checked": !automatic && selected === "slog3-sgamut3-cine"},
            {"key": "slog3-sgamut3", "label": "Sony S-Log3 / S-Gamut3 to Rec.709",
             "checked": !automatic && selected === "slog3-sgamut3"}
        ]
    }

    function inputTransformLabel() {
        const source = String(currentClip.videoInputTransformSource || "automatic")
        const selected = String(currentClip.videoInputTransform || "")
        if (source === "automatic")
            return "Input · Auto"
        if (selected.length === 0 || selected === "none")
            return "Input · Bypass"
        if (selected === "slog2-sgamut")
            return "Input · S-Log2"
        if (selected === "slog3-sgamut3-cine")
            return "Input · S-Log3 Cine"
        if (selected === "slog3-sgamut3")
            return "Input · S-Log3"
        return "Input · Manual"
    }

    function setInputTransform(profile) {
        if (!isVideoClip || selectedTrackLocked)
            return false
        const changed = timelineDocument.setSelectedClipVideoInputTransform(String(profile || "none"))
        message = changed ? "" : String(timelineDocument.status || "Input transform was not changed.")
        return changed
    }

    function applyLut(fileUrl) {
        if (!canEditGrade || !isVideoClip)
            return false
        const result = timelineDocument.applySelectedClipColorLut(String(fileUrl || ""))
        message = result ? "" : String(timelineDocument.status || "The CUBE LUT could not be applied.")
        return result
    }

    function clearLut() {
        if (!canEditGrade || !isVideoClip)
            return false
        const result = timelineDocument.clearSelectedClipColorLut()
        message = result ? "" : String(timelineDocument.status || "The finishing LUT could not be cleared.")
        return result
    }

    function resetGrade() {
        if (!canEditGrade)
            return false
        const result = timelineDocument.clearClipColorGradeRecipeForAutomation(selectedClipId)
        message = result && result.ok ? "" : String(result && result.error || "The grade could not be reset.")
        return result && result.ok === true
    }

    function beginContinuousEdit() {
        if (canEditGrade && !continuousEditOpen) {
            timelineDocument.beginContinuousEdit()
            continuousEditOpen = true
        }
    }

    function endContinuousEdit() {
        if (continuousEditOpen) {
            timelineDocument.endContinuousEdit()
            continuousEditOpen = false
        }
    }

    FileDialog {
        id: lutFileDialog
        title: "Choose a CUBE finishing LUT"
        fileMode: FileDialog.OpenFile
        nameFilters: ["3D LUT (*.cube)"]
        onAccepted: root.applyLut(selectedFile.toString())
    }

    onSharedEditControllerChanged: {
        if (root.sharedEditController)
            root.sharedEditController.active = root.visible
    }
    Component.onDestruction: {
        endContinuousEdit()
        if (root.sharedEditController)
            root.sharedEditController.active = false
    }

    Loader {
        id: sharedEditLoader
        objectName: "timelineSharedEditColorLoader"
        anchors.fill: parent
        anchors.margins: theme.panelPaddingCompact
        active: root.sharedEditController !== null
        visible: active && root.activePage !== "input"
        function loadEditor() {
            if (active && source.toString().length === 0)
                setSource(Qt.resolvedUrl("TimelineSharedEditColorPanel.qml"), {
                    bridge: root.sharedEditController,
                    timelineDocument: root.timelineDocument,
                    catalogModel: root.catalogModel,
                    requestedTab: root.requestedSharedTab
                })
        }
        onActiveChanged: if (active) Qt.callLater(loadEditor)
        Component.onCompleted: loadEditor()
        onLoaded: {
            item.deliveryWarning = Qt.binding(function() {
                return root.gradeDeliveryUnsupported ? String(root.currentClip.colorGradeSequenceExportError || "This grade cannot be exported with the clip's current settings.") : ""
            })
            item.videoInputRequested.connect(function() { root.activePage = "input" })
            item.comparisonRequested.connect(function(mode) { root.comparisonRequested(mode) })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.panelPaddingCompact
        visible: !root.sharedEditController || root.activePage === "input"
        spacing: theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space2

                Label {
                    objectName: "timelineColorConsoleTitle"
                    text: "Color Console"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeSection
                    font.weight: Font.DemiBold
                }

                Label {
                    objectName: "timelineColorConsoleTarget"
                    Layout.fillWidth: true
                    text: root.targetLabel
                    color: root.isEditablePicture ? theme.textMuted : theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                objectName: "timelineColorMoreToolsButton"
                text: "Details"
                iconName: "swatch"
                compact: true
                enabled: root.isEditablePicture
                disabledReason: "Select one photo, video, or adjustment clip."
                tooltipText: root.currentClip.mediaType === "photo"
                             ? "Open the full Color inspector for the still photo's static grade recipe and scopes."
                             : "Open the full Color inspector for recipe layers, scopes, and animation."
                onClicked: root.detailedColorRequested("color")
            }

            ActionButton {
                objectName: "timelineColorConsoleCloseButton"
                visible: !root.embeddedDockHeader
                showText: false
                compact: true
                iconName: "close"
                tooltipText: "Close Color Console"
                onClicked: root.dockCloseRequested()
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: theme.space6

            Repeater {
                model: root.sharedEditController ? [
                    {"key": "edit", "label": "Edit color tools"},
                    {"key": "input", "label": "Input + LUT"}
                ] : [
                    {"key": "primaries", "label": "Primaries"},
                    {"key": "wheels", "label": "Wheels"},
                    {"key": "curves", "label": "Curves"},
                    {"key": "input", "label": "Input + LUT"}
                ]

                delegate: NavigationTab {
                    required property var modelData
                    objectName: "timelineColorConsolePage_" + String(modelData.key)
                    Layout.fillWidth: true
                    text: String(modelData.label)
                    underlineSelection: true
                    selected: root.activePage === String(modelData.key)
                    enabled: root.isEditablePicture
                    onClicked: root.activePage = String(modelData.key)
                }
            }

            MenuButton {
                objectName: "timelineColorConsoleCompareMenu"
                text: "Compare"
                iconName: "compare"
                compact: true
                primaryOpensMenu: true
                enabled: root.isVideoClip && !root.selectedTrackLocked
                disabledReason: "Select one unlocked video clip."
                tooltipText: "Open the Program comparison surface at the current playhead."
                menuWidth: 190
                menuItems: [
                    {"key": "wipe", "label": "Wipe"},
                    {"key": "split", "label": "Split"},
                    {"key": "overlay", "label": "Overlay"},
                    {"key": "difference", "label": "Difference"}
                ]
                onMenuItemTriggered: function(key) {
                    root.comparisonMode = String(key || "wipe")
                    root.comparisonRequested(root.comparisonMode)
                }
            }
        }

        Rectangle {
            objectName: "timelineColorConsoleStatusBar"
            Layout.fillWidth: true
            Layout.preferredHeight: statusRow.implicitHeight + theme.space6 * 2
            radius: theme.radiusControl
            color: !root.hasSingleSelection ? theme.alpha(theme.ink850, 0.62)
                   : root.selectedTrackLocked ? theme.alpha(theme.warning, 0.13)
                   : root.recipeUnavailable ? theme.alpha(theme.error, 0.13)
                   : theme.alpha(theme.accentCool400, 0.10)
            border.width: 0

            GridLayout {
                id: statusRow
                columns: root.width < theme.scaled(420) ? 1 : 2
                anchors.fill: parent
                anchors.margins: theme.space6
                columnSpacing: theme.space6
                rowSpacing: theme.space6

                StatusChip {
                    objectName: "timelineColorConsoleSelectionState"
                    text: !root.hasSingleSelection ? "NO SINGLE CLIP"
                          : root.selectedTrackLocked ? "TRACK LOCKED"
                          : root.recipeUnavailable ? "GRADE UNAVAILABLE"
                          : root.currentClip.mediaType === "adjustment" ? "ADJUSTMENT"
                          : root.currentClip.mediaType === "photo" ? "PHOTO CLIP"
                          : "VIDEO CLIP"
                    tone: root.selectedTrackLocked || root.recipeUnavailable ? "warning"
                          : root.isEditablePicture ? "info" : "neutral"
                    compact: true
                }

                Label {
                    Layout.fillWidth: true
                    Layout.columnSpan: statusRow.columns
                    visible: !root.canEditGrade || root.gradeDeliveryUnsupported
                    text: !root.hasSingleSelection ? "Select one photo, video, or adjustment clip to edit color."
                          : root.selectedTrackLocked ? "Unlock the selected track to edit this grade."
                          : root.recipeUnavailable ? String(root.currentClip.colorGradeRecipeError)
                          : root.gradeDeliveryPending ? "Checking sequence delivery support for this grade."
                          : root.gradeDeliveryUnsupported ? "Sequence delivery cannot render this grade yet. Review the full Color inspector before delivery."
                          : "Color changes apply to the selected clip."
                    color: root.gradeDeliveryUnsupported ? theme.warning : theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    objectName: "timelineColorResetButton"
                    text: "Reset color"
                    iconName: "reset"
                    tone: "quiet"
                    compact: true
                    enabled: root.canEditGrade
                    disabledReason: root.selectedTrackLocked ? "Unlock this track to edit its grade."
                                     : root.recipeUnavailable ? String(root.currentClip.colorGradeRecipeError)
                                     : "Select one editable picture clip."
                    onClicked: root.resetGrade()
                }
            }
        }

        Label {
            objectName: "timelineColorConsoleMessage"
            Layout.fillWidth: true
            visible: root.message.length > 0
            text: root.message
            color: theme.warning
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.Wrap
        }

        StackLayout {
            id: colorPageStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.activePage === "wheels" ? 1
                          : root.activePage === "curves" ? 2
                          : root.activePage === "input" ? 3 : 0

            ScrollView {
                objectName: "timelineColorConsolePrimariesPage"
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                contentWidth: availableWidth

                ColumnLayout {
                    width: colorPageStack.width
                    spacing: theme.space8

                    Label {
                        Layout.fillWidth: true
                        text: "Primary correction"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: root.parameterDefinitions

                        delegate: InspectorSlider {
                            required property var modelData
                            objectName: "timelineColorConsole_" + String(modelData.key)
                            Layout.fillWidth: true
                            label: String(modelData.label)
                            from: Number(modelData.minimum)
                            to: Number(modelData.maximum)
                            stepSize: Number(modelData.step)
                            value: root.gradeValue(String(modelData.key))
                            defaultValue: 0
                            precisionResponse: String(modelData.key) === "exposure"
                            valueInputMode: "raw"
                            valueText: Number(value).toFixed(Number(modelData.precision))
                                       + String(modelData.unit)
                            enabled: root.canEditGrade
                            disabledReason: root.selectedTrackLocked
                                            ? "Unlock this track to edit its grade."
                                            : String(root.currentClip.colorGradeRecipeError || "")
                            onInteractionStarted: root.beginContinuousEdit()
                            onInteractionFinished: root.endContinuousEdit()
                            onValueDragged: function(nextValue) {
                                root.setGradeValue(String(modelData.key), nextValue)
                            }
                            onResetRequested: root.setGradeValue(String(modelData.key), 0)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: theme.space4

                        ActionButton {
                            objectName: "timelineColorConsoleOpenCurves"
                            text: "Tone Curves"
                            iconName: "curve"
                            compact: true
                            enabled: root.canEditGrade
                            onClicked: root.activePage = "curves"
                        }

                        ActionButton {
                            objectName: "timelineColorConsoleOpenWheels"
                            Layout.fillWidth: true
                            text: "Three-way Wheels"
                            iconName: "swatch"
                            compact: true
                            enabled: root.canEditGrade
                            onClicked: root.activePage = "wheels"
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Exposure is in EV. Other primaries use the shared photo engine's normalized grade values."
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                }
            }

            Flickable {
                objectName: "timelineColorConsoleWheelsPage"
                clip: true
                contentWidth: Math.max(width, gradeWheelsRow.implicitWidth)
                contentHeight: gradeWheelsRow.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                RowLayout {
                    id: gradeWheelsRow
                    spacing: theme.space10
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: theme.space4

                    Repeater {
                        model: [
                            {"key": "gradingShadows", "label": "Shadows"},
                            {"key": "gradingMidtones", "label": "Midtones"},
                            {"key": "gradingHighlights", "label": "Highlights"}
                        ]

                        delegate: Rectangle {
                            id: gradeWheelCard
                            required property var modelData
                            objectName: "timelineColorConsole" + String(modelData.label) + "Wheel"
                            Layout.preferredWidth: Math.max(theme.scaled(132),
                                Math.min(theme.scaled(176), (root.width - theme.space24) / 3))
                            Layout.fillHeight: true
                            radius: theme.radiusSurface
                            color: theme.alpha(theme.ink850, 0.42)
                            border.width: 0

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: theme.space8
                                spacing: theme.space6

                                ColorWheelControl {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: Math.min(theme.scaled(124),
                                                                     parent.width - theme.space12)
                                    Layout.preferredHeight: Math.min(theme.scaled(162),
                                                                     root.width / 3 + theme.space24)
                                    title: String(gradeWheelCard.modelData.label)
                                    wheelSize: Math.min(theme.scaled(108), parent.width - theme.space16)
                                    hue: root.gradeValue(String(gradeWheelCard.modelData.key) + "Hue")
                                    saturation: root.gradeValue(String(gradeWheelCard.modelData.key) + "Saturation")
                                    luminance: root.gradeValue(String(gradeWheelCard.modelData.key) + "Luminance")
                                    enabled: root.canEditGrade
                                    onInteractionStarted: root.beginContinuousEdit()
                                    onInteractionFinished: root.endContinuousEdit()
                                    onHueEdited: function(value) {
                                        root.setGradeValue(String(gradeWheelCard.modelData.key) + "Hue", value)
                                    }
                                    onSaturationEdited: function(value) {
                                        root.setGradeValue(String(gradeWheelCard.modelData.key) + "Saturation", value)
                                    }
                                    onLuminanceEdited: function(value) {
                                        root.setGradeValue(String(gradeWheelCard.modelData.key) + "Luminance", value)
                                    }
                                }

                                InspectorSlider {
                                    objectName: "timelineColorConsole" + String(gradeWheelCard.modelData.label) + "Luminance"
                                    Layout.fillWidth: true
                                    label: "Luminance"
                                    from: -100
                                    to: 100
                                    stepSize: 1
                                    value: root.gradeValue(String(gradeWheelCard.modelData.key) + "Luminance")
                                    defaultValue: 0
                                    valueText: String(Math.round(value))
                                    enabled: root.canEditGrade
                                    onInteractionStarted: root.beginContinuousEdit()
                                    onInteractionFinished: root.endContinuousEdit()
                                    onValueDragged: function(value) {
                                        root.setGradeValue(String(gradeWheelCard.modelData.key) + "Luminance", value)
                                    }
                                    onResetRequested: root.setGradeValue(
                                                          String(gradeWheelCard.modelData.key) + "Luminance", 0)
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "timelineColorConsoleCurvesPage"
                spacing: theme.space6

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: "Point curves"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                        font.weight: Font.DemiBold
                    }

                    ToneComboBox {
                        objectName: "timelineColorConsoleCurveChannel"
                        Layout.preferredWidth: theme.scaled(112)
                        accessibleLabel: "Color curve channel"
                        model: ["RGB", "Luma", "Red", "Green", "Blue"]
                        currentIndex: Math.max(0, model.indexOf(root.curveChannel))
                        onActivated: function(index) {
                            root.curveChannel = String(model[index] || "RGB")
                        }
                    }

                    ActionButton {
                        objectName: "timelineColorConsoleResetCurve"
                        text: "Reset"
                        iconName: "reset"
                        compact: true
                        enabled: root.canEditGrade
                        onClicked: root.resetCurvePoints(root.curveChannel)
                    }
                }

                ToneCurveEditor {
                    objectName: "timelineColorConsoleToneCurve"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: theme.scaled(208)
                    compactMode: true
                    selectedChannel: root.curveChannel
                    pointsModel: root.gradeCurvePoints(root.curveChannel)
                    enabled: root.canEditGrade
                    onEditStarted: root.beginContinuousEdit()
                    onEditFinished: root.finishCurveEdit()
                    onChannelSelected: function(channel) {
                        root.curveChannel = String(channel || "RGB")
                    }
                    onPointsEdited: function(points) {
                        root.setCurvePoints(root.curveChannel, points)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "Curves are stored in the shared grade recipe and follow the selected clip through preview and delivery."
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }
            }

            ScrollView {
                objectName: "timelineColorConsoleInputPage"
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                contentWidth: availableWidth

                ColumnLayout {
                    width: colorPageStack.width
                    spacing: theme.space8

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Camera input transform"
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Input transforms decode supported Sony S-Log footage to Rec.709 before the creative grade and finishing LUT."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }

                    MenuButton {
                        objectName: "timelineColorConsoleInputTransformMenu"
                        Layout.fillWidth: true
                        text: root.inputTransformLabel()
                        iconName: "settings"
                        primaryOpensMenu: true
                        enabled: root.isVideoClip && !root.selectedTrackLocked
                        disabledReason: "Select one unlocked video clip."
                        tooltipText: String(root.currentClip.videoInputTransformName
                                             || "Automatic from camera metadata")
                                     + "\nChoose the transfer curve and gamut recorded by the source clip."
                        menuItems: root.inputTransformMenuItems()
                        onMenuItemTriggered: function(key) { root.setInputTransform(key) }
                    }

                    StatusChip {
                        objectName: "timelineColorConsoleInputStatus"
                        text: String(root.currentClip.videoInputTransformName
                                     || "Automatic camera profile")
                        tone: root.currentClip.videoInputTransform ? "info" : "neutral"
                        compact: true
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Camera signal details"
                        expandable: true
                        expanded: false

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: theme.space4

                            Label {
                                Layout.fillWidth: true
                                text: "Source transfer: " + String(root.currentClip.sourceColorTransfer || "Unknown")
                                color: theme.textMuted
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Source primaries: " + String(root.currentClip.sourceColorPrimaries || "Unknown")
                                color: theme.textMuted
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Matrix / range: " + String(root.currentClip.sourceColorMatrix || "Unknown")
                                     + " · " + String(root.currentClip.sourceColorRange || "Unknown")
                                color: theme.textMuted
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: root.videoColorSignalMetadata.clipId !== undefined
                                         && String(root.videoColorSignalMetadata.clipId) === root.selectedClipId
                                text: "Program frame: "
                                      + String(root.videoColorSignalMetadata.width || 0) + "×"
                                      + String(root.videoColorSignalMetadata.height || 0)
                                      + " · " + String(root.videoColorSignalMetadata.pixelFormat || "Unknown format")
                                color: theme.textFaint
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Finishing CUBE LUT"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.space6

                        ActionButton {
                            objectName: "timelineColorConsoleChooseLut"
                            Layout.fillWidth: true
                            text: root.currentClip.colorLutName
                                  ? String(root.currentClip.colorLutName) : "Choose LUT…"
                            iconName: "folder"
                            compact: true
                            enabled: root.isVideoClip && !root.selectedTrackLocked
                            disabledReason: "Select one unlocked video clip."
                            tooltipText: "Apply a validated CUBE LUT after the camera input transform."
                            onClicked: lutFileDialog.open()
                        }

                        ActionButton {
                            objectName: "timelineColorConsoleClearLut"
                            text: "Clear"
                            iconName: "close"
                            compact: true
                            enabled: root.isVideoClip && !root.selectedTrackLocked
                                     && String(root.currentClip.colorLutPath || "").length > 0
                            disabledReason: "No finishing LUT is assigned."
                            onClicked: root.clearLut()
                        }
                    }

                    Label {
                        id: colorLutLocationLabel
                        Layout.fillWidth: true
                        text: root.colorLutDisplayName
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        Accessible.description: String(root.currentClip.colorLutPath || root.colorLutDisplayName)
                        HoverHandler { id: colorLutLocationHover }
                        AppToolTip {
                            targetItem: colorLutLocationLabel
                            visible: colorLutLocationLabel.visible && colorLutLocationHover.hovered
                                     && String(root.currentClip.colorLutPath || "").length > 0
                            delay: 350
                            text: String(root.currentClip.colorLutPath || root.colorLutDisplayName)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "The selected CUBE is validated for its declared input and output color spaces before it reaches the shared render recipe."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
