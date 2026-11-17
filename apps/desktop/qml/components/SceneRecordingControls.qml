pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PhotoEditor

RowLayout {
    id: root
    objectName: "sceneRecordingControls"
    required property var viewport
    required property string projectPath
    property bool ready: false
    property var catalog: null
    property var prepareRecording: null
    property bool layered: false
    property string targetProject: ""
    property string projectName: ""
    property var pendingFiles: []
    property bool importing: false
    property bool importStarted: false
    property string importError: ""
    property string processedOutput: ""
    property real importStartedAt: 0
    function startRecording(): void {
        targetProject = root.projectName.length ? root.projectName
            : catalog && catalog.projectFilter && !["all", "unassigned"].includes(String(catalog.projectFilter))
                ? String(catalog.projectFilter) : "Scene recordings"
        importError = ""
        videoRecorder.start(root.viewport, root.projectPath)
    }
    function checkImport(): void {
        if (!catalog || !importing) return
        if (Date.now() - importStartedAt > 180000) { importError = "Videos saved. Adding them to the project timed out; retry below."; importing = false; return }
        if (catalog.busy) return
        if (!importStarted) {
            importStarted = true
            catalog.importSelectionWithOptions(catalog.localPathForUrl(catalog.parentFolderUrl(pendingFiles[0])), pendingFiles, targetProject, "referenced")
            return
        }
        if (pendingFiles.every(path => String(catalog.assetIdForFilePath(path) || "").length > 0)) {
            importing = false; pendingFiles = []
        } else if (String(catalog.errorMessage || "").length) {
            importError = "Videos saved, but adding them to the project failed: " + catalog.errorMessage; importing = false
        }
    }
    Connections {
        target: videoRecorder
        function onChanged() {
            const key = videoRecorder.outputUrl.toString()
            if (videoRecorder.busy || !key.length || key === root.processedOutput || !root.catalog) return
            root.processedOutput = key; root.pendingFiles = videoRecorder.outputFiles
            root.importing = true; root.importStarted = false; root.importStartedAt = Date.now()
            root.checkImport()
        }
    }
    Timer { interval: 150; repeat: true; running: root.importing; onTriggered: root.checkImport() }
    property bool iconOnly: false
    property alias recorder: videoRecorder
    spacing: 6
    onProjectPathChanged: videoRecorder.stop()
    onVisibleChanged: if (!visible) videoRecorder.stop()
    Theme { id: theme }
    SceneVideoRecorder { id: videoRecorder; objectName: "sceneVideoRecorder" }
    ActionButton {
        objectName: "sceneCameraRecordButton"
        compact: true; text: videoRecorder.recording ? "Stop" : videoRecorder.busy ? "Saving…" : "Record"
        iconName: videoRecorder.recording ? "stop" : "record"
        showText: !root.iconOnly || videoRecorder.busy
        showLeadingIcon: true
        accent: videoRecorder.recording ? theme.accentWarm400 : theme.textStrong
        enabled: videoRecorder.recording || (!videoRecorder.busy && !root.importing && root.ready)
        accessibleName: videoRecorder.recording ? "Stop camera recording" : "Record scene camera"
        tooltipText: root.layered ? "Record four videos: the selected gradient palette, source-color points, flat depth map, and source video. The gradient is included in every recording."
            : "Record this scene view and your camera moves. Saves a silent MP4 to the project's recordings folder."
        onClicked: {
            if (videoRecorder.recording) videoRecorder.stop()
            else if (typeof root.prepareRecording === "function") root.prepareRecording()
            else root.startRecording()
        }
    }
    Label {
        visible: videoRecorder.busy && !root.iconOnly
        text: Math.floor(videoRecorder.seconds / 60) + ":" + String(Math.floor(videoRecorder.seconds % 60)).padStart(2, "0")
        color: theme.textMuted; font.pixelSize: theme.typeCaption
    }
    ActionButton {
        objectName: "sceneCameraRecordingResult"
        visible: !videoRecorder.busy && (videoRecorder.outputUrl.toString().length > 0 || videoRecorder.error.length > 0)
        compact: true; text: videoRecorder.error.length ? "Recording failed" : root.importing ? "Adding to project…" : root.importError.length ? "Retry adding videos" : videoRecorder.outputFiles.length > 1 ? videoRecorder.outputFiles.length + " videos saved" : "Video saved"
        showText: !root.iconOnly; iconName: videoRecorder.error.length ? "warning" : "video"
        tooltipText: videoRecorder.error.length ? videoRecorder.error : root.importError || (root.catalog ? "Saved in project: " + root.targetProject + "\n" : "") + videoRecorder.outputFiles.join("\n")
        onClicked: {
            if (root.importError.length) { root.importError = ""; root.importing = true; root.importStarted = false; root.importStartedAt = Date.now(); root.checkImport() }
            else if (!videoRecorder.error.length) Qt.openUrlExternally(videoRecorder.outputUrl)
        }
    }
}
