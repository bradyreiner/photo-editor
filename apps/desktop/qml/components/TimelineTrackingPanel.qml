pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Components.GlassSurface {
    id: root

    required property var theme
    required property var timelineDocument
    required property var currentClip
    required property var motionTrackSources
    property var sceneTrackSources: timelineDocument.sceneTrackSources || []
    property string sceneSourceClipId: ""
    property var scenePointIds: []
    required property bool seedSelectionActive
    required property bool seedSelectionAllowed
    required property var seedRectNormalized
    property bool surfaceCorrectionAvailable: false
    property string statusMessage: ""
    signal seedSelectionRequested
    signal seedSelectionCancelled
    signal seedRectChanged(var rect)
    signal sceneSourceChanged(string clipId)
    signal scenePointSelectionRequested(string clipId, var selectedPointIds)

    property string selectedMotionSourceClipId: ""
    property string selectedSurfaceSourceClipId: ""
    property bool surfaceExclusionSelectionPending: false
    property var surfaceExclusionBoxes: []
    signal surfaceExclusionSelectionRequested
    signal surfaceCorrectionRequested
    signal rotoCorrectionRequested
    implicitWidth: trackingLayout.implicitWidth + theme.panelPaddingCompact * 2
    implicitHeight: trackingLayout.implicitHeight + theme.panelPaddingCompact * 2

    readonly property string selectedClipId: String(currentClip && currentClip.clipId || "")
    readonly property bool hasSelectedVideoClip: Boolean(currentClip && currentClip.clipId) && String(currentClip.trackKind || "video") === "video" && String(currentClip.mediaType || "video") === "video"
    readonly property bool hasSelectedTargetClip: Boolean(currentClip && currentClip.clipId) && String(currentClip.trackKind || "") === "video" && String(currentClip.mediaType || "") !== "adjustment"
    readonly property bool hasSeedRect: Number(seedRectNormalized && seedRectNormalized.width || 0) > 0 && Number(seedRectNormalized && seedRectNormalized.height || 0) > 0
    readonly property bool hasMotionTrack: Boolean(currentClip && currentClip.motionTracking && currentClip.motionTracking.samples && currentClip.motionTracking.samples.length > 0)
    readonly property string selectedSceneSourceError: selectedSceneSource && typeof selectedSceneSource.error === "string"
                                                       ? selectedSceneSource.error : ""
    readonly property real selectedSceneMeanReprojectionError: {
        if (!selectedSceneSource || !selectedSceneSource.points)
            return -1
        const solveError = Number(selectedSceneSource.reprojectionError)
        if (isFinite(solveError) && solveError >= 0)
            return solveError
        let total = 0
        let count = 0
        const points = selectedSceneSource.points
        for (let index = 0; index < points.length; ++index) {
            const error = Number(points[index] && points[index].error)
            if (isFinite(error) && error >= 0) {
                total += error
                count += 1
            }
        }
        return count > 0 ? total / count : -1
    }
    readonly property int selectedMotionSourceIndex: {
        const rows = motionTrackSources || [];
        for (let index = 0; index < rows.length; ++index) {
            if (String(rows[index].clipId || "") === selectedMotionSourceClipId)
                return index;
        }
        return -1;
    }
    readonly property int selectedSceneSourceIndex: {
        const rows = sceneTrackSources || [];
        for (let index = 0; index < rows.length; ++index) {
            if (String(rows[index].clipId || "") === sceneSourceClipId)
                return index;
        }
        return -1;
    }
    readonly property var selectedSceneSource: selectedSceneSourceIndex >= 0 ? sceneTrackSources[selectedSceneSourceIndex] : null
    readonly property bool selectedSceneSourceReady: selectedSceneSource !== null && Number(selectedSceneSource.pointCount || 0) >= 3 && selectedSceneSourceError.length === 0
    readonly property int selectedScenePointCount: (scenePointIds || []).length
    readonly property bool canAttachScene: hasSelectedTargetClip && ["title", "caption", "photo", "image"].indexOf(String(currentClip.mediaType || "")) >= 0
    readonly property bool hasSurfaceAttachment: currentClip && currentClip.sceneAttachmentEnabled === true && String(currentClip.sceneAttachment && currentClip.sceneAttachment.mode || "") === "surface2d"
    readonly property bool hasSceneAttachment: currentClip && currentClip.sceneAttachmentEnabled === true && !root.hasSurfaceAttachment
    readonly property int selectedSurfaceSourceIndex: {
        const rows = timelineDocument.surfaceTrackSources || [];
        for (let index = 0; index < rows.length; ++index) {
            if (String(rows[index].clipId || "") === selectedSurfaceSourceClipId)
                return index;
        }
        return -1;
    }
    readonly property var currentSurfaceTrack: {
        const tracking = currentClip && currentClip.motionTracking ? currentClip.motionTracking : ({})
        return tracking.surfaceTrackDraft || tracking.surfaceTrack || ({})
    }
    readonly property var currentSurfaceSeed: currentSurfaceTrack.seed || ({})
    readonly property bool hasCurrentSurfaceTrack: currentClip && currentClip.surfaceTrackReady === true
    readonly property bool hasRepairableSurfaceTrack: currentClip && currentClip.surfaceTrackRepairable === true
    readonly property var sceneAttachment: currentClip && currentClip.sceneAttachment ? currentClip.sceneAttachment : ({})
    readonly property bool attachmentOcclusionAvailable: {
        const sourceId = String(sceneAttachment.sourceClipId || "")
        if (!sourceId)
            return false
        const rows = (timelineDocument.sceneTrackSources || []).concat(timelineDocument.surfaceTrackSources || [])
        for (let index = 0; index < rows.length; ++index)
            if (String(rows[index].clipId || "") === sourceId)
                return rows[index].occlusionAvailable === true
        return false
    }

    function sourceClipLabel(clip) {
        const label = String(clip && (clip.displayLabel || clip.label) || "");
        return label.length > 0 ? label : "Selected clip";
    }

    function setSourceFromCurrentClip() {
        if (selectedMotionSourceIndex >= 0)
            return;
        const rows = motionTrackSources || [];
        selectedMotionSourceClipId = rows.length > 0 ? String(rows[0].clipId || "") : "";
    }

    function setSurfaceSourceFromCurrentClip() {
        if (selectedSurfaceSourceIndex >= 0)
            return;
        const currentId = String(currentClip && currentClip.clipId || "");
        const rows = timelineDocument.surfaceTrackSources || [];
        for (let index = 0; index < rows.length; ++index) {
            if (String(rows[index].clipId || "") === currentId) {
                selectedSurfaceSourceClipId = currentId;
                return;
            }
        }
        selectedSurfaceSourceClipId = rows.length > 0 ? String(rows[0].clipId || "") : "";
    }

    function acceptSurfaceExclusionSelection(rect) {
        if (!surfaceExclusionSelectionPending)
            return false;
        surfaceExclusionSelectionPending = false;
        const x = Math.max(0, Math.min(1, Number(rect && rect.x || 0)));
        const y = Math.max(0, Math.min(1, Number(rect && rect.y || 0)));
        const width = Math.max(0, Math.min(1 - x, Number(rect && rect.width || 0)));
        const height = Math.max(0, Math.min(1 - y, Number(rect && rect.height || 0)));
        if (width < 0.015 || height < 0.015)
            return false;
        surfaceExclusionBoxes = (surfaceExclusionBoxes || []).concat([{"x":x, "y":y, "width":width, "height":height}]);
        return true;
    }

    function surfaceRetrackSeed() {
        const next = Object.assign({}, currentSurfaceSeed);
        next.exclusions = root.surfaceExclusionBoxes.slice(0);
        next.correctionFrames = (currentSurfaceSeed.correctionFrames || []).slice(0);
        return next;
    }

    onCurrentClipChanged: surfaceExclusionBoxes = (currentSurfaceSeed.exclusions || []).slice(0)

    function setSceneSourceFromCurrentClip() {
        if (selectedSceneSourceIndex >= 0)
            return;
        const currentId = String(currentClip && currentClip.clipId || "");
        const rows = sceneTrackSources || [];
        for (let index = 0; index < rows.length; ++index) {
            if (String(rows[index].clipId || "") === currentId) {
                sceneSourceChanged(currentId);
                return;
            }
        }
        if (rows.length > 0 && sceneSourceClipId.length === 0)
            sceneSourceChanged(String(rows[0].clipId || ""));
    }

    function applyScenePlacement(field, value) {
        const defaults = {
            "widthScale": 1,
            "offsetX": 0,
            "offsetY": 0,
            "offsetZ": 0,
            "rotationDegrees": 0
        };
        const next = Object.assign({}, defaults, currentClip && currentClip.sceneAttachment || {});
        next[field] = Number(value);
        timelineDocument.setSelectedSceneAttachmentPlacement(next);
    }

    function setSeedValue(field, percentValue) {
        const next = Object.assign({}, seedRectNormalized || {});
        next[field] = Math.max(0, Math.min(1, Number(percentValue || 0) / 100));
        next.x = Math.min(Number(next.x || 0), 1 - Number(next.width || 0));
        next.y = Math.min(Number(next.y || 0), 1 - Number(next.height || 0));
        next.width = Math.min(Number(next.width || 0), 1 - Number(next.x || 0));
        next.height = Math.min(Number(next.height || 0), 1 - Number(next.y || 0));
        seedRectChanged(next);
    }

    onMotionTrackSourcesChanged: setSourceFromCurrentClip()
    Connections {
        target: root.timelineDocument
        function onClipsChanged() { root.setSurfaceSourceFromCurrentClip() }
    }
    onSceneTrackSourcesChanged: setSceneSourceFromCurrentClip()
    Component.onCompleted: {
        setSourceFromCurrentClip();
        setSurfaceSourceFromCurrentClip();
        surfaceExclusionBoxes = (currentSurfaceSeed.exclusions || []).slice(0);
        setSceneSourceFromCurrentClip();
    }

    radius: theme.radiusSurface
    color: theme.ink850
    border.width: 1
    border.color: theme.alpha(theme.glassEdge, 0.36)

    Flickable {
        id: panelScrollView
        objectName: "timelineTrackingPanelScrollView"
        anchors.fill: parent
        contentWidth: width
        contentHeight: trackingLayout.implicitHeight + root.theme.panelPaddingCompact * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: trackingLayout
            x: root.theme.panelPaddingCompact
            y: root.theme.panelPaddingCompact
            width: Math.max(1, panelScrollView.width - root.theme.panelPaddingCompact * 2)
            spacing: root.theme.space8

            ColumnLayout {
                objectName: "timelineSurfaceTrackingSection"
                Layout.fillWidth: true
                spacing: root.theme.space6

                Label {
                    text: "Surface Tracking"
                    color: root.theme.textStrong
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeControl
                    font.weight: Font.DemiBold
                }

                Label {
                    text: "Attach a graphic to a tracked 2D plane in the source shot."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: "Track one physical surface. Parallax can move other surfaces differently. Choose a video source, set a region, then track."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space6
                    Components.ActionButton {
                        objectName: "timelineSurfaceSeedButton"
                        text: root.seedSelectionActive ? "Cancel Region" : "Choose surface"
                        iconName: root.seedSelectionActive ? "close" : "select"
                        compact: true
                        enabled: root.hasSelectedVideoClip && root.seedSelectionAllowed && !root.timelineDocument.trackingBusy
                        onClicked: root.seedSelectionActive ? root.seedSelectionCancelled() : root.seedSelectionRequested()
                    }
                    Components.ActionButton {
                        objectName: "timelineAnalyzeSurfaceButton"
                        text: "Track Surface"
                        iconName: "motion"
                        tone: "primary"
                        compact: true
                        enabled: root.hasSelectedVideoClip && !root.hasCurrentSurfaceTrack && root.hasSeedRect && root.timelineDocument.surfaceTrackingAvailable === true && !root.timelineDocument.trackingBusy
                        disabledReason: root.timelineDocument.surfaceTrackingAvailable !== true ? "Surface tracking is unavailable." : !root.hasSelectedVideoClip ? "Select a video source clip." : root.hasCurrentSurfaceTrack ? "Use Retrack for exclusions or Correct current frame for a local repair." : "Choose a surface region first."
                        onClicked: root.timelineDocument.analyzeSelectedClipSurface(root.seedRectNormalized)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space6
                    Label {
                        text: "Tracked source"
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                    }
                    Components.ToneComboBox {
                        objectName: "timelineSurfaceTrackSourceCombo"
                        Layout.fillWidth: true
                        model: root.timelineDocument.surfaceTrackSources || []
                        textRole: "displayLabel"
                        accessibleLabel: "Surface tracking source"
                        currentIndex: root.selectedSurfaceSourceIndex
                        enabled: count > 0 && !root.timelineDocument.trackingBusy
                        onActivated: function(index) {
                            const rows = root.timelineDocument.surfaceTrackSources || [];
                            if (index >= 0 && index < rows.length)
                                root.selectedSurfaceSourceClipId = String(rows[index].clipId || "");
                        }
                    }
                    Components.ActionButton {
                        objectName: "timelineAttachSurfaceTrackButton"
                        text: root.hasSurfaceAttachment ? "Attached" : "Attach surface"
                        iconName: root.hasSurfaceAttachment ? "check" : "link"
                        tone: root.hasSurfaceAttachment ? "quiet" : "primary"
                        compact: true
                        enabled: !root.hasSurfaceAttachment && root.selectedSurfaceSourceIndex >= 0 && root.canAttachScene && !root.timelineDocument.trackingBusy
                        disabledReason: !root.canAttachScene ? "Select a title, caption, or photo clip as the target." : "Track a surface on a source clip first."
                        onClicked: root.timelineDocument.attachSurfaceTrackToSelectedClip(root.selectedSurfaceSourceClipId)
                    }
                    Components.ActionButton {
                        objectName: "timelineDetachSurfaceTrackButton"
                        text: "Detach"
                        iconName: "unlink"
                        compact: true
                        visible: root.hasSurfaceAttachment
                        enabled: !root.timelineDocument.trackingBusy
                        onClicked: root.timelineDocument.detachSelectedSceneAttachment()
                    }
                }

                Label {
                    objectName: "timelineSurfaceTrackStatus"
                    Layout.fillWidth: true
                    text: {
                        if (root.selectedSurfaceSourceIndex < 0)
                            return "Track a source surface to make it available for attachment.";
                        const source = root.timelineDocument.surfaceTrackSources[root.selectedSurfaceSourceIndex];
                        return "Confidence " + Math.round(Number(source.minConfidence || 0) * 100) + "% min · " + Math.round(Number(source.meanErrorPx || 0) * 10) / 10 + " px fit error · " + Number(source.sampleCount || 0) + " samples";
                    }
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    objectName: "timelineSurfaceRepairControls"
                    Layout.fillWidth: true
                    visible: root.hasRepairableSurfaceTrack && root.hasSelectedVideoClip
                    spacing: root.theme.space6
                    Components.ActionButton {
                        objectName: "timelineAddSurfaceExclusionButton"
                        text: "Add exclusion"
                        iconName: "select"
                        compact: true
                        enabled: !root.timelineDocument.trackingBusy && !root.surfaceExclusionSelectionPending
                        tooltipText: "Draw a region on the source frame to ignore, then retrack."
                        onClicked: {
                            root.surfaceExclusionSelectionPending = true;
                            root.surfaceExclusionSelectionRequested();
                        }
                    }
                    Label {
                        objectName: "timelineSurfaceExclusionCount"
                        Layout.fillWidth: true
                        text: String(root.surfaceExclusionBoxes.length) + (root.surfaceExclusionBoxes.length === 1 ? " exclusion" : " exclusions")
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                    }
                    Components.ActionButton {
                        objectName: "timelineRemoveSurfaceExclusionButton"
                        text: "Remove last"
                        iconName: "undo"
                        compact: true
                        enabled: root.hasRepairableSurfaceTrack && (root.surfaceExclusionBoxes.length > 0
                                 || (root.currentSurfaceSeed.correctionFrames || []).length > 0)
                                 && !root.timelineDocument.trackingBusy
                        onClicked: root.surfaceExclusionBoxes = root.surfaceExclusionBoxes.slice(0, -1)
                    }
                    Components.ActionButton {
                        objectName: "timelineRetrackSurfaceButton"
                        text: "Retrack"
                        iconName: "refresh"
                        tone: "primary"
                        compact: true
                        enabled: root.surfaceExclusionBoxes.length > 0 && !root.timelineDocument.trackingBusy
                        tooltipText: "Reanalyze this source while ignoring the saved exclusion regions."
                        onClicked: root.timelineDocument.analyzeSelectedClipSurface(root.surfaceRetrackSeed())
                    }
                }

                Label {
                    objectName: "timelineSurfaceDraftRepairStatus"
                    Layout.fillWidth: true
                    visible: root.hasRepairableSurfaceTrack && !root.hasCurrentSurfaceTrack
                    text: "Cached surface solve needs repair. Correct the guide corners and retrack; attach becomes available after a successful solve."
                    color: root.theme.warning
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }

                Components.ActionButton {
                    objectName: "timelineCorrectSurfaceAtPlayheadButton"
                    Layout.alignment: Qt.AlignLeft
                    visible: root.hasRepairableSurfaceTrack && root.hasSelectedVideoClip
                    text: root.hasCurrentSurfaceTrack ? "Correct current frame" : "Correct corners"
                    iconName: "select"
                    compact: true
                    enabled: root.surfaceCorrectionAvailable && !root.timelineDocument.trackingBusy
                    disabledReason: root.surfaceCorrectionAvailable ? "An analysis is already running." : "Select the tracked source at its paused playhead in Program."
                    tooltipText: root.hasCurrentSurfaceTrack
                                  ? "Adjust the tracked corners on the current Program frame. The corrected frame is retracked."
                                  : "Correct the saved source-frame guide and retrack this cached solve."
                    onClicked: root.surfaceCorrectionRequested()
                }

                Label {
                    objectName: "timelineSurfaceAttachmentWarning"
                    Layout.fillWidth: true
                    visible: root.hasSurfaceAttachment && root.currentClip.sceneAttachmentReady !== true
                    text: "Surface attachment unavailable: " + String(root.currentClip.sceneAttachmentError || "source track is stale")
                    color: root.theme.warning
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }

                GridLayout {
                    objectName: "timelineSurfacePlacementControls"
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: root.theme.space8
                    rowSpacing: root.theme.space4
                    visible: root.hasSurfaceAttachment && root.currentClip.sceneAttachmentReady === true
                    Repeater {
                        model: [
                            {"key":"widthScale", "label":"Width scale", "from":0.1, "to":4, "step":0.05},
                            {"key":"offsetX", "label":"Offset X", "from":-1, "to":1, "step":0.01},
                            {"key":"offsetY", "label":"Offset Y", "from":-1, "to":1, "step":0.01},
                            {"key":"rotationDegrees", "label":"Rotation", "from":-180, "to":180, "step":1}
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            readonly property real displayScale: String(modelData.key) === "rotationDegrees" ? 1 : 100
                            Layout.fillWidth: true
                            spacing: root.theme.space4
                            Label {
                                Layout.preferredWidth: root.theme.scaled(76)
                                text: String(modelData.label)
                                color: root.theme.textMuted
                                font.family: root.theme.fontUi
                                font.pixelSize: root.theme.typeCaption
                            }
                            Components.ToneSpinBox {
                                Layout.fillWidth: true
                                from: Math.round(Number(modelData.from) * displayScale)
                                to: Math.round(Number(modelData.to) * displayScale)
                                stepSize: Math.max(1, Math.round(Number(modelData.step) * displayScale))
                                editable: true
                                value: Math.round(Number(root.currentClip.sceneAttachment && root.currentClip.sceneAttachment[modelData.key] !== undefined ? root.currentClip.sceneAttachment[modelData.key] : (modelData.key === "widthScale" ? 1 : 0)) * displayScale)
                                valueUnit: String(modelData.key) === "rotationDegrees" ? "°" : "%"
                                accessibleLabel: "Surface attachment " + String(modelData.label)
                                enabled: root.currentClip.sceneAttachmentReady === true && !root.timelineDocument.trackingBusy
                                onValueModified: root.applyScenePlacement(String(modelData.key), value / displayScale)
                            }
                        }
                    }
                }

                Components.ToneCheckBox {
                    objectName: "timelineSurfaceAttachmentOcclusionCheckBox"
                    visible: root.hasSurfaceAttachment
                    text: "Use foreground matte for occlusion"
                    accessibleLabel: "Use foreground matte for surface attachment occlusion"
                    accessibleDescription: "Composite foreground subjects in front of this attached graphic using the source clip's generated mask."
                    checked: root.sceneAttachment.occlusionEnabled === true
                    enabled: root.currentClip.sceneAttachmentReady === true && root.attachmentOcclusionAvailable && !root.timelineDocument.trackingBusy
                    onToggled: root.timelineDocument.setSelectedSceneAttachmentOcclusion(checked)
                }

                Label {
                    objectName: "timelineSurfaceOcclusionUnavailableLabel"
                    visible: root.hasSurfaceAttachment && !root.attachmentOcclusionAvailable
                    text: "Select the source video and generate an occlusion mask to let foreground subjects pass in front of this graphic."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.space8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space2

                    Label {
                        text: "Scene Camera"
                        color: root.theme.textStrong
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeControl
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Reconstruct camera motion, choose three visible points on a plane, then attach a title or image through the shot."
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                }

                Components.ActionButton {
                    objectName: "timelineSolveSceneCameraButton"
                    text: "Solve Scene Camera"
                    iconName: "motion"
                    tone: "primary"
                    compact: true
                    enabled: root.hasSelectedVideoClip && root.timelineDocument.sceneTrackingAvailable === true && !root.timelineDocument.trackingBusy
                    disabledReason: root.timelineDocument.sceneTrackingAvailable !== true ? "Scene reconstruction runtime is unavailable." : root.hasSelectedVideoClip ? "Scene camera analysis is already running." : "Select a video source clip before solving its camera."
                    tooltipText: "Reconstruct camera motion and static scene features for this video source."
                    onClicked: root.timelineDocument.analyzeSelectedClipSceneCamera()
                }

            }

            ColumnLayout {
                objectName: "timelineSceneCameraDetails"
                Layout.fillWidth: true
                spacing: root.theme.space6
                visible: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space8

                    Label {
                        text: "Tracked source"
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                    }

                    Components.ToneComboBox {
                        objectName: "timelineSceneTrackSourceCombo"
                        Layout.fillWidth: true
                        model: root.sceneTrackSources || []
                        textRole: "displayLabel"
                        accessibleLabel: "Scene camera source"
                        currentIndex: root.selectedSceneSourceIndex
                        enabled: count > 0 && !root.timelineDocument.trackingBusy
                        onActivated: function (index) {
                            const rows = root.sceneTrackSources || [];
                            if (index >= 0 && index < rows.length)
                                root.sceneSourceChanged(String(rows[index].clipId || ""));
                        }
                    }

                    Components.ActionButton {
                        objectName: "timelineSelectScenePlanePointsButton"
                        text: "Select 3 Points"
                        iconName: "select"
                        compact: true
                        enabled: root.selectedSceneSourceReady && !root.timelineDocument.trackingBusy
                        disabledReason: root.selectedSceneSourceIndex < 0 ? "Solve a source clip first." : "The reconstruction needs at least three static scene features."
                        tooltipText: "Choose three widely spaced reconstructed features on one physical plane."
                        onClicked: root.scenePointSelectionRequested(root.sceneSourceClipId, root.scenePointIds)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space8

                    Label {
                        objectName: "timelineScenePlanePointStatus"
                        Layout.fillWidth: true
                        text: root.selectedSceneSourceIndex < 0 ? "Solve a source video to reconstruct static features." : root.selectedSceneSourceError.length > 0 ? root.selectedSceneSourceError : String(root.selectedScenePointCount) + " of 3 plane points selected · " + String(root.selectedSceneSource.pointCount || 0) + " static features" + (root.selectedSceneMeanReprojectionError >= 0 ? " · " + root.selectedSceneMeanReprojectionError.toFixed(1) + " px average feature error" : "")
                        color: root.selectedSceneSourceError.length > 0 ? root.theme.warning : root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                        wrapMode: Text.Wrap
                    }

                    Components.ActionButton {
                        objectName: "timelineAttachSceneTrackButton"
                        text: root.hasSceneAttachment ? "Attached" : "Attach Scene"
                        iconName: root.hasSceneAttachment ? "check" : "link"
                        tone: root.hasSceneAttachment ? "quiet" : "primary"
                        compact: true
                        enabled: !root.hasSceneAttachment && root.selectedSceneSourceIndex >= 0 && root.selectedScenePointCount === 3 && root.selectedSceneSourceReady && root.canAttachScene && !root.timelineDocument.trackingBusy
                        disabledReason: root.selectedScenePointCount !== 3 ? "Select three points on one static plane first." : !root.canAttachScene ? "Select a title, caption, or image clip as the target." : "Choose a solved scene source first."
                        tooltipText: "Align the selected title or image to the source shot timing and attach it to the plane defined by these three static points."
                        onClicked: root.timelineDocument.attachSceneTrackToSelectedClip(root.sceneSourceClipId, root.scenePointIds)
                    }

                    Components.ActionButton {
                        objectName: "timelineDetachSceneTrackButton"
                        text: "Detach"
                        iconName: "unlink"
                        compact: true
                        visible: root.hasSceneAttachment
                        enabled: !root.timelineDocument.trackingBusy
                        onClicked: root.timelineDocument.detachSelectedSceneAttachment()
                    }
                }

                Label {
                    objectName: "timelineSceneAttachmentWarning"
                    Layout.fillWidth: true
                    visible: root.hasSceneAttachment && root.currentClip.sceneAttachmentReady !== true
                    text: "Scene attachment unavailable: " + String(root.currentClip.sceneAttachmentError || "source solve or selected points are stale")
                    color: root.theme.warning
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: root.theme.space8
                    rowSpacing: root.theme.space4
                    visible: root.hasSceneAttachment && root.currentClip.sceneAttachmentReady === true

                    Repeater {
                        model: [
                            {
                                "key": "widthScale",
                                "label": "Width scale",
                                "from": 0.1,
                                "to": 4,
                                "step": 0.05
                            },
                            {
                                "key": "offsetX",
                                "label": "Offset X",
                                "from": -1,
                                "to": 1,
                                "step": 0.01
                            },
                            {
                                "key": "offsetY",
                                "label": "Offset Y",
                                "from": -1,
                                "to": 1,
                                "step": 0.01
                            },
                            {
                                "key": "offsetZ",
                                "label": "Depth",
                                "from": -1,
                                "to": 1,
                                "step": 0.01
                            },
                            {
                                "key": "rotationDegrees",
                                "label": "Rotation",
                                "from": -180,
                                "to": 180,
                                "step": 1
                            }
                        ]

                        delegate: RowLayout {
                            required property var modelData
                            readonly property real displayScale: String(modelData.key) === "rotationDegrees" ? 1 : 100
                            Layout.fillWidth: true
                            spacing: root.theme.space4

                            Label {
                                Layout.preferredWidth: root.theme.scaled(76)
                                text: String(modelData.label)
                                color: root.theme.textMuted
                                font.family: root.theme.fontUi
                                font.pixelSize: root.theme.typeCaption
                            }

                            Components.ToneSpinBox {
                                Layout.fillWidth: true
                                from: Math.round(Number(modelData.from) * displayScale)
                                to: Math.round(Number(modelData.to) * displayScale)
                                stepSize: Math.max(1, Math.round(Number(modelData.step) * displayScale))
                                editable: true
                                value: Math.round(Number(root.currentClip.sceneAttachment && root.currentClip.sceneAttachment[modelData.key] !== undefined ? root.currentClip.sceneAttachment[modelData.key] : (modelData.key === "widthScale" ? 1 : 0)) * displayScale)
                                valueUnit: String(modelData.key) === "rotationDegrees" ? "°" : "%"
                                accessibleLabel: "Scene attachment " + String(modelData.label)
                                enabled: root.currentClip.sceneAttachmentReady === true && !root.timelineDocument.trackingBusy
                                onValueModified: root.applyScenePlacement(String(modelData.key), value / displayScale)
                            }
                        }
                    }
                }

                Components.ToneCheckBox {
                    objectName: "timelineSceneAttachmentOcclusionCheckBox"
                    visible: root.hasSceneAttachment
                    text: "Use foreground matte for occlusion"
                    accessibleLabel: "Use foreground matte for scene attachment occlusion"
                    accessibleDescription: "Composite foreground subjects in front of this attached graphic using the source clip's generated mask."
                    checked: root.sceneAttachment.occlusionEnabled === true
                    enabled: root.currentClip.sceneAttachmentReady === true && root.attachmentOcclusionAvailable && !root.timelineDocument.trackingBusy
                    onToggled: root.timelineDocument.setSelectedSceneAttachmentOcclusion(checked)
                }

                Label {
                    objectName: "timelineSceneOcclusionUnavailableLabel"
                    visible: root.hasSceneAttachment && !root.attachmentOcclusionAvailable
                    text: "Select the source video and generate an occlusion mask to let foreground subjects pass in front of this graphic."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                objectName: "timelineOcclusionMaskControls"
                Layout.fillWidth: true
                spacing: root.theme.space6

                Components.ToneCheckBox {
                    id: temporalOcclusionMode
                    objectName: "timelineOcclusionTemporalModeCheckBox"
                    text: "SAM2 temporal"
                    accessibleLabel: "Generate temporal SAM2 occlusion mask"
                    accessibleDescription: "Use the seeded SAM2 video model for a temporally propagated occlusion mask."
                    checked: false
                    enabled: root.hasSelectedVideoClip && !root.timelineDocument.trackingBusy
                    visible: root.timelineDocument.rotoscopingAvailable === true
                }

                Components.ActionButton {
                    objectName: "timelineGenerateOcclusionMaskButton"
                    text: "Generate occlusion mask"
                    iconName: "sparkles"
                    tone: "primary"
                    compact: true
                    enabled: root.hasSelectedVideoClip && !root.timelineDocument.trackingBusy
                             && (!temporalOcclusionMode.checked || (root.timelineDocument.rotoscopingAvailable === true && root.hasSeedRect))
                    disabledReason: !root.hasSelectedVideoClip ? "Select the source video clip." : temporalOcclusionMode.checked && !root.hasSeedRect ? "Set a seed region before using SAM2." : "An analysis is already running."
                    tooltipText: temporalOcclusionMode.checked
                                  ? "Generate a box-seeded temporal SAM2 matte for use in front of attached graphics."
                                  : "Generate a local per-frame foreground matte for use in front of attached graphics."
                    onClicked: root.timelineDocument.analyzeSelectedClipOcclusion(root.seedRectNormalized, temporalOcclusionMode.checked)
                }

                Label {
                    objectName: "timelineOcclusionMaskDisclosure"
                    Layout.fillWidth: true
                    text: temporalOcclusionMode.checked
                          ? "SAM2 follows the seeded subject; its local model and seed region are required."
                          : "The generated mask is used only to place attached graphics behind foreground subjects."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.space8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space2

                    Label {
                        text: "Motion Tracking"
                        color: root.theme.textStrong
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeControl
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.hasSelectedVideoClip ? root.sourceClipLabel(root.currentClip) + " · seed on first frame" : "Select a video clip to track motion or create a cutout."
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                }

                Components.StatusChip {
                    objectName: "timelineMotionTrackingStatusChip"
                    visible: root.hasSelectedVideoClip
                    text: root.timelineDocument.trackingBusy ? "Analyzing" : root.currentClip.foregroundCutoutReady === true ? "Matte ready" : root.hasMotionTrack ? "Tracked" : "Idle"
                    tone: root.timelineDocument.trackingBusy ? "info" : (root.currentClip.foregroundCutoutReady === true || root.hasMotionTrack) ? "success" : "neutral"
                    busy: root.timelineDocument.trackingBusy
                    compact: true
                    detailText: root.timelineDocument.trackingBusy ? Math.round(Number(root.timelineDocument.trackingProgress || 0) * 100) + "%" : ""
                }

                Components.ActionButton {
                    objectName: "timelineCancelTrackingButton"
                    visible: root.timelineDocument.trackingBusy
                    text: "Cancel"
                    iconName: "close"
                    compact: true
                    tone: "quiet"
                    tooltipText: "Stop the active motion, cutout, or rotoscope analysis."
                    onClicked: root.timelineDocument.cancelTrackingAnalysis()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.space6

                Components.ActionButton {
                    objectName: "timelineMotionSeedButton"
                    text: root.seedSelectionActive ? "Cancel Region" : "Set Region"
                    iconName: root.seedSelectionActive ? "close" : "select"
                    compact: true
                    active: root.seedSelectionActive
                    enabled: root.hasSelectedVideoClip && root.seedSelectionAllowed && !root.timelineDocument.trackingBusy
                    disabledReason: !root.hasSelectedVideoClip ? "Select a video clip before choosing a tracking region." : "Drag seeding requires Fit framing and reset transforms. Use normalized seed values."
                    tooltipText: "Drag a rectangle around the subject in the first frame."
                    onClicked: {
                        if (root.seedSelectionActive)
                            root.seedSelectionCancelled();
                        else
                            root.seedSelectionRequested();
                    }
                }

                Components.ActionButton {
                    objectName: "timelineAnalyzeMotionButton"
                    text: "Track Motion"
                    iconName: "motion"
                    tone: "primary"
                    compact: true
                    enabled: !root.timelineDocument.trackingBusy && root.hasSelectedVideoClip && root.hasSeedRect
                    disabledReason: root.hasSelectedVideoClip ? "Set a seed rectangle over the subject first." : "Select a video clip before tracking motion."
                    tooltipText: "Track the selected region through the source clip."
                    onClicked: root.timelineDocument.analyzeSelectedClipMotion(root.seedRectNormalized)
                }

                Components.ActionButton {
                    objectName: "timelineAnalyzeCutoutButton"
                    text: "Auto Cutout"
                    iconName: "sparkles"
                    compact: true
                    enabled: root.hasSelectedVideoClip && !root.timelineDocument.trackingBusy
                    disabledReason: "Select a video clip before creating a foreground cutout."
                    tooltipText: "Create a per-frame foreground matte for the selected source clip."
                    onClicked: root.timelineDocument.analyzeSelectedClipCutout()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.space8

                Components.ActionButton {
                    objectName: "timelineAnalyzeRotoscopeButton"
                    text: "SAM2 Rotoscope"
                    iconName: "motion"
                    compact: true
                    enabled: root.hasSelectedVideoClip && root.timelineDocument.rotoscopingAvailable === true && root.hasSeedRect && !root.timelineDocument.trackingBusy
                    disabledReason: root.timelineDocument.rotoscopingAvailable !== true ? "Set up the local SAM2 runtime and checkpoint to use Rotoscope." : !root.hasSelectedVideoClip ? "Select a video clip before rotoscoping." : root.timelineDocument.trackingBusy ? "An analysis is already running." : "Set a seed rectangle over the subject first."
                    tooltipText: "Propagate the seeded subject mask through the clip with local SAM2."
                    onClicked: root.timelineDocument.analyzeSelectedClipRotoscope(root.seedRectNormalized)
                }

                Components.ActionButton {
                    objectName: "timelineCorrectRotoscopeButton"
                    text: "Refine Matte"
                    iconName: "select"
                    compact: true
                    visible: root.hasSelectedVideoClip && root.currentClip.rotoCorrectionReady === true
                    enabled: visible && !root.timelineDocument.trackingBusy
                    disabledReason: "Pause the selected source clip inside the analyzed shot to add SAM2 correction points."
                    tooltipText: "Add positive or negative SAM2 points on the paused source frame and re-run the temporal matte."
                    onClicked: root.rotoCorrectionRequested()
                }

                Label {
                    objectName: "timelineRotoscopeSetupDisclosure"
                    Layout.fillWidth: true
                    text: root.timelineDocument.rotoscopingAvailable === true ? "Frame-by-frame subject matte from the selected seed." : "Temporal rotoscoping requires the local SAM2 runtime and checkpoint; model files are not downloaded automatically."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }
            }

            Label {
                visible: root.hasSelectedVideoClip && !root.seedSelectionAllowed
                Layout.fillWidth: true
                text: "Drag selection needs Fit framing and reset transforms. Set source-normalized values below; this tracker still needs reset transforms before analysis."
                color: root.theme.warning
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeCaption
                wrapMode: Text.Wrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: root.theme.space4

                Label {
                    text: "SEED REGION · SOURCE %"
                    color: root.theme.textMuted
                    font.family: root.theme.fontMono
                    font.pixelSize: root.theme.typeMicro
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: root.theme.space8
                    rowSpacing: root.theme.space4

                    Repeater {
                        model: [
                            {
                                "key": "x",
                                "label": "X"
                            },
                            {
                                "key": "y",
                                "label": "Y"
                            },
                            {
                                "key": "width",
                                "label": "W"
                            },
                            {
                                "key": "height",
                                "label": "H"
                            }
                        ]

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: root.theme.space4

                            Label {
                                Layout.preferredWidth: root.theme.scaled(18)
                                text: String(modelData.label)
                                color: root.theme.textMuted
                                font.family: root.theme.fontMono
                                font.pixelSize: root.theme.typeCaption
                            }

                            Components.ToneSpinBox {
                                objectName: "timelineMotionSeed" + String(modelData.key) + "PercentSpinBox"
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                stepSize: 1
                                editable: true
                                value: Math.round(Number(root.seedRectNormalized[modelData.key] || 0) * 100)
                                valueUnit: "percent"
                                accessibleLabel: "Seed region " + String(modelData.label) + " percent"
                                accessibleDescription: "Source-normalized " + String(modelData.label) + " coordinate or size from zero to one hundred percent."
                                enabled: root.hasSelectedVideoClip && !root.timelineDocument.trackingBusy
                                onValueModified: root.setSeedValue(String(modelData.key), value)
                            }
                        }
                    }
                }
            }

            Components.ProgressStrip {
                objectName: "timelineMotionTrackingProgress"
                visible: root.timelineDocument.trackingBusy
                Layout.fillWidth: true
                Layout.preferredHeight: 3
                progress: Number(root.timelineDocument.trackingProgress || 0)
                accent: root.theme.accentCool400
            }

            Label {
                objectName: "timelineMotionTrackingStatusLabel"
                Layout.fillWidth: true
                visible: String(root.timelineDocument.trackingStatus || "").length > 0 || root.statusMessage.length > 0
                text: root.statusMessage.length > 0 ? root.statusMessage : String(root.timelineDocument.trackingStatus || "")
                color: root.timelineDocument.trackingBusy ? root.theme.textMain : root.theme.textMuted
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeCaption
                wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: root.theme.alpha(root.theme.glassEdge, 0.20)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.space8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space2

                    Label {
                        text: "Attach Motion"
                        color: root.theme.textStrong
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeLabel
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "To selected clip: " + (root.hasSelectedVideoClip ? root.sourceClipLabel(root.currentClip) : (root.hasSelectedTargetClip ? root.sourceClipLabel(root.currentClip) : "none"))
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                        elide: Text.ElideRight
                    }
                }

                Components.ToneComboBox {
                    id: motionSourceCombo
                    objectName: "timelineMotionTrackSourceCombo"
                    Layout.preferredWidth: Math.min(208, Math.max(132, root.width * 0.38))
                    Layout.maximumWidth: 220
                    model: root.motionTrackSources || []
                    textRole: "displayLabel"
                    accessibleLabel: "Tracked motion source"
                    currentIndex: root.selectedMotionSourceIndex
                    enabled: count > 0 && !root.timelineDocument.trackingBusy
                    onActivated: function (index) {
                        const rows = root.motionTrackSources || [];
                        if (index >= 0 && index < rows.length)
                            root.selectedMotionSourceClipId = String(rows[index].clipId || "");
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.space8

                Label {
                    Layout.fillWidth: true
                    text: root.selectedMotionSourceIndex >= 0 ? "Tracked source: " + String(root.motionTrackSources[root.selectedMotionSourceIndex].displayLabel || "Clip") : "Track a source clip to make it available here."
                    color: root.selectedMotionSourceIndex >= 0 ? root.theme.textMuted : root.theme.textFaint
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }

                Components.ActionButton {
                    objectName: "timelineAttachMotionTrackButton"
                    text: "Attach"
                    iconName: "link"
                    compact: true
                    enabled: root.selectedMotionSourceIndex >= 0 && root.hasSelectedTargetClip && root.selectedMotionSourceClipId !== root.selectedClipId && !root.timelineDocument.trackingBusy
                    disabledReason: root.selectedMotionSourceIndex < 0 ? "Track a source clip first." : "Select a different target clip."
                    tooltipText: "Apply the source motion as editable Transform keyframes on the selected clip."
                    onClicked: root.timelineDocument.attachMotionTrackToSelectedClip(root.selectedMotionSourceClipId)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.space8

                Components.ToneCheckBox {
                    objectName: "timelineForegroundCutoutEnabledCheckBox"
                    text: "Foreground cutout"
                    accessibleLabel: "Enable foreground cutout"
                    accessibleDescription: "Apply the analyzed per-frame foreground matte to this video clip."
                    allowLabelWrap: true
                    Layout.fillWidth: true
                    checked: root.currentClip && root.currentClip.foregroundCutoutEnabled === true
                    enabled: root.hasSelectedVideoClip && root.currentClip.foregroundCutoutReady === true && !root.timelineDocument.trackingBusy
                    onToggled: root.timelineDocument.setSelectedClipForegroundCutout(checked)
                }

                Item {
                    Layout.fillWidth: true
                }

                Components.StatusChip {
                    visible: root.currentClip && root.currentClip.foregroundCutoutReady === true
                    text: "Matte ready"
                    tone: "success"
                    compact: true
                }
            }

            Label {
                objectName: "timelineForegroundCutoutUnavailableLabel"
                Layout.fillWidth: true
                visible: root.currentClip && root.currentClip.foregroundCutoutEnabled === true && root.currentClip.foregroundCutoutReady !== true
                text: "This clip's cutout is stale or incomplete. Analyze it again before using the matte."
                color: root.theme.warning
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeCaption
                wrapMode: Text.Wrap
            }
        }
    }
}
