pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import PhotoEditor
import "SceneCameraNavigation.js" as SceneNavigation

Item {
    id: root
    objectName: "depthMotionWorkspace"
    property bool sceneHosted: false
    property var catalog: null
    property bool recordingPending: false
    property bool sourceNeedsInitialSeek: false
    property bool primingSourcePreview: false
    property bool sourceSeekIssued: false
    property real sourceSeekSeconds: 0
    function seekSourceTrim(): void {
        if (!root.visible || !sourceNeedsInitialSeek || !sourcePlayer.seekable || sourceSeekIssued) return
        sourceSeekIssued = true
        sourcePlayer.position = Math.round(sourceSeekSeconds * 1000)
        sourcePlayer.play()
    }
    function previewSource(seconds: real): void {
        sourceSeekSeconds = seconds
        sourceNeedsInitialSeek = true; sourceSeekIssued = false; primingSourcePreview = true
        sourcePreviewDeadline.restart()
        sourcePlayer.play(); seekSourceTrim()
    }
    readonly property bool sourceIsVideo: /\.(mp4|mov|mkv|m4v)(\?.*)?$/i.test(sourcePath)
    readonly property bool sourcePreviewActive: sourceIsVideo && (settingsChanged || view.currentIndex === 2)
    readonly property real sourceSeconds: Number((state.sourceInfo || {}).duration || sourcePlayer.duration / 1000)
    readonly property real trimIn: startTime.value / 1000
    readonly property real trimOut: trimIn + duration.value / 1000
    function syncTrimRange(): void {
        // Restore both handles together after the source duration is known.
        // Setting one handle while the other is still clamped loses the range.
        Qt.callLater(function() { trimRange.setValues(root.trimIn, root.trimOut) })
    }
    onSourceSecondsChanged: syncTrimRange()
    onTrimInChanged: syncTrimRange()
    onTrimOutChanged: syncTrimRange()
    function setTrim(first: real, last: real): void {
        const limit = sourceSeconds > 0 ? sourceSeconds : 86400
        const end = Math.max(1 / 24, Math.min(limit, last))
        const begin = Math.max(0, Math.min(end - 1 / 24, first))
        startTime.value = Math.round(begin * 1000)
        duration.value = Math.round(Math.min(12, end - begin) * 1000)
        settingsChanged = true
        previewSource(trimIn)
    }
    onSourcePathChanged: {
        sourcePlayer.pause(); settingsChanged = true; sourceNeedsInitialSeek = false; primingSourcePreview = false
        const path = sourcePath
        Qt.callLater(function() {
            if (root.sourcePath !== path) return
            depth.inspectSource(path)
            if (root.sourceIsVideo) root.previewSource(root.trimIn)
        })
    }
    function startRecording(): void {
        if (!state.recordingLayersAvailable) { recordingPending = true; depth.prepareDepthVideo(); return }
        recordingControls.startRecording()
    }
    signal exitRequested()
    signal libraryScenesRequested()
    property alias backend: depth
    property alias pointCloudViewport: cloud
    property string sourcePath: ""
    property string depthPath: ""
    property bool settingsChanged: true
    property string observedProject: ""
    property string exportKind: "movie"
    property bool inspectorVisible: true
    readonly property bool compact: width < 760
    readonly property bool liveView: view.currentIndex === 0 || view.currentIndex >= 4
    readonly property bool depthView: view.currentIndex === 3
    readonly property bool useCloudClock: liveView || depthView
    readonly property var state: depth.snapshot
    readonly property bool editingAllowed: typeof customerAccess === "undefined" || customerAccess.editingAllowed
    property bool navigationOrbitMode: true
    property bool navigationPanMode: false
    property bool navigationRollMode: false
    property bool focusPickArmed: false
    property bool orbitPickArmed: false
    property bool cleanupSelectionArmed: false
    property string actionError: ""
    readonly property real displayedImageWidth: cloud.width
    readonly property real displayedImageHeight: cloud.height
    readonly property real navigationMoveStep: Math.max(0.01, Number(cloud.status.sceneFocusDistance || 1) * 0.05)
    function nudgeCamera(x, y) {
        if (navigationPanMode) cloud.pan(x * 0.06, y * 0.06)
        else cloud.moveCamera(x * navigationMoveStep, y * navigationMoveStep, 0)
        cloud.forceActiveFocus()
    }
    function dollyCamera(direction) { cloud.moveCamera(0, 0, direction * navigationMoveStep * 5); cloud.forceActiveFocus() }
    function armOrbitTarget() {
        orbitPickArmed = !orbitPickArmed; focusPickArmed = false
        navigationOrbitMode = true; navigationPanMode = false; navigationRollMode = false
        actionError = ""; cloud.forceActiveFocus()
    }
    function openProject(path) {
        const result = depth.openProject(path)
        if (result.ok) root.applyRecipe(result.recipe)
        return result
    }
    function saveView() { if (cloud.status.ready && !state.busy) depth.savePointCloudView(cloud.viewState) }
    onVisibleChanged: if (!visible) {
        cloud.playing = false; player.pause(); sourcePlayer.pause()
        primingSourcePreview = false; sourceNeedsInitialSeek = false; sourcePreviewDeadline.stop(); saveView()
    }
    Component.onDestruction: saveView()
    onStateChanged: {
        if (!state.busy && state.recordingLayersAvailable && cloud.status.error)
            cloud.refreshFrame()
        if (recordingPending && !state.busy) {
            recordingPending = false
            if (state.recordingLayersAvailable && !state.error) Qt.callLater(function() { recordingControls.startRecording() })
        }
        if (root.sourceIsVideo && sourceSeconds > 0 && trimOut > sourceSeconds && settingsChanged)
            root.setTrim(trimIn, sourceSeconds)
        const project = String((state || {}).project || "")
        if (project !== observedProject) {
            observedProject = project
            if (project.length) applyRecipe(state.recipe || {})
            view.currentIndex = 0
            if (project.length && !state.cloudAvailable)
                Qt.callLater(function() { if (!root.state.busy && !root.state.cloudAvailable) depth.preparePointCloud() })
        }
    }
    function renderColorKey(value): string {
        const a = value || {}
        const mode = Number(a.colorMode || 0)
        const key = [mode, Number(a.brightness ?? 1).toFixed(5), String(a.background || "#000000").toLowerCase()]
        if (mode === 2) key.push(String(a.gradientNear || "#0000ff").toLowerCase(),
            String(a.gradientMid || "#00ff00").toLowerCase(), String(a.gradientFar || "#ff0000").toLowerCase())
        return JSON.stringify(key)
    }
    readonly property bool renderColorsChanged: {
        const saved = (state.recipe || {}).settings || {}
        const usesPoints = Number(cloud.appearance.colorMode) !== 0 || appearance.currentIndex !== 0
        return Boolean(state.project) && usesPoints && (saved.style !== "points" ||
            renderColorKey(Object.assign({}, cloud.appearance, {colorMode: 2})) !== renderColorKey(saved.pointAppearance))
    }
    function restorePointCloudLook(recipe): void {
        const savedView = recipe.interactiveView || {}
        cloud.restoreView(savedView)
        const settings = recipe.settings || {}
        const look = savedView.appearance || (settings.style === "points" ? settings.pointAppearance : null)
        if (look) cloud.appearance = look
    }
    function renderSettings(): var {
        return {depth: depthStrength.value, motion: motionStrength.value / 100,
            convergence: convergence.value, path: ["slide", "arc", "push"][cameraPath.currentIndex],
            style: Number(cloud.appearance.colorMode) !== 0 || appearance.currentIndex !== 0 ? "points" : "photo",
            pointAppearance: Object.assign({}, cloud.appearance, {colorMode: 2}), duration: duration.value / 1000,
            start: startTime.value / 1000, edge: Number(resolution.currentText), fps: 24, invert: invert.checked,
            projectName: root.catalog && !["all", "unassigned"].includes(String(root.catalog.projectFilter))
                ? String(root.catalog.projectFilter) : String(((root.state.recipe || {}).settings || {}).projectName || "")}
    }
    function applyRecipe(recipe): void {
        root.sourcePath = String(recipe.source || "")
        root.depthPath = String(recipe.depthMap || "")
        const s = recipe.settings || {}
        depthStrength.value = Number(s.depth ?? 0.65)
        motionStrength.value = Number(s.motion ?? 0.025) * 100
        convergence.value = Number(s.convergence ?? 0.5)
        cameraPath.currentIndex = Math.max(0, ["slide", "arc", "push"].indexOf(String(s.path || "slide")))
        appearance.currentIndex = s.style === "points" ? 1 : 0
        duration.value = Math.round(Number(s.duration || 6) * 1000)
        startTime.value = Math.round(Number(s.start || 0) * 1000)
        resolution.currentIndex = Math.max(0, [640, 960, 1280, 1920].indexOf(Number(s.edge || 960)))
        invert.checked = Boolean(s.invert)
        root.restorePointCloudLook(recipe)
        root.settingsChanged = false
    }
    function name(path): string {
        const fileName = String(path).replace(/\\/g, "/").split("/").pop() || ""
        if (!/^file:/i.test(String(path))) return fileName
        try { return decodeURIComponent(fileName) } catch (error) { return fileName }
    }
    function exportResult(kind): void {
        root.exportKind = kind
        exportPicker.nameFilters = kind === "movie" ? ["MP4 video (*.mp4)"]
            : kind === "cloud" ? ["Colored point cloud (*.ply)"] : ["16-bit depth map (*.png)"]
        exportPicker.defaultSuffix = kind === "movie" ? "mp4" : kind === "cloud" ? "ply" : "png"
        exportPicker.open()
    }
    Theme { id: theme }
    SceneSaveDialog {
        id: saveSceneDialog
        kind: "depth-motion"
        projectPath: root.state.project ? root.state.project + "/project.depthmotion.json" : ""
        defaultName: root.name(root.sourcePath).replace(/\.[^.]+$/, "") + " · 3D Motion"
        sceneViewport: cloud
        beforeSave: function(name) { return {ok: depth.savePointCloudView(cloud.viewState), error: root.state.error} }
    }
    DepthMotionController {
        id: depth
        objectName: "depthMotionController"
        editingAllowed: root.editingAllowed
    }
    FileDialog {
        id: sourcePicker
        title: "Choose an image or video for depth motion"
        nameFilters: ["Images and videos (*.jpg *.jpeg *.png *.tif *.tiff *.webp *.bmp *.mp4 *.mov *.mkv *.m4v)"]
        onAccepted: {
            root.sourcePath = selectedFile.toString()
            root.depthPath = ""
            startTime.value = 0; duration.value = 6000
            root.settingsChanged = true
        }
    }
    FileDialog {
        id: depthPicker
        title: "Choose a depth map · white is near"
        nameFilters: ["Depth images (*.png *.tif *.tiff)"]
        onAccepted: { root.depthPath = selectedFile.toString(); root.settingsChanged = true }
    }
    FileDialog {
        id: projectPicker
        title: "Open a Depth Motion project"
        nameFilters: ["Depth Motion project (*.depthmotion.json)"]
        onAccepted: {
            const result = depth.openProject(selectedFile.toString())
            if (result.ok) root.applyRecipe(result.recipe)
        }
    }
    FileDialog {
        id: exportPicker
        title: "Export depth motion"
        fileMode: FileDialog.SaveFile
        onAccepted: depth.exportFile(root.exportKind, selectedFile.toString())
    }
    ColorDialog {
        id: gradientColor
        property string key: "gradientNear"
        title: "Depth gradient color"
        onAccepted: {
            const patch = {gradientPreset: "Custom"}
            patch[key] = selectedColor.toString()
            cloud.appearance = patch
        }
    }
    AppDialog {
        id: setupDialog
        title: "Set up local depth tools"
        width: Math.min(500, popupAvailableWidth)
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: depth.install()
        contentItem: Label {
            text: "Download the depth model and a private Python runtime (several hundred MB). Processing then happens on this Mac. FFmpeg and Python 3.10 or later are required.\n\nThe first download needs an internet connection. Your images and videos stay local."
            wrapMode: Text.WordWrap
            color: theme.textMain
        }
    }
    Timer {
        id: saveViewTimer
        interval: 700
        onTriggered: root.saveView()
    }
    MediaPlayer {
        id: player
        objectName: "depthMotionPlayer"
        source: root.state.movieUrl || ""
        videoOutput: movie
        loops: MediaPlayer.Infinite
    }
    MediaPlayer {
        id: sourcePlayer
        objectName: "depthMotionSourcePlayer"
        source: root.sourceIsVideo ? depth.localMediaUrl(root.sourcePath) : ""
        videoOutput: sourceMovie
        onMediaStatusChanged: root.seekSourceTrim()
        onSeekableChanged: if (sourcePlayer.seekable) root.seekSourceTrim()
        onDurationChanged: function(duration) {
            if (duration > 0 && root.trimOut > duration / 1000 && root.settingsChanged)
                root.setTrim(root.trimIn, duration / 1000)
        }
        onPositionChanged: if (root.sourcePreviewActive && !root.primingSourcePreview && sourcePlayer.playbackState === MediaPlayer.PlayingState && sourcePlayer.position >= root.trimOut * 1000) {
            pause(); setPosition(root.trimIn * 1000)
        }
    }
    Timer {
        id: sourcePreviewDeadline
        interval: 5000
        onTriggered: {
            root.primingSourcePreview = false; root.sourceNeedsInitialSeek = false
            sourcePlayer.pause()
        }
    }
    Connections {
        target: sourceMovie.videoSink
        function onVideoFrameChanged() {
            if (!root.primingSourcePreview) return
            if (!root.sourceSeekIssued) { root.seekSourceTrim(); return }
            if (Math.abs(sourcePlayer.position - root.sourceSeekSeconds * 1000) > 60) return
            root.primingSourcePreview = false; root.sourceNeedsInitialSeek = false
            sourcePreviewDeadline.stop(); sourcePlayer.pause()
        }
    }
    Rectangle { anchors.fill: parent; color: theme.ink950 }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.scaled(48)
            color: theme.ink900
            RowLayout {
                anchors.fill: parent
                anchors.margins: theme.space8
                spacing: theme.space8
                ActionButton {
                    visible: root.sceneHosted
                    compact: true; text: "Scene"; iconName: "arrow-left"; showLeadingIcon: true; showText: !root.compact
                    onClicked: root.exitRequested()
                }
                Label { text: "3D Motion"; color: theme.textStrong; font.weight: Font.DemiBold }
                Label {
                    Layout.fillWidth: true
                    text: root.name(root.sourcePath)
                    elide: Text.ElideMiddle
                    color: theme.textMuted
                }
                ActionButton {
                    objectName: "depthMotionLibraryButton"
                    compact: true; text: "Library"
                    visible: !root.compact
                    onClicked: { root.saveView(); root.libraryScenesRequested() }
                }
                ActionButton {
                    objectName: "depthMotionSaveSceneButton"
                    compact: true; text: "Save"; iconName: "save"; showLeadingIcon: true; showText: !root.compact
                    enabled: root.editingAllowed && Boolean(cloud.status.frameAvailable) && !root.state.busy
                    onClicked: saveSceneDialog.open()
                }
                ActionButton {
                    objectName: "depthMotionChooseSource"
                    compact: true; text: root.sourcePath.length ? "Source…" : "Choose source…"
                    enabled: !root.state.busy && !recordingControls.recorder.busy
                    onClicked: sourcePicker.open()
                }
                ActionButton {
                    compact: true; text: "Open…"; iconName: "folder"; showLeadingIcon: true; showText: !root.compact; enabled: !root.state.busy
                    onClicked: projectPicker.open()
                }
                ActionButton {
                    objectName: "depthMotionInspectorToggle"
                    compact: true; text: "Settings"; iconName: "adjust"; showLeadingIcon: true; showText: !root.compact
                    onClicked: root.compact ? settingsDrawer.open() : root.inspectorVisible = !root.inspectorVisible
                }
            }
        }
        Rectangle {
            objectName: "depthMotionTrimPanel"
            visible: root.sourceIsVideo
            Layout.fillWidth: true
            Layout.preferredHeight: theme.scaled(100)
            color: theme.ink900
            enabled: !root.state.busy && !recordingControls.recorder.busy
            ColumnLayout {
                anchors.fill: parent; anchors.margins: theme.space8; spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Trim before depth"; color: theme.textStrong; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Label { text: (duration.value / 1000).toFixed(2) + " s selected · up to 12 s"; color: theme.textMuted; font.pixelSize: theme.typeCaption }
                }
                RangeSlider {
                    id: trimRange
                    objectName: "depthMotionTrimRange"
                    Layout.fillWidth: true
                    from: 0; to: Math.max(1 / 24, root.sourceSeconds || root.trimOut)
                    Component.onCompleted: root.syncTrimRange()
                    first.onMoved: root.setTrim(first.value, second.value)
                    second.onMoved: root.setTrim(first.value, second.value)
                    Accessible.name: "Source clip trim range"
                    background: Rectangle {
                        x: parent.leftPadding; y: parent.topPadding + (parent.availableHeight - height) / 2
                        width: parent.availableWidth; height: 4; radius: 2; color: theme.lineSoft
                        Rectangle {
                            x: parent.width * parent.parent.first.visualPosition
                            width: parent.width * (parent.parent.second.visualPosition - parent.parent.first.visualPosition)
                            height: parent.height; color: theme.textStrong; radius: 2
                        }
                    }
                    first.handle: Rectangle {
                        x: parent.leftPadding + parent.first.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + (parent.availableHeight - height) / 2
                        implicitWidth: 20; implicitHeight: 20; radius: 10
                        color: theme.textStrong; border.color: theme.ink950; border.width: 2
                    }
                    second.handle: Rectangle {
                        x: parent.leftPadding + parent.second.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + (parent.availableHeight - height) / 2
                        implicitWidth: 20; implicitHeight: 20; radius: 10
                        color: theme.textStrong; border.color: theme.ink950; border.width: 2
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "In"; color: theme.textMuted }
                    ToneSpinBox {
                        id: startTime; objectName: "depthMotionTrimIn"; Layout.fillWidth: true
                        Layout.maximumWidth: root.compact ? 10000 : theme.scaled(140)
                        from: 0; to: 86400000; value: 0; stepSize: 42; editable: true
                        textFromValue: function(value, locale) { return (value / 1000).toFixed(2) }
                        valueFromText: function(text, locale) { return Math.round(Number(text) * 1000) }
                        validator: DoubleValidator { bottom: 0; top: 86400; decimals: 3; notation: DoubleValidator.StandardNotation }
                        Accessible.name: "Trim in seconds"
                        onValueModified: root.setTrim(value / 1000, value / 1000 + duration.value / 1000)
                    }
                    ActionButton { compact: true; text: "Set In"; onClicked: root.setTrim(sourcePlayer.position / 1000, root.trimOut) }
                    Label { text: "Out"; color: theme.textMuted }
                    ToneSpinBox {
                        objectName: "depthMotionTrimOut"; Layout.fillWidth: true
                        Layout.maximumWidth: root.compact ? 10000 : theme.scaled(140)
                        from: 42; to: Math.max(42, root.sourceSeconds * 1000); value: Math.min(to, Math.round(root.trimOut * 1000)); stepSize: 42; editable: true
                        textFromValue: function(value, locale) { return (value / 1000).toFixed(2) }
                        valueFromText: function(text, locale) { return Math.round(Number(text) * 1000) }
                        validator: DoubleValidator { bottom: 0; top: 86400; decimals: 3; notation: DoubleValidator.StandardNotation }
                        Accessible.name: "Trim out seconds"
                        onValueModified: root.setTrim(root.trimIn, value / 1000)
                    }
                    ActionButton { compact: true; text: "Set Out"; onClicked: root.setTrim(root.trimIn, sourcePlayer.position / 1000) }
                    Item { Layout.fillWidth: true; visible: !root.compact }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                Item {
                    id: stage
                    objectName: "depthMotionSceneStage"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    DepthPointCloudViewport {
                        id: cloud
                        objectName: "depthPointCloudViewport"
                        anchors.fill: parent
                        visible: root.useCloudClock && !root.sourcePreviewActive
                        depthPreview: root.depthView && Boolean(root.state.depthVideoAvailable)
                        colorPass: view.currentIndex === 4 ? "source" : view.currentIndex === 5 ? "depth" : "appearance"
                        project: root.state.cloudAvailable ? String(root.state.project || "") : ""
                        activeFocusOnTab: true
                        onProjectChanged: root.restorePointCloudLook(root.state.recipe || {})
                        onViewChanged: if (root.visible && status.ready) saveViewTimer.restart()
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Space) { playing = !playing; event.accepted = true }
                            else SceneNavigation.handleKey(event, cloud, root)
                        }
                    }
                    SceneCameraInput {
                        objectName: "depthPointCloudGestures"
                        anchors.fill: parent
                        visible: root.liveView && !root.sourcePreviewActive
                        viewport: cloud
                        navigation: root
                    }
                    SceneCameraControls {
                        visible: root.liveView && Boolean(cloud.status.frameAvailable)
                        viewport: cloud
                        navigation: root
                    }
                    VideoOutput {
                        id: sourceMovie
                        anchors.fill: parent
                        visible: root.sourcePreviewActive
                        fillMode: VideoOutput.PreserveAspectFit
                    }
                    VideoOutput {
                        id: movie
                        anchors.fill: parent
                        visible: view.currentIndex === 1 && player.playbackState !== MediaPlayer.StoppedState
                        fillMode: VideoOutput.PreserveAspectFit
                    }
                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true; cache: false
                        visible: !root.sourcePreviewActive && (view.currentIndex === 2 || (view.currentIndex === 1 && player.playbackState === MediaPlayer.StoppedState))
                        source: view.currentIndex === 2 ? String(root.state.sourceUrl || "")
                            : view.currentIndex === 3 ? String(root.state.depthUrl || "") : String(root.state.previewUrl || "")
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: theme.space12
                        visible: !root.state.project && !root.sourcePath.length
                        Label { text: "Video, in three dimensions"; color: theme.textStrong; font.pixelSize: theme.typeSection }
                        Label { text: "Choose a source to build its point cloud."; color: theme.textMuted }
                    }
                    Label {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: theme.space12
                        visible: root.orbitPickArmed || root.actionError.length > 0
                        text: root.orbitPickArmed ? "Click a point to set the orbit target" : root.actionError
                        color: theme.textStrong
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: theme.scaled(44)
                    color: theme.ink900
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: theme.space8; anchors.rightMargin: theme.space8
                        spacing: theme.space8
                        ActionButton {
                            objectName: "depthMotionPlay"
                            compact: true
                            text: (root.sourcePreviewActive ? sourcePlayer.playbackState === MediaPlayer.PlayingState : root.useCloudClock ? cloud.playing : player.playbackState === MediaPlayer.PlayingState) ? "Pause" : "Play"
                            enabled: (root.sourcePreviewActive ? root.sourceSeconds > 0 : Boolean(root.state.project) && (root.useCloudClock ? Boolean(cloud.status.ready) : view.currentIndex === 1)) && !root.state.busy
                            onClicked: {
                                if (root.sourcePreviewActive) {
                                    root.primingSourcePreview = false; root.sourceNeedsInitialSeek = false
                                    if (sourcePlayer.playbackState === MediaPlayer.PlayingState) sourcePlayer.pause()
                                    else { if (sourcePlayer.position < root.trimIn * 1000 || sourcePlayer.position >= root.trimOut * 1000) sourcePlayer.setPosition(root.trimIn * 1000); sourcePlayer.play() }
                                }
                                else if (root.useCloudClock) { cloud.playing = !cloud.playing; cloud.forceActiveFocus() }
                                else player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                            }
                        }
                        SceneRecordingControls {
                            id: recordingControls
                            viewport: cloud
                            catalog: root.catalog
                            projectName: String(((root.state.recipe || {}).settings || {}).projectName || "")
                            prepareRecording: function() { root.startRecording() }
                            layered: true
                            projectPath: String(root.state.project || "")
                            ready: Boolean(cloud.status.frameAvailable) && !root.state.busy && root.useCloudClock && !root.settingsChanged
                            iconOnly: root.compact
                        }
                        ToolValueSlider {
                            id: scrubber
                            objectName: "depthMotionScrubber"
                            Layout.fillWidth: true
                            minimumReadableWidth: 60
                            label: "Depth motion playhead"
                            from: 0; to: root.sourcePreviewActive ? Math.max(1, sourcePlayer.duration) : root.useCloudClock ? Math.max(1, cloud.duration * 1000) : Math.max(1, player.duration)
                            value: root.sourcePreviewActive ? sourcePlayer.position : root.useCloudClock ? cloud.position * 1000 : player.position
                            enabled: root.sourcePreviewActive || Boolean(root.state.project) && (root.useCloudClock || view.currentIndex === 1)
                            onMoved: {
                                if (root.sourcePreviewActive) root.previewSource(value / 1000)
                                else if (root.useCloudClock) { cloud.playing = false; cloud.position = value / 1000 }
                                else { player.pause(); player.setPosition(value) }
                            }
                        }
                        Label {
                            text: (root.sourcePreviewActive ? sourcePlayer.position / 1000 : root.useCloudClock ? cloud.position : player.position / 1000).toFixed(1) + " s"
                            color: theme.textMuted
                            Layout.preferredWidth: theme.scaled(40)
                            horizontalAlignment: Text.AlignRight
                        }
                        ToneComboBox {
                            id: view
                            objectName: "depthMotionPreviewMode"
                            Layout.preferredWidth: root.compact ? theme.scaled(105) : theme.scaled(130)
                            model: ["Point cloud", "Rendered clip", "Source", "Depth video", "Source-color points", "Depth points"]
                            displayText: root.sourcePreviewActive ? "Source preview" : currentText
                            enabled: Boolean(root.state.project)
                            onActivated: {
                                player.pause()
                                if (!root.useCloudClock) cloud.playing = false
                                if ((root.depthView || view.currentIndex >= 4) && !root.state.recordingLayersAvailable && !root.state.busy) depth.prepareDepthVideo()
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: inspectorDock
                objectName: "depthMotionInspectorDock"
                visible: !root.compact && root.inspectorVisible
                Layout.preferredWidth: theme.scaled(280)
                Layout.fillHeight: true
                color: theme.ink900
            }
        }
    }
    AppDrawer {
        id: settingsDrawer
        objectName: "depthMotionSettingsDrawer"
        edge: Qt.RightEdge
        width: Math.min(theme.scaled(300), root.width * 0.9)
        height: root.height
        parent: root
    }
    Item {
        parent: root.compact ? settingsDrawer.contentItem : inspectorDock
        anchors.fill: parent
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: theme.space12
            spacing: theme.space8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Point cloud"; color: theme.textStrong; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Label { text: root.state.recipe && root.state.recipe.width ? root.state.recipe.width + " × " + root.state.recipe.height : ""; color: theme.textMuted; font.pixelSize: theme.typeCaption }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: theme.space4
                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Appearance"; uppercaseTitle: false; expandable: true; expanded: true
                        visible: root.liveView
                        CompactSceneSlider {
                            objectName: "depthPointCloudExaggeration"
                            Layout.fillWidth: true
                            defaultValue: 1
                            label: "Z depth"; from: 0; to: 8; stepSize: 0.1; decimals: 1; suffix: "×"
                            value: cloud.zExaggeration
                            onValueEdited: value => cloud.zExaggeration = value
                        }
                        CompactSceneSlider {
                            objectName: "depthPointCloudSize"
                            Layout.fillWidth: true
                            defaultValue: 1
                            label: "Size"; from: 1; to: 8; stepSize: 1; decimals: 0; suffix: " px"
                            value: cloud.pointSize
                            onValueEdited: value => cloud.pointSize = Math.round(value)
                        }
                        CompactSceneSlider {
                            objectName: "depthPointCloudDensity"
                            Layout.fillWidth: true
                            defaultValue: 100
                            label: "Density"; from: 5; to: 100; stepSize: 1; decimals: 0; suffix: "%"
                            value: Math.round(Number(cloud.appearance.density) * 100)
                            onValueEdited: value => cloud.appearance = {density: value / 100}
                        }
                        ToneComboBox {
                            objectName: "depthPointCloudShape"; Layout.fillWidth: true
                            model: ["Round points", "Square pixels", "Soft dots"]
                            currentIndex: Number(cloud.appearance.shape || 0)
                            Accessible.name: "Point shape"
                            onActivated: cloud.appearance = {shape: currentIndex}
                        }
                        ToneComboBox {
                            objectName: "depthPointCloudColor"; Layout.fillWidth: true
                            model: ["Source color", "Monochrome", "Depth gradient"]
                            currentIndex: Number(cloud.appearance.colorMode || 0)
                            Accessible.name: "Point color"
                            onActivated: cloud.appearance = {colorMode: currentIndex}
                        }
                        Label {
                            text: "Recording gradient"; color: theme.textMuted; font.pixelSize: theme.typeCaption
                        }
                        ToneComboBox {
                            objectName: "depthPointCloudGradientPreset"
                            Layout.fillWidth: true
                            Accessible.name: "Recording gradient palette"
                            readonly property var palettes: [
                                {name: "Spectrum", colors: ["#0000ff", "#00ff00", "#ff0000"]},
                                {name: "Glacier", colors: ["#173b73", "#2cc7ce", "#e7ffff"]},
                                {name: "Ember", colors: ["#30104b", "#ed4d5a", "#ffd889"]},
                                {name: "Aurora", colors: ["#5a1faa", "#f186c0", "#c5f5b5"]},
                                {name: "Ocean", colors: ["#031a40", "#2576b7", "#74f5d2"]},
                                {name: "Silver", colors: ["#101318", "#7f8997", "#f4f4ef"]},
                                {name: "Custom", colors: []}
                            ]
                            model: palettes.map(p => p.name)
                            currentIndex: Math.max(0, palettes.findIndex(p => p.name === String(cloud.appearance.gradientPreset || "Spectrum")))
                            onActivated: {
                                const palette = palettes[currentIndex]
                                if (palette.colors.length) cloud.appearance = {gradientNear: palette.colors[0], gradientMid: palette.colors[1], gradientFar: palette.colors[2], gradientPreset: palette.name}
                                else cloud.appearance = {gradientPreset: palette.name}
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: Number(cloud.appearance.colorMode) === 2 || String(cloud.appearance.gradientPreset) === "Custom"
                            Repeater {
                                model: [{key: "gradientNear", name: "Near"}, {key: "gradientMid", name: "Mid"}, {key: "gradientFar", name: "Far"}]
                                delegate: ActionButton {
                                    required property var modelData
                                    objectName: "depthGradientColor_" + modelData.key
                                    Layout.fillWidth: true; compact: true
                                    text: modelData.name
                                    accent: String(cloud.appearance[modelData.key])
                                    tooltipText: "Edit " + modelData.name.toLowerCase() + " depth color"
                                    onClicked: {
                                        gradientColor.key = modelData.key
                                        gradientColor.selectedColor = String(cloud.appearance[modelData.key])
                                        gradientColor.open()
                                    }
                                    Rectangle { anchors.left: parent.left; anchors.leftMargin: 3; anchors.verticalCenter: parent.verticalCenter; width: 5; height: 18; radius: 2; color: String(cloud.appearance[modelData.key]) }
                                }
                            }
                        }
                        CompactSceneSlider {
                            objectName: "depthPointCloudBrightness"; Layout.fillWidth: true
                            defaultValue: 100
                            label: "Brightness"; from: 10; to: 300; stepSize: 1; decimals: 0; suffix: "%"
                            value: Math.round(Number(cloud.appearance.brightness) * 100)
                            onValueEdited: value => cloud.appearance = {brightness: value / 100}
                        }
                        ToneComboBox {
                            objectName: "depthPointCloudBackground"; Layout.fillWidth: true
                            model: ["Black background", "Charcoal background", "White background"]
                            currentIndex: ["#000000", "#171a20", "#ffffff"].indexOf(String(cloud.appearance.background))
                            Accessible.name: "Point-cloud background"
                            onActivated: cloud.appearance = {background: ["#000000", "#171a20", "#ffffff"][currentIndex]}
                        }
                    }
                    InspectorSection {
                        id: buildSection
                        Layout.fillWidth: true
                        title: "Build settings"; uppercaseTitle: false; expandable: true; expanded: !root.state.project
                        enabled: !root.state.busy
                        CompactSceneSlider {
                            id: depthStrength; Layout.fillWidth: true; objectName: "depthMotionDepth"
                            defaultValue: 0.65
                            label: "Depth"; from: 0; to: 1.5; stepSize: 0.01; decimals: 2; value: 0.65
                            onValueEdited: value => { depthStrength.value = value; root.settingsChanged = true }
                        }
                        CompactSceneSlider {
                            id: convergence; Layout.fillWidth: true
                            defaultValue: 0.5
                            label: "Converge"; from: 0; to: 1; stepSize: 0.01; decimals: 2; value: 0.5
                            onValueEdited: value => { convergence.value = value; root.settingsChanged = true }
                        }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2
                            visible: !root.sourceIsVideo
                            Label { text: "Seconds"; color: theme.textMain; font.pixelSize: theme.typeCaption }
                            ToneSpinBox {
                                id: duration; objectName: "depthMotionDuration"; from: 42; to: 12000; value: 6000; stepSize: 250; editable: true
                                textFromValue: function(value, locale) { return (value / 1000).toFixed(2) }
                                valueFromText: function(text, locale) { return Math.round(Number(text) * 1000) }
                                validator: DoubleValidator { bottom: 0.042; top: 12; decimals: 3; notation: DoubleValidator.StandardNotation }
                                onValueModified: root.settingsChanged = true
                            }
                        }
                        ToneComboBox { id: resolution; Layout.fillWidth: true; model: ["640", "960", "1280", "1920"]; currentIndex: 1; Accessible.name: "Depth source maximum edge"; onActivated: root.settingsChanged = true }
                        ToneComboBox { id: cameraPath; Layout.fillWidth: true; visible: !root.liveView; model: ["Gentle slide", "Soft arc", "Slow push"]; onActivated: root.settingsChanged = true }
                        CompactSceneSlider {
                            id: motionStrength; Layout.fillWidth: true; visible: !root.liveView
                            defaultValue: 2.5
                            label: "Travel"; from: 0; to: 8; stepSize: 0.1; value: 2.5
                            onValueEdited: value => { motionStrength.value = value; root.settingsChanged = true }
                        }
                        ToneComboBox { id: appearance; Layout.fillWidth: true; visible: !root.liveView; model: ["Photographic surface", "Colored point cloud"]; currentIndex: 1; onActivated: root.settingsChanged = true }
                        ToneCheckBox { id: invert; text: "Invert depth"; onToggled: root.settingsChanged = true }
                        ActionButton { compact: true; text: root.depthPath.length ? "Change depth map…" : "Use depth map…"; onClicked: depthPicker.open() }
                        ActionButton { compact: true; visible: root.depthPath.length > 0; text: "Automatic depth"; onClicked: { root.depthPath = ""; root.settingsChanged = true } }
                        ActionButton { objectName: "depthMotionSetup"; compact: true; text: root.state.modelReady ? "Depth tools installed" : "Install depth tools…"; enabled: !(root.state.modelReady && root.state.runtimeReady); onClicked: setupDialog.open() }
                    }
                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Export"; uppercaseTitle: false; expandable: true; expanded: false
                        ActionButton { objectName: "depthMotionExportMovie"; Layout.fillWidth: true; compact: true; text: "Rendered clip…"; enabled: root.editingAllowed && Boolean(root.state.project) && !root.state.busy && !root.settingsChanged && !root.renderColorsChanged; onClicked: root.exportResult("movie") }
                        ActionButton { objectName: "depthMotionExportCloud"; Layout.fillWidth: true; compact: true; text: "First-frame cloud…"; enabled: root.editingAllowed && Boolean(root.state.project) && !root.state.busy && !root.settingsChanged; onClicked: root.exportResult("cloud") }
                        ActionButton { Layout.fillWidth: true; compact: true; text: "Depth image…"; enabled: root.editingAllowed && Boolean(root.state.project) && !root.state.busy && !root.settingsChanged; onClicked: root.exportResult("depth") }
                        ActionButton { Layout.fillWidth: true; compact: true; text: "Show project"; enabled: Boolean(root.state.project); onClicked: Qt.openUrlExternally("file://" + String(root.state.project)) }
                    }
                }
            }
            ProgressStrip { Layout.fillWidth: true; visible: Boolean(root.state.busy); progress: Number(root.state.progress || 0) }
            Label {
                objectName: "depthMotionStatusMessage"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.state.error || (root.state.busy ? root.state.status
                    : String((root.state.sourceInfo || {}).error || "") || (root.sourcePreviewActive ? sourcePlayer.errorString : root.liveView ? cloud.status.error : player.errorString))
                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                color: theme.accentWarm400; font.pixelSize: theme.typeCaption
            }
            Label {
                objectName: "depthMotionRenderColorsNotice"
                Layout.fillWidth: true
                visible: root.renderColorsChanged && !root.state.busy
                text: "Update the rendered clip to apply these point-cloud colors."
                color: theme.textMuted; font.pixelSize: theme.typeCaption; wrapMode: Text.WordWrap
            }
            ActionButton {
                objectName: "depthMotionRender"
                Layout.fillWidth: true
                compact: true; tone: "primary"
                text: root.renderColorsChanged ? "Update rendered clip" : root.state.project ? "Rebuild 3D motion" : "Build 3D motion"
                enabled: root.editingAllowed && !root.state.busy && !recordingControls.recorder.busy && root.sourcePath.length > 0 && root.state.runtimeReady && (root.depthPath.length > 0 || root.state.modelReady)
                onClicked: { player.stop(); root.primingSourcePreview = false; root.sourceNeedsInitialSeek = false; sourcePlayer.pause(); cloud.playing = false; root.saveView(); depth.generate(root.sourcePath, root.depthPath, root.renderSettings()) }
            }
            ActionButton { Layout.fillWidth: true; compact: true; text: "Stop build"; visible: Boolean(root.state.busy); onClicked: depth.cancel() }
        }
    }
}
