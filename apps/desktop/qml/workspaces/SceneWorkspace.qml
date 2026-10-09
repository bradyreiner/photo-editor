pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import PhotoEditor 1.0
import "../components"

Item {
    id: root
    objectName: "sceneWorkspace"
    property var controller: null
    property var catalog: null
    property alias photoViewport: viewport
    signal photoReadyForEditing(string assetId, bool openEditor)
    property bool editPhotoAfterRender: false
    readonly property string photoEditPhase: photoEdit.phase
    function renderPhotoForEditing(destination: string): var {
        root.actionError = ""
        return photoEdit.start(destination, Number(exportEdge.currentText))
    }
    ScenePhotoEditHandoff {
        id: photoEdit
        viewport: root.photoViewport
        catalog: root.catalog
        sceneIdentity: String(root.scene.projectPath || "") + "|" + String(root.scene.artifactPath || "")
            + "|" + String(root.scene.artifactValidation ? root.scene.artifactValidation.artifactSha256 || "" : "")
        onPhotoReady: (assetId, openEditor) => root.photoReadyForEditing(assetId, openEditor)
    }
    readonly property var scene: controller ? controller.snapshot : ({})
    property string actionError: ""
    property bool showDetails: false
    property bool showCaptureSetup: false
    property alias immersive: sceneWorkspaceSettings.immersive
    property alias previewStatusVisible: sceneWorkspaceSettings.previewStatusVisible
    Settings {
        id: sceneWorkspaceSettings
        objectName: "sceneWorkspaceSettings"
        category: "SceneWorkspace"
        property bool immersive: true
        property bool previewStatusVisible: true
    }
    function handleLayoutMenuAction(key: string): void {
        if (key === "navigation") immersive = !immersive
        else if (key === "status") previewStatusVisible = !previewStatusVisible
        else if (key === "photo-settings") photoControls.visible ? photoControls.close() : photoControls.open()
    }
    property bool focusPickArmed: false
    Keys.onEscapePressed: event => { event.accepted = focusPickArmed; if (focusPickArmed) { focusPickArmed = false; actionError = "" } }
    onVisibleChanged: { if (!visible) { focusPickArmed = false; if (photoControls) photoControls.close() } }
    onRenderableChanged: {
        if (!renderable && photoControls) photoControls.close()
        if (renderable && sceneControls) { sceneControls.close(); showCaptureSetup = false }
    }
    signal exitViewerRequested()
    readonly property bool renderable: scene.state === "trained" || scene.state === "artifact-ready"
    readonly property bool spacetimeProject: scene.mode === "spacetime-4d"
    readonly property bool captureReady: renderable || scene.state === "prepared-not-trained"
    readonly property bool timedProject: scene.mode === "timed-4d-input"
    readonly property bool timedInput: captureMode.currentIndex === 1
    readonly property bool videoInput: captureMode.currentIndex === 2
    property bool advancedCapture: false
    readonly property string stageLabel: ({"idle": "Create a 3D scene", "extracting-video": "Preparing your video",
        "preparing": "Preparing photographs", "extracting-features": "Finding image details", "matching-views": "Connecting viewpoints",
        "reconstructing-cameras": "Reconstructing the camera path", "undistorting": "Preparing the aligned scene",
        "training-gaussians": "Building the scene", "validating-artifact": "Checking the finished scene",
        "canceling": "Stopping safely…", "canceled": "Scene creation stopped", "failed": "Scene needs attention"})[String(scene.state || "idle")] || String(scene.state || "")
    function prepareVideoInput(path: string): void {
        captureMode.currentIndex = 2
        capturePath.text = path
        projectPath.text = root.controller ? root.controller.suggestedProjectPath(path) : ""
        captureQuality.currentIndex = 0
        iterations.value = 5000; resolution.currentIndex = 1; splatBudget.currentIndex = 0
        root.advancedCapture = false; root.showCaptureSetup = true; root.actionError = ""
        sceneControls.open()
    }
    DropArea {
        anchors.fill: parent
        enabled: !root.scene.busy
        onDropped: drop => {
            if (drop.urls.length === 1 && /\.(mov|mp4|m4v|mkv)$/i.test(String(drop.urls[0]))) {
                root.prepareVideoInput(String(drop.urls[0])); drop.acceptProposedAction()
            }
        }
    }
    property var pathDraft: []
    readonly property var photoSettings: viewport.status.photoSettings || ({focalScale: 1, exposureEv: 0, look: "neutral"})
    readonly property var lensSettings: viewport.status.lensSettings || ({focusDistance: 1, apertureRadius: 0, blades: 0, rotationDegrees: 0, roundness: 0, samples: 32})
    readonly property var filmSettings: root.photoSettings.film || ({grainAmount: 0, grainSize: 1.5, halationAmount: 0, halationRadius: 4, halationThreshold: 0.8, seed: 1})
    readonly property var physicalCamera: viewport.status.physicalCamera || ({})
    readonly property bool physicalCameraActive: Boolean(root.physicalCamera.schema)
    function updatePhysicalSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.physicalCamera)
        settings[key] = value
        root.actionError = String(viewport.setPhysicalCamera(settings).error || "")
    }
    function updatePhotoSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.photoSettings)
        settings[key] = value
        const result = viewport.setPhotoSettings(settings)
        root.actionError = String(result.error || "")
    }
    function updateLensSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = settings.film ? "framehouse-gaussian-photo-settings-v3" : "framehouse-gaussian-photo-settings-v2"
        settings.lens = Object.assign({}, root.lensSettings)
        settings.lens[key] = value
        settings.lens.schema = Number(settings.lens.samples) > 128 ? "framehouse-thin-lens-v2" : "framehouse-thin-lens-v1"
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function updateFilmSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = "framehouse-gaussian-photo-settings-v3"
        settings.lens = Object.assign({}, root.lensSettings)
        settings.lens.schema = Number(settings.lens.samples) > 128 ? "framehouse-thin-lens-v2" : "framehouse-thin-lens-v1"
        settings.film = Object.assign({}, root.filmSettings)
        settings.film.schema = "framehouse-creative-film-v1"
        settings.film[key] = value
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function exportTimedFilmTo(destination: string): var {
        if (!filmSceneStart.acceptableInput || !filmSceneEnd.acceptableInput || !timedFilmDuration.acceptableInput) {
            root.actionError = "Enter valid scene times and film length."
            return {ok: false, error: root.actionError}
        }
        const result = viewport.exportTimedFilm(destination, {
            startSeconds: Number(filmSceneStart.text), endSeconds: Number(filmSceneEnd.text),
            durationSeconds: Number(timedFilmDuration.text), fps: Number(timedFilmFps.currentText),
            cameraMotion: timedFilmCamera.currentIndex === 1 ? "saved-path" : "current-camera"
        }, Number(exportEdge.currentText))
        root.actionError = String(result.error || "")
        if (result.ok) timedFilmPopup.close()
        return result
    }
    FileDialog {
        id: filmDestination
        title: root.spacetimeProject ? "Export 4D film" : "Export camera path as a film"
        fileMode: FileDialog.SaveFile
        nameFilters: ["MP4 film (*.mp4)"]
        defaultSuffix: "mp4"
        onAccepted: {
            const result = root.spacetimeProject ? root.exportTimedFilmTo(selectedFile.toString())
                : viewport.exportFilm(selectedFile.toString(), Number(exportEdge.currentText))
            root.actionError = String(result.error || "")
        }
    }
    Popup {
        id: timedFilmPopup
        objectName: "sceneTimedFilmPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(theme.scaled(520), root.width - theme.space24)
        modal: true
        padding: theme.space16
        background: PopupSurface { radius: theme.radiusSurface; fillColor: theme.ink900 }
        onOpened: {
            filmSceneStart.text = "0"
            filmSceneEnd.text = String(viewport.status.timeDurationSeconds || 0)
            timedFilmDuration.text = String(Math.max(0.5, Math.min(600, Number(viewport.status.timeDurationSeconds || 0))))
            timedFilmCamera.currentIndex = 0
            root.actionError = ""
        }
        contentItem: ColumnLayout {
            spacing: 16
            Label { text: "Export 4D film"; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold; color: theme.textStrong }
            Label { Layout.fillWidth: true; text: "Scene time range (seconds from capture start)"; wrapMode: Text.WordWrap; color: theme.textMain }
            RowLayout {
                Layout.fillWidth: true
                ToneTextField { id: filmSceneStart; objectName: "sceneFilmTimeStart"; Layout.fillWidth: true; placeholderText: "Start"; validator: DoubleValidator { bottom: 0; top: Number(viewport.status.timeDurationSeconds || 0); locale: "C" } }
                Label { text: "→"; color: theme.textMuted }
                ToneTextField { id: filmSceneEnd; objectName: "sceneFilmTimeEnd"; Layout.fillWidth: true; placeholderText: "End"; validator: DoubleValidator { bottom: 0; top: Number(viewport.status.timeDurationSeconds || 0); locale: "C" } }
            }
            Label { Layout.fillWidth: true; text: "Reverse the range to reverse motion. Equal times freeze the scene."; wrapMode: Text.WordWrap; color: theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Film length"; color: theme.textMain }
                ToneTextField { id: timedFilmDuration; objectName: "sceneTimedFilmDuration"; Layout.preferredWidth: 90; validator: DoubleValidator { bottom: 0.5; top: 600; locale: "C" } }
                Label { text: "s"; color: theme.textMuted }
                Item { Layout.fillWidth: true }
                ToneComboBox { id: timedFilmFps; accessibleLabel: "Timed film frame rate"; model: ["24", "25", "30", "60"] }
                Label { text: "fps"; color: theme.textMuted }
            }
            ToneComboBox {
                id: timedFilmCamera
                accessibleLabel: "Timed film camera option"
                Layout.fillWidth: true
                model: Number(viewport.status.pathDuration || 0) > 0
                    ? ["Hold current camera", "Fit saved camera path to film length"] : ["Hold current camera"]
            }
            Label { Layout.fillWidth: true; visible: root.actionError.length > 0; text: root.actionError; wrapMode: Text.WordWrap; color: theme.textMain }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancel"; onClicked: timedFilmPopup.close() }
                ActionButton {
                    text: "Choose destination…"
                    tone: "primary"
                    disabledReason: "Enter valid scene times and film duration."
                    enabled: filmSceneStart.acceptableInput && filmSceneEnd.acceptableInput && timedFilmDuration.acceptableInput
                    onClicked: filmDestination.open()
                }
            }
        }
    }
    Popup {
        id: pathPopup
        objectName: "sceneCameraPathPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(theme.scaled(520), root.width - theme.space24)
        modal: true
        padding: theme.space16
        background: PopupSurface { radius: theme.radiusSurface; fillColor: theme.ink900 }
        onOpened: {
            const settings = viewport.status.cameraPath || {}
            root.pathDraft = (settings.viewIds || []).slice()
            pathSeconds.value = Number(settings.segmentSeconds || 3)
            pathFps.currentIndex = Math.max(0, [24, 25, 30, 60].indexOf(Number(settings.fps || 24)))
            root.actionError = ""
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: "Camera path"; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold; color: theme.textStrong }
            Label { Layout.fillWidth: true; text: "Order saved shots. The camera eases to a stop at each shot."; wrapMode: Text.WordWrap; color: theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                ToneComboBox { id: pathShot; accessibleLabel: "Saved camera shot"; Layout.fillWidth: true; model: viewport.status.savedViews || []; textRole: "name"; valueRole: "id" }
                ActionButton { text: "Add shot"; enabled: pathShot.count > 0 && root.pathDraft.length < 128; onClicked: root.pathDraft = root.pathDraft.concat([String(pathShot.currentValue)]) }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, Math.max(48, root.pathDraft.length * 42))
                clip: true
                ColumnLayout {
                    width: parent.width
                    Repeater {
                        model: root.pathDraft
                        delegate: RowLayout {
                            id: shotRow
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: theme.textMain
                                text: {
                                    const views = viewport.status.savedViews || []
                                    for (const view of views) if (view.id === shotRow.modelData) return (shotRow.index + 1) + ". " + view.name
                                    return "Missing shot"
                                }
                            }
                            ActionButton {
                                text: "Up"; compact: true; enabled: shotRow.index > 0
                                onClicked: { const next = root.pathDraft.slice(); const value = next.splice(shotRow.index, 1)[0]; next.splice(shotRow.index - 1, 0, value); root.pathDraft = next }
                            }
                            ActionButton {
                                text: "Remove"; compact: true
                                onClicked: { const next = root.pathDraft.slice(); next.splice(shotRow.index, 1); root.pathDraft = next }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Label { text: "Seconds per transition"; color: theme.textMain }
                ToneSpinBox {
                    id: pathSeconds
                    objectName: "scenePathTransitionSeconds"
                    accessibleLabel: "Seconds per camera transition"
                    accessibleDescription: "Duration of each segment when moving between saved camera views."
                    valueUnit: "seconds"
                    from: 1
                    to: 60
                    value: 3
                    editable: true
                }
                ToneComboBox { id: pathFps; accessibleLabel: "Camera path frame rate"; model: ["24", "25", "30", "60"] }
                Label { text: "fps"; color: theme.textMain }
            }
            Label { Layout.fillWidth: true; visible: root.actionError.length > 0; text: root.actionError; color: theme.textMain; wrapMode: Text.WordWrap }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancel"; onClicked: pathPopup.close() }
                ActionButton {
                    text: "Save path"; tone: "primary"; enabled: root.pathDraft.length >= 2
                    disabledReason: "Add at least two saved views to create a camera path."
                    onClicked: {
                        const result = viewport.saveCameraPath({viewIds: root.pathDraft, segmentSeconds: pathSeconds.value, fps: Number(pathFps.currentText)})
                        root.actionError = String(result.error || "")
                        if (result.ok) pathPopup.close()
                    }
                }
            }
        }
    }
    FileDialog {
        id: photoDestination
        title: root.editPhotoAfterRender ? "Render a photo to edit" : "Export this camera as a photo"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG photo (*.png)", "JPEG photo (*.jpg *.jpeg)"]
        defaultSuffix: "png"
        onAccepted: {
            const result = root.editPhotoAfterRender ? root.renderPhotoForEditing(selectedFile.toString())
                : viewport.exportPhoto(selectedFile.toString(), Number(exportEdge.currentText))
            root.actionError = String(result.error || "")
        }
    }
    Theme { id: theme }
    Popup {
        id: saveViewPopup
        objectName: "sceneSaveViewPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(theme.scaled(360), root.width - theme.space24)
        modal: true
        padding: theme.space16
        onOpened: { viewName.clear(); viewName.forceActiveFocus() }
        background: PopupSurface { radius: theme.radiusSurface; fillColor: theme.ink900 }
        contentItem: ColumnLayout {
            spacing: 16
            Label { text: "Save this camera view"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
            ToneTextField {
                id: viewName
                objectName: "sceneViewName"
                Layout.fillWidth: true
                maximumLength: 80
                placeholderText: "Shot name"
                onAccepted: if (text.trim().length > 0) saveShot.clicked()
            }
            Label {
                Layout.fillWidth: true
                visible: root.actionError.length > 0
                text: root.actionError
                wrapMode: Text.WordWrap
                color: theme.textMain
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancel"; onClicked: saveViewPopup.close() }
                ActionButton {
                    id: saveShot
                    text: "Save view"
                    tone: "primary"
                    disabledReason: "Enter a name for this camera view."
                    enabled: viewName.text.trim().length > 0
                    onClicked: {
                        const result = viewport.saveView(viewName.text)
                        root.actionError = String(result.error || "")
                        if (result.ok) saveViewPopup.close()
                    }
                }
            }
        }
    }
    FolderDialog {
        id: capturePicker
        title: "Choose overlapping photographs of one static scene"
        onAccepted: {
            captureMode.currentIndex = 0; capturePath.text = selectedFolder.toString()
            projectPath.text = root.controller.suggestedProjectPath(capturePath.text)
            root.showCaptureSetup = true; sceneControls.open()
        }
    }
    FileDialog {
        id: videoPicker
        title: "Choose a video to turn into a 3D scene"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Video (*.mov *.MOV *.mp4 *.MP4 *.m4v *.mkv)"]
        onAccepted: root.prepareVideoInput(selectedFile.toString())
    }
    FileDialog {
        id: trainerPicker
        title: "Locate the installed Brush trainer (brush_app)"
        fileMode: FileDialog.OpenFile
        onAccepted: root.actionError = String(root.controller.setTrainerExecutable(selectedFile.toString()).error || "")
    }
    FileDialog {
        id: timedCapturePicker
        title: "Choose calibrated, synchronized capture-4d.json"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Timed capture manifest (*.json)"]
        onAccepted: capturePath.text = selectedFile.toString()
    }
    FolderDialog {
        id: projectPicker
        title: "Choose where to create the new scene project"
        onAccepted: projectPath.text = selectedFolder.toString() + "/Scene-" + Qt.formatDateTime(new Date(), "yyyyMMdd-hhmmss")
    }
    FolderDialog {
        id: reopenPicker
        title: "Open an existing scene project"
        onAccepted: {
            const result = root.controller.openProject(selectedFolder.toString())
            root.actionError = String(result.error || "")
        }
    }
        Rectangle {
            anchors.fill: parent
            objectName: "sceneFullScreenSurface"
            color: theme.ink950
            radius: 0
            border.color: theme.lineSoft
            clip: true
            GaussianSceneViewport {
                id: viewport
                objectName: "gaussianSceneViewport"
                anchors.fill: parent
                anchors.margins: 0
                source: root.scene
                onSourceChanged: root.focusPickArmed = false
                MouseArea {
                    objectName: "sceneNavigationSurface"
                    anchors.fill: parent
                    enabled: Boolean(viewport.status.frameAvailable)
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    property real previousX: 0
                    property real previousY: 0
                    property bool suppressFocusDoubleClick: false
                    cursorShape: root.focusPickArmed ? Qt.CrossCursor : Qt.ArrowCursor
                    onPressed: mouse => { previousX = mouse.x; previousY = mouse.y; if (!root.focusPickArmed) viewport.beginNavigation() }
                    onReleased: if (!root.focusPickArmed) viewport.endNavigation()
                    onCanceled: viewport.endNavigation()
                    onClicked: mouse => {
                        if (!root.focusPickArmed) { suppressFocusDoubleClick = false; return }
                        suppressFocusDoubleClick = true
                        if (mouse.button !== Qt.LeftButton) { root.focusPickArmed = false; root.actionError = ""; return }
                        const result = viewport.pickFocusAt(mouse.x, mouse.y)
                        root.actionError = String(result.error || "")
                        if (result.ok) root.focusPickArmed = false
                    }
                    onPositionChanged: mouse => {
                        if (!pressed || root.focusPickArmed) return
                        const dx = mouse.x - previousX
                        const dy = mouse.y - previousY
                        if (pressedButtons & Qt.LeftButton) viewport.orbit(dx * 0.25, dy * 0.25)
                        else viewport.pan(-dx / width, -dy / height)
                        previousX = mouse.x
                        previousY = mouse.y
                    }
                    onWheel: wheel => { viewport.dolly(wheel.angleDelta.y / 120); wheel.accepted = true }
                    onDoubleClicked: if (!suppressFocusDoubleClick && !root.focusPickArmed && !viewport.status.focusPicking) viewport.resetCamera()
                }
            }
            ColumnLayout {
                visible: !viewport.status.frameAvailable
                anchors.centerIn: parent
                width: Math.min(680, parent.width - 48)
                spacing: 18
                ProgressStrip {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 128
                    Layout.preferredHeight: 2
                    progress: -1
                    visible: Boolean(root.scene.busy)
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: root.renderable ? String(viewport.status.state).replace(/-/g, " ") : root.stageLabel
                    font.family: theme.fontUi
                    font.pixelSize: theme.scaled(20)
                    font.weight: Font.DemiBold
                    color: theme.textStrong
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    objectName: "sceneCaptureStatus"
                    text: root.actionError || String(viewport.status.error || root.scene.error || root.scene.persistenceError || "") || (root.timedProject
                        ? "Calibrated frames are prepared as sRGB images with preserved camera poses and timestamps. Temporal training, novel-view rendering and 4D film export are not available yet. Originals remain untouched."
                        : root.renderable
                        ? "Loading the verified scene and preparing the native camera viewport…"
                        : root.scene.busy ? "Keep this window open. You can use other workspaces while this runs; the viewer opens here when the scene is ready."
                        : "Drop a video here, or choose overlapping photos. We’ll build a 3D scene you can move around in and photograph. Everything stays on this Mac.")
                    color: theme.textMain
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !root.scene.busy && !root.renderable
                    ActionButton { objectName: "sceneChooseVideoButton"; text: "Create from video"; tone: "primary"; enabled: !!root.controller; onClicked: videoPicker.open() }
                    ActionButton { text: "Use photos"; enabled: !!root.controller; onClicked: capturePicker.open() }
                }
                ActionButton {
                    objectName: "sceneBuildPartial"
                    Layout.alignment: Qt.AlignHCenter
                    visible: !root.scene.busy && root.scene.state === "failed" && Boolean(root.scene.sourceVideo)
                        && Number(root.scene.stageIndex) === 2 && Number(root.scene.registeredViews) >= 8
                    text: "Build partial scene · " + String(root.scene.registeredViews || 0) + " views"
                    tone: "primary"
                    onClicked: {
                        const result = root.controller.buildPartialScene()
                        root.actionError = String(result.error || "")
                    }
                }
                ProgressBar {
                    objectName: "sceneBuildProgress"
                    Layout.fillWidth: true
                    visible: Boolean(root.scene.busy)
                    indeterminate: root.scene.state !== "training-gaussians" && root.scene.state !== "extracting-video"
                    value: root.scene.state === "extracting-video"
                        ? Math.min(1, Number(root.scene.extractedFrames || 0) / Math.max(1, Number(root.scene.requestedFrames || 1)))
                        : Math.min(1, Number(root.scene.iteration || 0) / Math.max(1, Number(root.scene.trainingSteps || 1)))
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    visible: Boolean(root.scene.busy)
                    text: root.scene.state === "extracting-video"
                        ? String(root.scene.extractedFrames || 0) + " / " + String(root.scene.requestedFrames || 0) + " sampled frames · then camera alignment and training"
                        : root.scene.state === "training-gaussians" ? String(root.scene.iteration || 0) + " / " + String(root.scene.trainingSteps || 0) + " steps"
                        : "This stage can take several minutes."
                    color: theme.textMuted
                }
                ActionButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: Boolean(root.scene.busy)
                    enabled: root.scene.state !== "canceling"
                    text: "Cancel scene creation"
                    onClicked: root.controller.cancel()
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    objectName: "sceneCaptureCounts"
                    text: root.timedProject
                        ? String((root.scene.capture || {}).cameraCount || 0) + " cameras · " + String((root.scene.capture || {}).timeCount || 0) + " capture times · " + String((root.scene.capture || {}).imageCount || 0) + " images · " + Number((root.scene.capture || {}).durationSeconds || 0).toFixed(3) + " s"
                        : root.scene.sourceCount ? String(root.scene.sourceCount) + " captures · " + String(root.scene.registeredViews || 0) + " registered views" : ""
                    color: theme.textMuted
                }
                Label { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; text: String(root.scene.projectPath || ""); color: theme.textMuted }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; text: root.spacetimeProject ? "Open scene verifies the 4D artifact against its saved hash." : root.timedProject ? "A prepared timed capture is not a trained scene." : "Best with an unmoving scene and a camera moving through it. Blur, moving objects and unseen surfaces can leave gaps."; color: theme.textMuted }
            }
        }
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 100
        gradient: Gradient {
            GradientStop { position: 0; color: "#b3000000" }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    Popup {
        id: sceneControls
        objectName: "sceneControlsPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(880, root.width - 48)
        height: Math.min(sceneControlContents.implicitHeight + padding * 2, root.height - 48)
        padding: theme.space16
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: PopupSurface { radius: theme.radiusSurface; fillColor: theme.ink900 }
        contentItem: Flickable {
            clip: true
            contentWidth: width
            contentHeight: sceneControlContents.implicitHeight
            ScrollBar.vertical: ScrollBar {}
        }
    }
    Popup {
        id: photoControls
        objectName: "scenePhotoControlsPopup"
        property int page: 0
        property bool halationPage: false
        property bool renderDetailsExpanded: false
        onPageChanged: { if (contentItem) contentItem.contentY = 0 }
        x: Math.max(12, root.width - width - 20)
        y: 72
        width: Math.min(370, root.width - 24)
        height: Math.min(photoControlContents.implicitHeight + topPadding + bottomPadding, Math.max(100, root.height - y - 20))
        padding: theme.space16
        modal: false
        closePolicy: Popup.CloseOnEscape
        background: PopupSurface { radius: theme.radiusSurface; fillColor: theme.ink900 }
        contentItem: Flickable {
            objectName: "scenePhotoControlsScroll"
            clip: true
            contentWidth: width
            contentHeight: photoControlContents.implicitHeight
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: photoControlContents
                width: parent.width
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Photo settings"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "Done"; onClicked: photoControls.close() }
                }
                ToneComboBox {
                    objectName: "scenePhotoSettingsSectionCombo"
                    accessibleLabel: "Photo settings section"
                    Layout.fillWidth: true
                    currentIndex: photoControls.page
                    model: ["Tone", "Focus", "Aperture", "Film", "Camera"]
                    onActivated: photoControls.page = currentIndex
                }
                Label {
                    visible: photoControls.page === 4
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMain
                    text: "Full-frame thin lens needs a measured scene scale in metres per reconstruction unit. After applying, set focal length and focus in Focus, and f-stop in Aperture."
                }
                RowLayout {
                    visible: photoControls.page === 4; Layout.fillWidth: true
                    Label { text: "Metres / scene unit"; color: theme.textMain }
                    ToneTextField {
                        id: physicalScale; objectName: "scenePhysicalScaleField"; Layout.fillWidth: true
                        text: root.physicalCameraActive ? Number(root.physicalCamera.metresPerSceneUnit).toPrecision(8) : ""
                        placeholderText: "Measured scale"
                        validator: DoubleValidator { bottom: 0.000001; top: 1000000; locale: "C" }
                        Accessible.name: "User-supplied metres per scene unit"
                    }
                }
                ToneComboBox {
                    id: physicalGate; objectName: "scenePhysicalGateCombo"
                    accessibleLabel: "Virtual film gate orientation"
                    visible: photoControls.page === 4; Layout.fillWidth: true
                    model: ["Full frame · 36 × 24 mm", "Full frame portrait · 24 × 36 mm"]
                    currentIndex: root.physicalCameraActive && Number(root.physicalCamera.sensorWidthMm) < Number(root.physicalCamera.sensorHeightMm) ? 1 : 0
                }
                ActionButton {
                    objectName: "sceneApplyPhysicalCameraButton"
                    visible: photoControls.page === 4; enabled: physicalScale.acceptableInput
                    text: root.physicalCameraActive ? "Update physical camera" : "Use full-frame thin lens"
                    onClicked: {
                        const settings = root.physicalCameraActive ? Object.assign({}, root.physicalCamera)
                            : {schema: "framehouse-physical-thin-lens-v1", focalLengthMm: 50, fNumber: 2.8, focusDistanceM: 2}
                        settings.metresPerSceneUnit = Number(physicalScale.text)
                        settings.sensorWidthMm = physicalGate.currentIndex === 1 ? 24 : 36
                        settings.sensorHeightMm = physicalGate.currentIndex === 1 ? 36 : 24
                        root.actionError = String(viewport.setPhysicalCamera(settings).error || "")
                    }
                }
                ActionButton {
                    objectName: "sceneClearPhysicalCameraButton"
                    visible: photoControls.page === 4 && root.physicalCameraActive
                    text: "Use scene-unit controls · keep this view"
                    onClicked: root.actionError = String(viewport.clearPhysicalCamera().error || "")
                }
                RowLayout {
                    visible: photoControls.page === 1 && root.physicalCameraActive; Layout.fillWidth: true
                    Label { text: "Focal length (mm)"; color: theme.textMain }
                    ToneTextField {
                        objectName: "scenePhysicalFocalField"; Layout.fillWidth: true
                        property bool userEdited: false
                        text: Number(root.physicalCamera.focalLengthMm || 50).toPrecision(6)
                        validator: DoubleValidator { bottom: 1; top: 2000; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: {
                            if (!userEdited) return
                            userEdited = false
                            if (acceptableInput) root.updatePhysicalSetting("focalLengthMm", Number(text))
                            else root.actionError = "Enter a focal length from 1 to 2000 mm."
                        }
                        Accessible.name: "Physical focal length in millimetres"
                    }
                }
                RowLayout {
                    visible: photoControls.page === 2 && root.physicalCameraActive; Layout.fillWidth: true
                    Label { text: "Aperture (f-number)"; color: theme.textMain }
                    ToneTextField {
                        objectName: "scenePhysicalFNumberField"; Layout.fillWidth: true
                        property bool userEdited: false
                        text: Number(root.physicalCamera.fNumber || 2.8).toPrecision(4)
                        validator: DoubleValidator { bottom: 0.5; top: 128; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: {
                            if (!userEdited) return
                            userEdited = false
                            if (acceptableInput) root.updatePhysicalSetting("fNumber", Number(text))
                            else root.actionError = "Enter an f-number from 0.5 to 128."
                        }
                        Accessible.name: "Physical aperture f-number"
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: photoControls.page === 1 && !root.physicalCameraActive
                    text: "Lens framing  " + Number(root.photoSettings.focalScale).toFixed(2) + "×"
                    color: theme.textMain
                }
                ToolValueSlider {
                    id: photoFocal
                    label: "Lens framing relative to the current camera"
                    valueText: Number(value).toFixed(2) + "×"
                    visible: photoControls.page === 1 && !root.physicalCameraActive
                    objectName: "scenePhotoFocalSlider"
                    Layout.fillWidth: true
                    from: 0.25; to: 4; stepSize: 0.05
                    value: Number(root.photoSettings.focalScale)
                    onPressedChanged: pressed ? viewport.beginNavigation() : viewport.endNavigation()
                    onMoved: root.updatePhotoSetting("focalScale", value)
                }
                Label {
                    Layout.fillWidth: true
                    text: "Exposure  " + (Number(root.photoSettings.exposureEv) > 0 ? "+" : "")
                        + Number(root.photoSettings.exposureEv).toFixed(2) + " EV"
                    visible: photoControls.page === 0
                    color: theme.textMain
                }
                ToolValueSlider {
                    id: photoExposure
                    label: "Photographic exposure compensation"
                    valueText: Number(value).toFixed(2) + " EV"
                    visible: photoControls.page === 0
                    objectName: "scenePhotoExposureSlider"
                    Layout.fillWidth: true
                    from: -6; to: 6; stepSize: 0.25
                    value: Number(root.photoSettings.exposureEv)
                    onMoved: root.updatePhotoSetting("exposureEv", value)
                }
                ToneComboBox {
                    id: photoLook
                    visible: photoControls.page === 0
                    objectName: "scenePhotoLookCombo"
                    accessibleLabel: "Photographic tone look"
                    Layout.fillWidth: true
                    model: ["Neutral", "Soft highlights", "Monochrome"]
                    currentIndex: root.photoSettings.look === "soft-highlight" ? 1 : root.photoSettings.look === "monochrome" ? 2 : 0
                    onActivated: index => root.updatePhotoSetting("look", ["neutral", "soft-highlight", "monochrome"][index])
                }
                RowLayout {
                    visible: photoControls.page === 1
                    Layout.fillWidth: true
                    Label { text: root.physicalCameraActive ? "Focus distance (m)" : "Focus distance (scene units)"; color: theme.textMain }
                    ToneTextField {
                        objectName: "sceneLensFocusField"
                        property bool userEdited: false
                        onTextEdited: userEdited = true
                        Layout.fillWidth: true
                        text: Number(root.physicalCameraActive ? root.physicalCamera.focusDistanceM : root.lensSettings.focusDistance).toPrecision(6)
                        validator: DoubleValidator { bottom: root.physicalCameraActive ? 0.001 : 0.01; top: 100000000; locale: "C" }
                        onEditingFinished: {
                            if (!userEdited) return
                            userEdited = false
                            if (acceptableInput) {
                                if (root.physicalCameraActive) root.updatePhysicalSetting("focusDistanceM", Number(text))
                                else root.updateLensSetting("focusDistance", Number(text))
                            } else root.actionError = "Enter a valid focus distance in the displayed units."
                        }
                        Accessible.name: root.physicalCameraActive ? "Focus distance in metres from the lens plane" : "Focus distance in scene units"
                    }
                }
                ActionButton {
                    objectName: "scenePickFocusButton"
                    text: "Pick focus in image"
                    visible: photoControls.page === 1
                    enabled: !viewport.status.focusPicking && Boolean(viewport.status.frameAvailable)
                    onClicked: { root.focusPickArmed = true; root.actionError = ""; photoControls.close(); root.forceActiveFocus() }
                }
                ActionButton {
                    objectName: "sceneUseCaptureFocusButton"
                    text: "Use capture depth estimate"
                    visible: photoControls.page === 1 && !root.physicalCameraActive
                    enabled: Boolean(viewport.status.frameAvailable)
                    onClicked: root.updateLensSetting("focusDistance", Math.max(0.01, Math.min(100000000, Number(viewport.status.sceneFocusDistance || 1))))
                }
                Label {
                    Layout.fillWidth: true
                    text: "Aperture radius  " + Number(root.lensSettings.apertureRadius).toPrecision(3)
                        + (Number(root.lensSettings.apertureRadius) === 0 ? " · Pinhole" : " scene units")
                    visible: photoControls.page === 2 && !root.physicalCameraActive
                    color: theme.textMain
                }
                ToolValueSlider {
                    objectName: "sceneLensApertureSlider"
                    label: "Aperture radius in scene units, zero disables depth of field"
                    valueText: value === 0 ? "Pinhole" : Number(value).toPrecision(3) + " scene units"
                    visible: photoControls.page === 2 && !root.physicalCameraActive
                    Layout.fillWidth: true
                    from: 0
                    to: Math.min(Number(root.lensSettings.focusDistance) * 0.25, Number(viewport.status.sceneFocusDistance || 1) * 0.1)
                    value: Number(root.lensSettings.apertureRadius)
                    onPressedChanged: pressed ? viewport.beginNavigation() : viewport.endNavigation()
                    onMoved: root.updateLensSetting("apertureRadius", value)
                }
                ToneComboBox {
                    objectName: "sceneLensShapeCombo"
                    accessibleLabel: "Aperture blade shape"
                    visible: photoControls.page === 2
                    Layout.fillWidth: true
                    model: ["Circular aperture", "6 blades", "7 blades", "8 blades", "9 blades"]
                    currentIndex: Number(root.lensSettings.blades) === 0 ? 0 : Number(root.lensSettings.blades) - 5
                    onActivated: index => root.updateLensSetting("blades", index === 0 ? 0 : index + 5)
                }
                Label { visible: photoControls.page === 2 && Number(root.lensSettings.blades) > 0; text: "Blade roundness  " + Math.round(Number(root.lensSettings.roundness) * 100) + "%"; color: theme.textMain }
                ToolValueSlider {
                    label: "Aperture blade roundness"
                    valueText: Math.round(value * 100) + "%"
                    Layout.fillWidth: true
                    visible: photoControls.page === 2 && Number(root.lensSettings.blades) > 0
                    from: 0; to: 1; value: Number(root.lensSettings.roundness)
                    onPressedChanged: pressed ? viewport.beginNavigation() : viewport.endNavigation()
                    onMoved: root.updateLensSetting("roundness", value)
                }
                Label { visible: photoControls.page === 2 && Number(root.lensSettings.blades) > 0; text: "Aperture rotation  " + Number(root.lensSettings.rotationDegrees).toFixed(1) + "°"; color: theme.textMain }
                ToolValueSlider {
                    label: "Aperture rotation in degrees"
                    valueText: Number(value).toFixed(1) + "°"
                    Layout.fillWidth: true
                    visible: photoControls.page === 2 && Number(root.lensSettings.blades) > 0
                    from: 0; to: 360; value: Number(root.lensSettings.rotationDegrees)
                    onPressedChanged: pressed ? viewport.beginNavigation() : viewport.endNavigation()
                    onMoved: root.updateLensSetting("rotationDegrees", value)
                }
                ToneComboBox {
                    objectName: "sceneLensQualityCombo"
                    accessibleLabel: "Settled photo aperture sampling quality"
                    visible: photoControls.page === 2
                    Layout.fillWidth: true
                    model: ["Draft photo · 16 samples", "Standard · 32 samples", "Fine · 64 samples", "High · 128 samples",
                        "Extra fine · 256 samples", "Bokeh detail · 512 samples", "Bokeh detail · 1024 samples",
                        "Ultra bokeh · 2048 samples", "Final bokeh · 4096 samples"]
                    currentIndex: [16, 32, 64, 128, 256, 512, 1024, 2048, 4096].indexOf(Number(root.lensSettings.samples))
                    onActivated: index => root.updateLensSetting("samples", [16, 32, 64, 128, 256, 512, 1024, 2048, 4096][index])
                }
                Label {
                    objectName: "sceneLensHighQualityHint"
                    visible: photoControls.page === 2 && Number(root.lensSettings.samples) > 128
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Higher sampling smooths defocused highlights, but takes longer. Move to interrupt refinement. Photos and films use the selected quality."
                    color: theme.textMuted
                }
                ToneComboBox {
                    objectName: "sceneFilmSectionCombo"
                    accessibleLabel: "Film effect controls"
                    visible: photoControls.page === 3; Layout.fillWidth: true
                    model: ["Grain", "Halation"]
                    currentIndex: photoControls.halationPage ? 1 : 0
                    onActivated: index => photoControls.halationPage = index === 1
                }
                Label { visible: photoControls.page === 3 && !photoControls.halationPage; text: "Grain  " + Number(root.filmSettings.grainAmount).toFixed(2); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmGrainSlider"
                    label: "Film grain amount"
                    valueText: Number(value).toFixed(2)
                    visible: photoControls.page === 3 && !photoControls.halationPage; Layout.fillWidth: true
                    from: 0; to: 1; value: Number(root.filmSettings.grainAmount)
                    onMoved: root.updateFilmSetting("grainAmount", value)
                }
                Label { visible: photoControls.page === 3 && !photoControls.halationPage; text: "Grain size  " + Number(root.filmSettings.grainSize).toFixed(1); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmGrainSizeSlider"
                    label: "Grain size at 2048 pixel long edge"
                    valueText: Number(value).toFixed(1) + " px"
                    visible: photoControls.page === 3 && !photoControls.halationPage; Layout.fillWidth: true
                    from: 0.5; to: 6; stepSize: 0.1; value: Number(root.filmSettings.grainSize)
                    onMoved: root.updateFilmSetting("grainSize", value)
                }
                Label { visible: photoControls.page === 3 && photoControls.halationPage; text: "Halation  " + Number(root.filmSettings.halationAmount).toFixed(2); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmHalationSlider"
                    label: "Red orange highlight halation amount"
                    valueText: Number(value).toFixed(2)
                    visible: photoControls.page === 3 && photoControls.halationPage; Layout.fillWidth: true
                    from: 0; to: 1; value: Number(root.filmSettings.halationAmount)
                    onMoved: root.updateFilmSetting("halationAmount", value)
                }
                Label { visible: photoControls.page === 3 && photoControls.halationPage; text: "Halation spread  " + Number(root.filmSettings.halationRadius).toFixed(1); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmRadiusSlider"
                    label: "Halation spread at 2048 pixel long edge"
                    valueText: Number(value).toFixed(1) + " px"
                    visible: photoControls.page === 3 && photoControls.halationPage; Layout.fillWidth: true
                    from: 0.5; to: 16; stepSize: 0.5; value: Number(root.filmSettings.halationRadius)
                    onMoved: root.updateFilmSetting("halationRadius", value)
                }
                Label { visible: photoControls.page === 3 && photoControls.halationPage; text: "Highlight threshold  " + Number(root.filmSettings.halationThreshold).toFixed(2); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmThresholdSlider"
                    label: "Halation threshold in linearized luminance"
                    valueText: Number(value).toFixed(2)
                    visible: photoControls.page === 3 && photoControls.halationPage; Layout.fillWidth: true
                    from: 0.1; to: 4; stepSize: 0.05; value: Number(root.filmSettings.halationThreshold)
                    onMoved: root.updateFilmSetting("halationThreshold", value)
                }
                ActionButton {
                    objectName: "sceneRenderDetailsToggle"
                    text: photoControls.renderDetailsExpanded ? "Hide render details" : "Render details"
                    compact: true
                    onClicked: photoControls.renderDetailsExpanded = !photoControls.renderDetailsExpanded
                }
                Label {
                    visible: photoControls.renderDetailsExpanded
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Move freely with this panel open. Stop to resolve your photo. Saved views and exports keep these settings."
                    color: theme.textMuted
                }
                Label {
                    visible: photoControls.renderDetailsExpanded
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: photoControls.page === 3
                        ? "Film adjustments update the resolved photo without rerendering the lens. Creative effects, not a measured stock. Sizes reference a 2048 px long edge; grain stays fixed per shot."
                        : root.physicalCameraActive
                        ? "Ideal thin lens with user-supplied scene scale, not a measured lens profile. F-stop changes blur, not absolute exposure. Film calibration is still in development."
                        : "Focus and aperture use scene units, not metres or calibrated f-stops. Motion uses a pinhole draft; stopping resolves the lens. Measured film profiles are still in development."
                    color: theme.textMuted
                    font.pixelSize: 12
                }
                ActionButton {
                    text: "Reset photo settings"
                    onClicked: {
                        const settings = {schema: "framehouse-gaussian-photo-settings-v1", focalScale: 1, exposureEv: 0, look: "neutral"}
                        if (root.physicalCameraActive) {
                            settings.schema = "framehouse-gaussian-photo-settings-v2"
                            settings.lens = Object.assign({}, root.lensSettings, {schema: "framehouse-thin-lens-v1",
                                blades: 0, rotationDegrees: 0, roundness: 0, samples: 32})
                        }
                        const result = viewport.setPhotoSettings(settings)
                        root.actionError = String(result.error || "")
                    }
                }
                Label { Layout.fillWidth: true; visible: root.actionError.length > 0; text: root.actionError; wrapMode: Text.WordWrap; color: theme.textMain }
            }
        }
    }
    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        spacing: 12
        ActionButton { text: "Library"; iconName: "chevron-left"; compact: true; visible: root.immersive; onClicked: root.exitViewerRequested() }
        Label { text: root.scene.partialScene ? "Partial scene · " + String(root.scene.registeredViews || 0) + " / " + String(root.scene.sourceCount || 0) + " views" : "Scene"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
        Item { Layout.fillWidth: true }
        ActionButton {
            objectName: "scenePhotoSettingsButton"
            text: "Photo settings"
            visible: root.renderable
            enabled: Boolean(viewport.status.frameAvailable)
            onClicked: photoControls.visible ? photoControls.close() : photoControls.open()
        }
        ActionButton {
            objectName: "sceneTakePhotoButton"
            text: viewport.status.exporting ? "Saving…" : "Take photo"
            visible: root.renderable
            enabled: viewport.status.previewPhase === "photo" && !viewport.status.exporting
            onClicked: { root.editPhotoAfterRender = false; photoDestination.open() }
        }
        ActionButton { text: "Open scene"; visible: !sceneControls.visible; enabled: root.controller && !root.scene.busy; onClicked: reopenPicker.open() }
        ActionButton { objectName: "sceneOptionsButton"; text: "Scene options"; visible: !sceneControls.visible; onClicked: sceneControls.open() }
        MenuButton {
            objectName: "sceneWorkspaceLayoutButton"
            text: "Layout"
            iconName: "grid"
            compact: true
            primaryOpensMenu: true
            tooltipText: "Choose Scene navigation and visible controls."
            menuItems: [
                {key: "navigation", label: "Workspace navigation", icon: "grid", checked: !root.immersive},
                {key: "photo-settings", label: "Photo settings", icon: "adjust", checked: photoControls.visible,
                    enabled: root.renderable, disabledReason: "Open a finished scene first."},
                {key: "status", label: "Preview status", icon: "info", checked: root.previewStatusVisible}
            ]
            onMenuItemTriggered: key => root.handleLayoutMenuAction(key)
        }
    }
    Rectangle {
        id: scenePreviewStatusSurface
        objectName: "scenePreviewStatusSurface"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: Math.min(root.width - theme.space24, liveStatus.implicitWidth + theme.space24)
        height: liveStatus.implicitHeight + theme.space16
        radius: theme.radiusSurface
        color: theme.alpha(theme.ink950, 0.85)
        visible: root.previewStatusVisible && Boolean(viewport.status.frameAvailable)
        Label {
            id: liveStatus
            objectName: "sceneLivePhotoStatus"
            anchors.centerIn: parent
            width: parent.width - theme.space24
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.family: theme.fontUi
            font.pixelSize: theme.typeBody
            color: theme.textStrong
            text: viewport.status.error ? "Preview error · Open scene options"
                : root.focusPickArmed ? (root.actionError || "PICK FOCUS · Click a solid surface · Esc to cancel")
                : viewport.status.focusPicking ? "Finding surface focus…"
                : viewport.status.focusPickError ? String(viewport.status.focusPickError)
                : viewport.status.exporting ? (Number(viewport.status.exportTilesTotal || 0) > 0
                    && Number(viewport.status.exportTilesCompleted || 0) < Number(viewport.status.exportTilesTotal)
                    ? "RENDERING PHOTO · " + Number(viewport.status.exportTilesCompleted || 0) + "/" + Number(viewport.status.exportTilesTotal) + " tiles"
                    : "SAVING PHOTO…")
                : viewport.status.previewPhase === "live" ? "Live preview"
                : viewport.status.previewPhase === "refining" ? (Number(viewport.status.refinementSamplesTotal || 1) > 1
                    ? "Resolving lens · " + Number(viewport.status.refinementSamplesCompleted || 0) + "/" + Number(viewport.status.refinementSamplesTotal) + " samples"
                    : "Resolving photo…")
                : "Photo preview"
        }
        HoverHandler { id: sceneStatusHover }
        AppToolTip {
            targetItem: scenePreviewStatusSurface
            visible: sceneStatusHover.hovered
            text: root.focusPickArmed ? "Click a solid surface to focus. Press Escape to cancel."
                : "Drag to orbit. Right-drag to pan. Scroll to move. Double-click to reset the camera."
        }
    }
    ColumnLayout {
        id: sceneControlContents
        parent: sceneControls.contentItem.contentItem
        width: sceneControls.availableWidth
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Label { text: root.captureReady && !root.showCaptureSetup ? "Your scene" : "Create a 3D scene"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
                Label { text: "Choose source media and a destination. Everything is built locally."; color: theme.textMuted; font.pixelSize: 13 }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                visible: root.captureReady
                text: root.showCaptureSetup ? "Hide capture setup" : "New capture"
                onClicked: root.showCaptureSetup = !root.showCaptureSetup
            }
            ActionButton { text: "Open scene"; enabled: root.controller && !root.scene.busy; onClicked: reopenPicker.open() }
            ActionButton { text: "Done"; onClicked: sceneControls.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.captureReady || root.showCaptureSetup
            Label { text: "Capture type"; Layout.preferredWidth: 104; color: theme.textMain; font.pixelSize: 13 }
            ToneComboBox {
                id: captureMode
                objectName: "sceneCaptureMode"
                accessibleLabel: "Capture type"
                model: ["Photos", "Timed 4D input", "Video"]
                enabled: !root.scene.busy
                onActivated: { capturePath.clear(); root.actionError = "" }
            }
            Label {
                Layout.fillWidth: true
                text: root.timedInput ? "Calibrated multi-camera input; preparation only." : root.videoInput ? "One video of a static scene. Frames are sampled automatically." : "Overlapping photos of one unmoving subject."
                wrapMode: Text.WordWrap
                color: theme.textMuted
                font.pixelSize: 12
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            visible: !root.captureReady || root.showCaptureSetup
            Label { text: "Source media"; Layout.preferredWidth: 104; color: theme.textMain; font.pixelSize: 13 }
            ToneTextField { id: capturePath; objectName: "sceneCapturePath"; Layout.fillWidth: true; placeholderText: root.timedInput ? "Calibrated capture-4d.json" : root.videoInput ? "Choose a MOV or MP4 video" : "Capture folder — JPG, PNG, TIFF or RAW"; enabled: !root.scene.busy; onEditingFinished: { if (!projectPath.text.length && root.controller) projectPath.text = root.controller.suggestedProjectPath(text) } }
            ActionButton { text: root.timedInput ? "Choose manifest" : root.videoInput ? "Choose video" : "Choose photos"; enabled: !root.scene.busy; onClicked: { if (root.timedInput) timedCapturePicker.open(); else if (root.videoInput) videoPicker.open(); else capturePicker.open() } }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.captureReady || root.showCaptureSetup
            Label { text: "Destination"; Layout.preferredWidth: 104; color: theme.textMain; font.pixelSize: 13 }
            ToneTextField { id: projectPath; objectName: "sceneProjectPath"; Layout.fillWidth: true; placeholderText: "New project folder (must not already exist)"; enabled: !root.scene.busy }
            ActionButton { text: "Choose location"; enabled: !root.scene.busy; onClicked: projectPicker.open() }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: (!root.captureReady || root.showCaptureSetup) && !root.timedInput
            Label { text: "Quality"; Layout.preferredWidth: 104; color: theme.textMain; font.pixelSize: 13 }
            ToneComboBox {
                id: captureQuality
                objectName: "sceneCaptureQuality"
                accessibleLabel: "Capture quality preset"
                model: ["Explore · faster", "Detail · slower"]
                currentIndex: 1
                enabled: !root.scene.busy
                onActivated: {
                    iterations.value = currentIndex === 0 ? 5000 : 30000
                    resolution.currentIndex = currentIndex === 0 ? 1 : 3
                    splatBudget.currentIndex = currentIndex === 0 ? 0 : 2
                }
            }
            Label { visible: root.videoInput; text: "Sampled frames"; color: theme.textMain }
            ToneSpinBox {
                id: videoFrames
                objectName: "sceneVideoFrameCount"
                accessibleLabel: "Sampled video frames"
                accessibleDescription: "Number of video frames used to reconstruct the scene."
                valueUnit: "frames"
                visible: root.videoInput
                from: 16
                to: 1920
                value: 480
                stepSize: 16
                editable: true
                enabled: !root.scene.busy
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: root.advancedCapture ? "Hide advanced" : "Advanced"; onClicked: root.advancedCapture = !root.advancedCapture }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: sceneControls.availableWidth < 748 ? 2 : 6
            columnSpacing: 8
            rowSpacing: 8
            visible: (!root.captureReady || root.showCaptureSetup) && (root.advancedCapture || root.timedInput)
            Label { text: "Training steps"; color: theme.textMain; visible: !root.timedInput }
            ToneSpinBox {
                id: iterations
                objectName: "sceneTrainingSteps"
                accessibleLabel: "Training steps"
                accessibleDescription: "Number of optimization steps used to reconstruct the scene."
                valueUnit: "steps"
                visible: !root.timedInput
                from: 200
                to: 100000
                value: 30000
                stepSize: 200
                editable: true
                enabled: !root.scene.busy
            }
            Label { text: "Image edge"; color: theme.textMain }
            ToneComboBox { id: resolution; objectName: "sceneTrainingResolution"; accessibleLabel: "Training image edge resolution"; model: ["800", "1600", "2400", "3200", "4096"]; currentIndex: 3; enabled: !root.scene.busy }
            Label { text: "Splat budget"; color: theme.textMain; visible: !root.timedInput }
            ToneComboBox {
                id: splatBudget
                objectName: "sceneTrainingSplatBudget"
                accessibleLabel: "Training splat budget"
                visible: !root.timedInput
                model: ["500,000", "1 million", "2 million"]
                currentIndex: 2
                readonly property int maximumSplats: [500000, 1000000, 2000000][currentIndex]
                enabled: !root.scene.busy
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.captureReady || root.showCaptureSetup
            Label {
                id: sceneCaptureActionGuidance
                objectName: "sceneCaptureActionGuidance"
                Layout.fillWidth: true
                visible: !root.scene.busy && (capturePath.text.length === 0 || projectPath.text.length === 0)
                text: capturePath.text.length === 0
                      ? (root.videoInput ? "Choose a video to begin." : root.timedInput ? "Choose a capture manifest to begin." : "Choose overlapping photos to begin.")
                      : "Choose where to save the new scene."
                color: theme.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillWidth: true; visible: !sceneCaptureActionGuidance.visible }
            ActionButton {
                objectName: "sceneStartCapture"
                tone: root.scene.busy ? "secondary" : "primary"
                text: root.scene.busy ? "Cancel operation" : (root.timedInput ? "Prepare timed capture" : "Create scene")
                enabled: root.controller && (root.scene.busy || (capturePath.text.length > 0 && projectPath.text.length > 0))
                disabledReason: capturePath.text.length === 0 ? "Choose source media to begin."
                                : projectPath.text.length === 0 ? "Choose a destination for the new scene."
                                : "Scene creation is unavailable."
                onClicked: {
                    if (root.scene.busy) { root.controller.cancel(); return }
                    const result = root.timedInput
                        ? root.controller.startTimedCapture(capturePath.text, projectPath.text, Number(resolution.currentText))
                        : root.videoInput ? root.controller.startVideo(capturePath.text, projectPath.text, iterations.value, Number(resolution.currentText), splatBudget.maximumSplats, videoFrames.value)
                        : root.controller.startCapture(capturePath.text, projectPath.text, iterations.value, Number(resolution.currentText), splatBudget.maximumSplats)
                    root.actionError = String(result.error || "")
                    if (result.ok) { root.showCaptureSetup = false; sceneControls.close() }
                }
            }
        }
        Label {
            objectName: "sceneTrainingResourceHint"
            Layout.fillWidth: true
            visible: (!root.captureReady || root.showCaptureSetup) && !root.timedInput
            wrapMode: Text.WordWrap
            color: theme.textMuted
            text: String(iterations.value) + " steps · " + resolution.currentText + " px · up to " + splatBudget.currentText + " splats. "
                + "Explore gets you into a first scene sooner; Detail takes substantially longer. Neither guarantees sharp geometry. Originals remain untouched."
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.timedInput && !root.scene.trainerAvailable
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "One-time setup: select the installed Brush trainer. Its location is remembered."; color: theme.textMuted }
            ActionButton { text: "Locate Brush…"; enabled: !root.scene.busy; onClicked: trainerPicker.open() }
        }
        Label {
            Layout.fillWidth: true
            visible: !root.timedInput && (!root.scene.reconstructionAvailable || (root.videoInput && !root.scene.videoAvailable))
            wrapMode: Text.WordWrap
            text: !root.scene.reconstructionAvailable ? "COLMAP is not available. Install or configure it before building a scene." : "Video import needs FFmpeg and FFprobe."
            color: theme.textMuted
        }
        Label { Layout.fillWidth: true; visible: root.actionError.length > 0 && !root.renderable; text: root.actionError; wrapMode: Text.WordWrap; color: theme.textMain }
        RowLayout {
            Layout.fillWidth: true
            visible: root.renderable
            Label { text: "Capture camera"; color: theme.textMain }
            ToneComboBox {
                accessibleLabel: "Capture camera"
                Layout.preferredWidth: 230
                model: viewport.status.cameras || []
                currentIndex: Number(viewport.status.cameraIndex || 0)
                onActivated: viewport.selectCamera(currentIndex)
            }
            ActionButton { text: "Reset camera"; onClicked: viewport.resetCamera() }
            Item { Layout.fillWidth: true }
            Label { text: "Export edge"; color: theme.textMain }
            ToneComboBox { id: exportEdge; objectName: "sceneExportEdge"; accessibleLabel: "Scene export image edge"; model: ["800", "1600", "4096", "6000", "8192"]; currentIndex: 2 }
            ActionButton {
                text: viewport.status.exporting ? "Exporting…" : "Export photo"
                enabled: viewport.status.frameAvailable && !viewport.status.exporting
                onClicked: { root.editPhotoAfterRender = false; photoDestination.open() }
            }
            ActionButton {
                objectName: "sceneEditPhotoButton"
                text: photoEdit.busy ? "Preparing photo…" : "Edit photo…"
                enabled: viewport.status.frameAvailable && !viewport.status.exporting && !photoEdit.busy && Boolean(root.catalog) && !root.catalog.busy
                disabledReason: "Wait for scene rendering and Library operations to finish."
                tooltipText: "Freeze this camera and scene time, save an sRGB photo, add it to Library, and open it in Edit. This creates a rendered image, not a new RAW."
                onClicked: { root.editPhotoAfterRender = true; photoDestination.open() }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.renderable
            Label { text: "Saved views"; color: theme.textMain }
            ToneComboBox {
                objectName: "sceneSavedViews"
                accessibleLabel: "Recall saved camera view"
                Layout.preferredWidth: 280
                model: viewport.status.savedViews || []
                textRole: "name"
                valueRole: "id"
                currentIndex: {
                    const views = viewport.status.savedViews || []
                    for (let i = 0; i < views.length; ++i)
                        if (views[i].id === viewport.status.activeViewId) return i
                    return -1
                }
                displayText: currentIndex >= 0 ? currentText : (count > 0 ? "Recall a saved shot…" : "No saved shots yet")
                enabled: count > 0
                onActivated: {
                    const result = viewport.restoreView(String(currentValue))
                    root.actionError = String(result.error || "")
                }
            }
            ActionButton {
                objectName: "sceneSaveViewButton"
                text: "Save view…"
                enabled: Boolean(viewport.status.frameAvailable)
                onClicked: { root.actionError = ""; saveViewPopup.open() }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.renderable && (!root.spacetimeProject || Number(viewport.status.pathDuration || 0) > 0)
            ActionButton { text: "Camera path…"; enabled: (viewport.status.savedViews || []).length >= 2; onClicked: pathPopup.open() }
            ActionButton {
                text: viewport.status.pathPlaying ? "Pause" : "Play"
                enabled: Number(viewport.status.pathDuration || 0) > 0
                onClicked: viewport.playCameraPath(!viewport.status.pathPlaying)
            }
            ToolValueSlider {
                label: "Camera path position"
                valueText: Number(value).toFixed(1) + " s"
                Layout.fillWidth: true
                from: 0; to: Number(viewport.status.pathDuration || 0)
                value: Number(viewport.status.pathSeconds || 0)
                enabled: to > 0
                onMoved: { viewport.playCameraPath(false); viewport.seekCameraPath(value) }
            }
            Label {
                color: theme.textMuted
                text: viewport.status.filmExporting
                    ? String(viewport.status.filmFramesCompleted || 0) + "/" + String(viewport.status.filmFrameCount || 0) + " frames"
                    : Number(viewport.status.pathSeconds || 0).toFixed(1) + " / " + Number(viewport.status.pathDuration || 0).toFixed(1) + " s"
            }
            ActionButton {
                text: viewport.status.filmExporting ? "Cancel film" : "Export film"
                visible: !root.spacetimeProject
                enabled: Boolean(viewport.status.filmExporting) || (Number(viewport.status.pathDuration || 0) > 0 && Number(exportEdge.currentText) <= 4096)
                disabledReason: Number(exportEdge.currentText) > 4096 ? "Film export supports up to 4096 pixels." : "Save at least two shots and a camera path first."
                onClicked: { if (viewport.status.filmExporting) viewport.cancelFilm(); else filmDestination.open() }
            }
        }
        RowLayout {
            objectName: "sceneTimeControls"
            Layout.fillWidth: true
            visible: root.spacetimeProject && Boolean(viewport.status.temporal)
            Label { text: "Scene time"; color: theme.textMain }
            ActionButton {
                objectName: "sceneTimePlay"
                text: viewport.status.timePlaying ? "Pause time" : "Play time"
                onClicked: viewport.playSceneTime(!viewport.status.timePlaying)
            }
            ToolValueSlider {
                objectName: "sceneTimeSlider"
                label: "Scene time position"
                valueText: Number(value).toFixed(3) + " s"
                Layout.fillWidth: true
                from: 0
                to: Number(viewport.status.timeDurationSeconds || 0)
                value: Number(viewport.status.timePositionSeconds || 0)
                enabled: to > 0
                onMoved: { viewport.playSceneTime(false); viewport.seekSceneTime(value) }
            }
            Label {
                text: Number(viewport.status.timePositionSeconds || 0).toFixed(3) + " / " + Number(viewport.status.timeDurationSeconds || 0).toFixed(3) + " s"
                color: theme.textMuted
            }
            ActionButton { text: "Camera path…"; enabled: (viewport.status.savedViews || []).length >= 2; onClicked: pathPopup.open() }
            ActionButton {
                objectName: "sceneTimedFilmButton"
                text: viewport.status.filmExporting ? "Cancel film" : "Export 4D film…"
                enabled: Boolean(viewport.status.filmExporting) || (Boolean(viewport.status.frameAvailable) && Number(exportEdge.currentText) <= 4096)
                onClicked: { if (viewport.status.filmExporting) viewport.cancelFilm(); else timedFilmPopup.open() }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.spacetimeProject
            wrapMode: Text.WordWrap
            text: viewport.status.filmExporting ? "Rendering film: " + String(viewport.status.filmFramesCompleted || 0) + "/" + String(viewport.status.filmFrameCount || 0) + " frames"
                : "4D artifact · Camera and scene time are independent. Photo export freezes both; film export maps the selected time range to the film length. Artifact validation does not certify reconstruction quality."
            color: theme.textMuted
        }
        Label {
            Layout.fillWidth: true
            visible: root.renderable
            wrapMode: Text.WordWrap
            color: theme.textMuted
            text: root.actionError || photoEdit.errorMessage || String(viewport.status.error || viewport.status.savedViewError || "") || (photoEdit.busy ? photoEdit.statusText : "") || (viewport.status.filmResult
                ? (viewport.status.filmResult.ok ? "Film saved: " + String(viewport.status.filmResult.outputPath)
                    : (viewport.status.filmResult.canceled ? "Film export canceled." : String(viewport.status.filmResult.error || "Film export failed.")))
                : "") || (viewport.status.lastExport
                ? "Photo saved: " + String(viewport.status.lastExport)
                : "Drag to orbit · right-drag to pan · scroll to dolly · double-click to reset. "
                    + (root.spacetimeProject ? "Scene time is independent of the camera. " : "Static 3D scene. ")
                    + "Unobserved surfaces and reconstruction artifacts remain. Exports are sRGB PNG/JPG.")
        }
        Label {
            objectName: "sceneCameraPriorStatus"
            Layout.fillWidth: true
            visible: root.scene.focalPriorFrames !== undefined
            wrapMode: Text.WordWrap
            color: theme.textMuted
            text: String(root.scene.focalPriorFrames || 0) + " views with EXIF focal priors · "
                + String(root.scene.estimatedIntrinsicsFrames || 0) + " with heuristic focal estimates. Refined during reconstruction."
        }
        ActionButton {
            visible: root.scene.busy || root.captureReady || String(root.scene.logTail || "").length > 0
            text: root.showDetails ? "Hide details" : "Show details"
            onClicked: root.showDetails = !root.showDetails
        }
        Label {
            objectName: "sceneTrainingScheduleStatus"
            Layout.fillWidth: true
            visible: root.showDetails && root.scene.trainingSchedule !== undefined
            wrapMode: Text.WordWrap
            color: theme.textMuted
            text: "Refinement every " + String((root.scene.trainingSchedule || {}).refineEvery || 0)
                + " steps · " + String((root.scene.trainingSchedule || {}).settlingSteps || 0)
                + " final steps with splat growth stopped."
                + " Budget: " + String((root.scene.trainingSchedule || {}).maxSplats || 0) + " splats."
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            visible: root.showDetails
            ToneTextArea {
                objectName: "sceneProcessingOutput"
                accessibleLabel: "Scene processing output"
                readOnly: true
                text: String(root.scene.logTail || "No backend output yet.")
                wrapMode: TextEdit.Wrap
                selectByMouse: true
            }
        }
    }
}
