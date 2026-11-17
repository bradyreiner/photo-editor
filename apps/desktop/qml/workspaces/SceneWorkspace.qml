pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import PhotoEditor
import "../components"
import "../components/SceneLooks.js" as SceneLooks
import "../components/SceneCameraNavigation.js" as SceneNavigation

Item {
    id: root
    objectName: "sceneWorkspace"
    property var controller: null
    property var catalog: null
    property alias photoViewport: viewport
    signal photoReadyForEditing(string assetId, bool openEditor)
    property bool editPhotoAfterRender: false
    property bool navigationOrbitMode: false
    property bool navigationPanMode: false
    property bool navigationRollMode: false
    readonly property real navigationDepth: Math.max(0.01, Number(viewport.status.sceneFocusDistance || 1))
    readonly property real navigationMoveStep: Math.max(0.01, Math.min(1000, navigationDepth * 0.05))
    property bool orbitPickArmed: false
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
    readonly property var savedScenes: controller && controller.exampleScenes ? controller.exampleScenes : []
    property string pendingSceneViewId: ""
    function openSavedScene(key: string): void {
        const entry = savedScenes.find(item => item.key === key)
        if (!entry || !controller) return
        pendingSceneViewId = String(entry.viewId || "")
        const result = controller.openProject(key)
        actionError = String(result.error || "")
        if (!result.ok) pendingSceneViewId = ""
        else { showCaptureSetup = false; sceneControls.close() }
    }
    function restoreSavedSceneView(): void {
        if (!pendingSceneViewId.length) return
        const views = viewport.status.savedViews || []
        if (!views.some(view => view.id === pendingSceneViewId)) return
        const id = pendingSceneViewId
        pendingSceneViewId = ""
        actionError = String(viewport.restoreView(id).error || "")
    }
    property string actionError: ""
    property bool showDetails: false
    property bool showCaptureSetup: false
    function fileNameForPath(value) {
        const normalized = String(value || "").replace(/\\/g, "/")
        const fileName = normalized.split("/").pop()
        return fileName.length > 0 ? fileName : normalized
    }
    property alias immersive: sceneWorkspaceSettings.immersive
    property alias previewStatusVisible: sceneWorkspaceSettings.previewStatusVisible
    Settings {
        id: sceneWorkspaceSettings
        objectName: "sceneWorkspaceSettings"
        category: "SceneWorkspace"
        property bool immersive: true
        property bool previewStatusVisible: true
        property bool outlinerVisible: false
        property bool inspectorVisible: true
        property bool shotsVisible: false
        property string inspectorPanel: "camera"
        property real inspectorWidth: 360
        property real permissibleCocMicrons: 30
        property real sensorDraftWidthMm: 36
        property real sensorDraftHeightMm: 24
        property int sensorPresetIndex: 0
        property int studioLayoutVersion: 0
        Component.onCompleted: {
            if (studioLayoutVersion < 1) {
                immersive = false
                studioLayoutVersion = 1
            }
            if (studioLayoutVersion < 2) {
                outlinerVisible = false
                shotsVisible = false
                studioLayoutVersion = 2
            }
            if (studioLayoutVersion < 3) {
                if (inspectorWidth === 360) inspectorWidth = 280
                studioLayoutVersion = 3
            }
            if (studioLayoutVersion < 4) { immersive = false; studioLayoutVersion = 4 }
        }
    }
    function handleLayoutMenuAction(key: string): void {
        if (key === "navigation") immersive = !immersive
        else if (key === "studio") { sceneWorkspaceSettings.outlinerVisible = true; sceneWorkspaceSettings.inspectorVisible = true; sceneWorkspaceSettings.shotsVisible = true }
        else if (key === "viewport") { sceneWorkspaceSettings.outlinerVisible = false; sceneWorkspaceSettings.inspectorVisible = false; sceneWorkspaceSettings.shotsVisible = false }
        else if (key === "outliner") sceneWorkspaceSettings.outlinerVisible = !sceneWorkspaceSettings.outlinerVisible
        else if (key === "inspector") sceneWorkspaceSettings.inspectorVisible = !sceneWorkspaceSettings.inspectorVisible
        else if (key === "shots") sceneWorkspaceSettings.shotsVisible = !sceneWorkspaceSettings.shotsVisible
        else if (key === "status") previewStatusVisible = !previewStatusVisible
    }
    function handleSceneProjectMenuAction(key: string): void {
        if (key === "depth-motion") root.depthMotionActive = true
        else if (key === "create-video") videoPicker.open()
        else if (key === "create-photos") capturePicker.open()
        else if (key === "new-capture") {
            root.showCaptureSetup = root.captureReady
            sceneControls.open()
        } else if (key === "open-scene") reopenPicker.open()
        else if (key === "import-ply") importPlyPicker.open()
        else root.openSavedScene(key)
    }
    function sceneProjectMenuItems(): var {
        const items = []
        if (root.width < theme.scaled(980)) {
            items.push({key: "new-capture", label: root.captureReady ? "New scene capture…" : "Create scene…", icon: "plus",
                enabled: root.controller && !root.scene.busy,
                disabledReason: "Wait for scene processing to finish."})
        }
        items.push(
            {key: "depth-motion", label: "3D Motion", icon: "layers", tooltip: "Animate a photo in 3D."},
            {key: "create-video", label: "Create from video…", icon: "media"},
            {key: "create-photos", label: "Create from photos…", icon: "folder"},
            {key: "open-scene", label: "Open existing scene…", icon: "folder",
                enabled: root.controller && !root.scene.busy,
                disabledReason: "Wait for scene processing to finish."},
            {key: "import-ply", label: "Import Gaussian PLY…", icon: "folder",
                enabled: root.controller && !root.scene.busy,
                disabledReason: "Wait for scene processing to finish."})
        if (root.savedScenes.length > 0) {
            const savedItems = root.savedScenes.map(function(scene, index) {
                return Object.assign({}, scene, {separatorBefore: index === 0})
            })
            return items.concat(savedItems)
        }
        return items
    }
    function sceneToolsMenuItems(): var {
        const items = []
        if (sceneWorkspaceSettings.inspectorVisible) {
            items.push({key: "photo-settings", label: "Photo settings…", icon: "adjust",
                checked: photoControls.visible, enabled: Boolean(viewport.status.frameAvailable),
                disabledReason: "Open a finished scene first."})
        }
        if (root.width <= theme.scaled(1300) || sceneWorkspaceSettings.inspectorVisible) {
            items.push({key: "objects", label: "Scene composition…", icon: "layers",
                enabled: Boolean(viewport.status.frameAvailable),
                disabledReason: "Open a finished scene first."})
        }
        if (root.width <= theme.scaled(1100) || sceneWorkspaceSettings.inspectorVisible) {
            items.push({key: "cleanup", label: "Clean scene & export PLY…", icon: "adjust",
                enabled: Boolean(viewport.status.cleanupAvailable),
                disabledReason: "Open a static scene first."})
        }
        return items
    }
    function handleSceneToolsMenuAction(key: string): void {
        if (key === "photo-settings") photoControls.visible ? photoControls.close() : photoControls.open()
        else if (key === "cleanup") cleanupControls.open()
        else if (key === "objects") compositionControls.open()
    }
    property bool focusPickArmed: false
    property bool depthMotionActive: false
    property bool depthMotionLoaded: false
    property string pendingDepthMotionProject: ""
    function openDepthMotionProject(path: string): var {
        if (depthMotionLoader.item) {
            root.depthMotionActive = true
            return depthMotionLoader.item.openProject(path)
        }
        root.pendingDepthMotionProject = path
        root.depthMotionActive = true
        return {ok: true, pending: true}
    }
    Loader {
        id: depthMotionLoader
        anchors.fill: parent
        z: 100
        visible: root.depthMotionActive
        active: root.depthMotionActive || root.depthMotionLoaded
        source: Qt.resolvedUrl("../components/DepthMotionWorkspace.qml")
        onLoaded: {
            root.depthMotionLoaded = true
            item.sceneHosted = true
            item.catalog = Qt.binding(function() { return root.catalog })
            if (root.pendingDepthMotionProject.length) {
                const path = root.pendingDepthMotionProject
                root.pendingDepthMotionProject = ""
                item.openProject(path)
            }
        }
    }
    Connections {
        target: depthMotionLoader.item
        function onExitRequested() { root.depthMotionActive = false }
        function onLibraryScenesRequested() { root.libraryScenesRequested() }
    }
    onDepthMotionActiveChanged: {
        if (depthMotionActive) {
            viewport.playSceneTime(false)
            viewport.playCameraPath(false)
            sceneControls.close(); photoControls.close(); compositionControls.close(); cleanupControls.close()
        }
    }
    property bool cleanupSelectionArmed: false
    property bool cleanupKeepInside: false
    readonly property bool cleanupLassoMode: cleanupShape.currentIndex === 1
    readonly property bool cleanupDepthLimited: cleanupDepthMode.currentIndex === 1
    readonly property bool cleanupDepthValid: !cleanupDepthLimited || (cleanupNear.acceptableInput && cleanupFar.acceptableInput && Number(cleanupFar.text) > Number(cleanupNear.text))
    readonly property real cleanupNearDepth: cleanupDepthLimited ? Number(cleanupNear.text) : 0
    readonly property real cleanupFarDepth: cleanupDepthLimited ? Number(cleanupFar.text) : Infinity
    Keys.onEscapePressed: event => {
        event.accepted = focusPickArmed || orbitPickArmed || cleanupSelectionArmed
        focusPickArmed = false
        orbitPickArmed = false
        cleanupSelectionArmed = false
        actionError = ""
    }
    onVisibleChanged: { if (!visible) { focusPickArmed = false; orbitPickArmed = false; cleanupSelectionArmed = false } }
    onRenderableChanged: {
        if (!renderable) { cleanupSelectionArmed = false }
        if (renderable && sceneControls) { sceneControls.close(); showCaptureSetup = false }
    }
    signal exitViewerRequested()
    signal libraryScenesRequested()
    function openLibraryScene(entry: var): var {
        if (entry.kind === "depth-motion") return root.openDepthMotionProject(String(entry.projectPath))
        root.depthMotionActive = false
        root.pendingSceneViewId = String(entry.viewId || "")
        const result = root.controller.openProject(String(entry.projectPath))
        root.actionError = String(result.error || "")
        if (!result.ok) root.pendingSceneViewId = ""
        root.restoreSavedSceneView()
        return result
    }
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
        "importing-ply": "Importing Gaussian scene", "canceling": "Stopping safely…", "canceled": "Scene creation stopped", "failed": "Scene needs attention"})[String(scene.state || "idle")] || String(scene.state || "")
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
    onLensSettingsChanged: {
        if (Number(root.lensSettings.apertureRadius || 0) > 0 && !Boolean(root.lensSettings.rayTraced) && photoControls)
            photoControls.rayTracingOptedOut = true
    }
    readonly property bool autoFocusActive: root.lensSettings.focusMode === "auto-centre"
    readonly property var autofocusStatus: viewport.status.autofocus || ({})
    readonly property var toneSettings: root.photoSettings.tone || ({contrast: 1, highlightCompression: 0})
    readonly property var filmSettings: root.photoSettings.film || ({grainAmount: 0, grainSize: 1.5, halationAmount: 0, halationRadius: 4, halationThreshold: 0.8, seed: 1})
    readonly property bool filmPlaneActive: root.filmSettings.schema === "framehouse-film-plane-v2" || root.filmSettings.schema === "framehouse-film-plane-v3"
    readonly property var postSettings: root.photoSettings.post || ({bloomAmount: 0, bloomRadius: 8, glareAmount: 0, glareRadius: 32, glareRays: 4, glareRotation: 0, spectralIntensity: 0, threshold: 0.8, chromaticAberration: 0, saturation: 1})
    readonly property var physicalCamera: viewport.status.physicalCamera || ({})
    readonly property bool physicalCameraActive: Boolean(root.physicalCamera.schema)
    readonly property bool physicalSetupVisible: photoControls.page === 4 && photoControls.advancedVisible
    readonly property real displayedImageWidth: Math.min(viewport.width, viewport.height * Number(viewport.status.imageAspectRatio || 1.6))
    readonly property real displayedImageHeight: displayedImageWidth / Number(viewport.status.imageAspectRatio || 1.6)
    readonly property var cameraOptics: {
        const state = viewport.status
        return root.renderable && state ? viewport.describeCameraOptics(sceneWorkspaceSettings.permissibleCocMicrons) : ({})
    }
    readonly property var sensorGates: [
        {name: "36 × 24 mm", width: 36, height: 24},
        {name: "24 × 36 mm · portrait", width: 24, height: 36},
        {name: "23.5 × 15.6 mm", width: 23.5, height: 15.6},
        {name: "22.3 × 14.9 mm", width: 22.3, height: 14.9},
        {name: "17.3 × 13 mm", width: 17.3, height: 13},
        {name: "44 × 33 mm", width: 44, height: 33}
    ]
    property string observedSensorDimensions: ""
    onPhysicalCameraChanged: {
        if (!physicalCameraActive) return
        const key = String(physicalCamera.sensorWidthMm) + "/" + String(physicalCamera.sensorHeightMm)
        if (key === observedSensorDimensions) return
        observedSensorDimensions = key
        sceneWorkspaceSettings.sensorDraftWidthMm = Number(physicalCamera.sensorWidthMm)
        sceneWorkspaceSettings.sensorDraftHeightMm = Number(physicalCamera.sensorHeightMm)
        let selected = sensorGates.length
        for (let i = 0; i < sensorGates.length; ++i)
            if (Math.abs(sensorGates[i].width - Number(physicalCamera.sensorWidthMm)) < 0.000001
                    && Math.abs(sensorGates[i].height - Number(physicalCamera.sensorHeightMm)) < 0.000001) selected = i
        sceneWorkspaceSettings.sensorPresetIndex = selected
    }
    function updatePhysicalSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.physicalCamera)
        settings[key] = value
        root.actionError = String(viewport.setPhysicalCamera(settings).error || "")
    }
    function setPhysicalFNumber(value: real): void {
        root.updatePhysicalSetting("fNumber", value)
        if (!root.actionError.length && !photoControls.rayTracingOptedOut && !root.lensSettings.rayTraced)
            root.updateLensSettings({rayTraced: true, samples: Math.max(128, Number(root.lensSettings.samples || 32))})
    }
    function rayTracedOpticsEdit(overrides: var): var {
        const edit = Object.assign({}, overrides, {rayTraced: true})
        if (!root.physicalCameraActive && Number(root.lensSettings.apertureRadius || 0) <= 0)
            edit.apertureRadius = root.sceneApertureMaximum * 0.08
        return edit
    }
    readonly property real sceneApertureMaximum: Math.max(0.001, Math.min(
        Number(root.lensSettings.focusDistance || 1) * 0.25,
        Number(viewport.status.sceneFocusDistance || 1) * 0.1))
    readonly property real focusDistanceValue: Number(root.physicalCameraActive ? root.physicalCamera.focusDistanceM : root.lensSettings.focusDistance)
    readonly property real minimumFocusDistance: root.physicalCameraActive
        ? Math.max(0.011 * Number(root.physicalCamera.metresPerSceneUnit), Number(root.physicalCamera.focalLengthMm) * 0.00101,
            Number(root.physicalCamera.focalLengthMm) / (10000 * Number(root.physicalCamera.fNumber)))
        : Math.max(0.01, Number(root.lensSettings.apertureRadius) / 5)
    function setManualFocus(distance: real): void {
        if (root.autoFocusActive) {
            const result = viewport.setFocusMode("manual")
            if (!result.ok) { root.actionError = String(result.error || ""); return }
        }
        if (root.physicalCameraActive) root.updatePhysicalSetting("focusDistanceM", distance)
        else root.updateLensSetting("focusDistance", distance)
    }
    function adjustFocus(direction: int): void {
        const next = Math.max(root.minimumFocusDistance,
            Math.min(100000000, root.focusDistanceValue * Math.pow(1.02, direction)))
        root.setManualFocus(next)
    }
    function adjustAperture(direction: int): void {
        const samples = Math.max(128, Number(root.lensSettings.samples || 32))
        viewport.beginCameraAdjustment()
        if (root.physicalCameraActive) {
            const current = Math.max(0.1, Number(root.physicalCamera.fNumber || 2.8))
            root.setPhysicalFNumber(Math.max(0.1, Math.min(128, current * Math.pow(Math.sqrt(2), -direction))))
        } else {
            const current = Math.max(0, Number(root.lensSettings.apertureRadius || 0))
            const next = direction > 0
                ? (current <= 0 ? root.sceneApertureMaximum * 0.08 : Math.min(root.sceneApertureMaximum, current * Math.sqrt(2)))
                : (current <= root.sceneApertureMaximum * 0.08 ? 0 : current / Math.sqrt(2))
            const edit = {apertureRadius: next}
            if (next > 0 && !photoControls.rayTracingOptedOut) {
                edit.rayTraced = true
                edit.samples = samples
            }
            root.updateLensSettings(edit)
        }
        viewport.endCameraAdjustment()
    }
    function adjustFocalLength(direction: int): void {
        viewport.beginCameraAdjustment()
        if (root.physicalCameraActive) {
            const current = Number(root.physicalCamera.focalLengthMm || 50)
            root.updatePhysicalSetting("focalLengthMm", Math.max(1, Math.min(2000, current * Math.pow(1.1, direction))))
        } else {
            const current = Number(root.photoSettings.focalScale || 1)
            root.updatePhotoSetting("focalScale", Math.max(0.25, Math.min(4, current * Math.pow(1.08, direction))))
        }
        viewport.endCameraAdjustment()
    }
    function adjustExposure(direction: int): void {
        const next = Math.max(-6, Math.min(6, Number(root.photoSettings.exposureEv || 0) + direction * 0.25))
        root.updatePhotoSetting("exposureEv", Math.round(next * 100) / 100)
    }
    function adjustPrimaryBloom(direction: int): void {
        const next = Math.max(0, Math.min(4, Number(root.postSettings.bloomAmount || 0) + direction * 0.1))
        root.updatePrimaryBloom(Math.round(next * 100) / 100)
    }
    function adjustPrimaryHalation(direction: int): void {
        const next = Math.max(0, Math.min(1, Number(root.filmSettings.halationAmount || 0) + direction * 0.05))
        root.updatePrimaryHalation(Math.round(next * 100) / 100)
    }
    function nudgeCamera(horizontal: real, vertical: real): void {
        if (root.navigationPanMode) viewport.pan(horizontal * 0.06, vertical * 0.06)
        else viewport.moveCamera(horizontal * root.navigationMoveStep, vertical * root.navigationMoveStep, 0)
        viewport.forceActiveFocus(Qt.OtherFocusReason)
    }
    function dollyCamera(direction: real): void {
        viewport.moveCamera(0, 0, direction * root.navigationMoveStep * 5)
        viewport.forceActiveFocus(Qt.OtherFocusReason)
    }
    function armOrbitTarget(): void {
        root.orbitPickArmed = !root.orbitPickArmed
        root.focusPickArmed = false
        if (root.orbitPickArmed) {
            root.navigationOrbitMode = true
            root.navigationPanMode = false
            root.navigationRollMode = false
        }
        root.actionError = ""
        viewport.forceActiveFocus(Qt.OtherFocusReason)
    }
    function updatePhotoSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.photoSettings)
        settings[key] = value
        const result = viewport.setPhotoSettings(settings)
        root.actionError = String(result.error || "")
    }
    function configuredLens(overrides: var, clearRayTraced: bool): var {
        const lens = Object.assign({}, root.lensSettings, overrides)
        if (clearRayTraced === true) {
            delete lens.rayTraced
            delete lens.pupilSoftness
        }
        const hasRayTracedFlag = Object.prototype.hasOwnProperty.call(lens, "rayTraced")
        const edge = Number(lens.apertureEdge || 0)
        const distance = Number(lens.vignettingDistance || 0)
        const scale = lens.vignettingScale === undefined ? 1 : Number(lens.vignettingScale)
        if (edge !== 0 || distance !== 0 || scale !== 1) {
            lens.schema = "framehouse-thin-lens-v3"
            lens.apertureEdge = edge
            lens.vignettingDistance = distance
            lens.vignettingScale = scale
        } else {
            delete lens.apertureEdge
            delete lens.vignettingDistance
            delete lens.vignettingScale
            lens.schema = Number(lens.samples) > 128 ? "framehouse-thin-lens-v2" : "framehouse-thin-lens-v1"
        }
        const distortion = Number(lens.sphericalDistortion || 0)
        const astigmatism = Number(lens.astigmatism || 0)
        const curvature = Number(lens.fieldCurvature || 0)
        if (distortion !== 0 || astigmatism !== 0 || curvature !== 0) {
            lens.schema = "framehouse-thin-lens-v4"
            lens.apertureEdge = edge; lens.vignettingDistance = distance; lens.vignettingScale = scale
            lens.sphericalDistortion = distortion; lens.astigmatism = astigmatism; lens.fieldCurvature = curvature
        } else {
            delete lens.sphericalDistortion; delete lens.astigmatism; delete lens.fieldCurvature
        }
        if (overrides.focusDistance !== undefined) lens.focusMode = "manual"
        if (lens.focusMode === "auto-centre") {
            lens.schema = "framehouse-thin-lens-v5"
            lens.apertureEdge = edge; lens.vignettingDistance = distance; lens.vignettingScale = scale
            lens.sphericalDistortion = distortion; lens.astigmatism = astigmatism; lens.fieldCurvature = curvature
        } else delete lens.focusMode
        const aspect = Number(lens.bokehAspect || 1)
        if (aspect !== 1) Object.assign(lens, {schema: "framehouse-thin-lens-v6", bokehAspect: aspect,
            focusMode: root.autoFocusActive && overrides.focusDistance === undefined ? "auto-centre" : "manual",
            apertureEdge: edge, vignettingDistance: distance, vignettingScale: scale,
            sphericalDistortion: distortion, astigmatism: astigmatism, fieldCurvature: curvature})
        else delete lens.bokehAspect
        const hasV7Optics = hasRayTracedFlag || Boolean(lens.rayTraced) || Number(lens.pupilSoftness || 0) > 0
        if (hasV7Optics) {
            Object.assign(lens, {
                schema: "framehouse-thin-lens-v7",
                apertureEdge: edge, vignettingDistance: distance, vignettingScale: scale,
                sphericalDistortion: distortion, astigmatism: astigmatism, fieldCurvature: curvature,
                focusMode: root.autoFocusActive && overrides.focusDistance === undefined ? "auto-centre" : "manual",
                bokehAspect: aspect, rayTraced: Boolean(lens.rayTraced),
                pupilSoftness: Math.max(0, Math.min(4, Number(lens.pupilSoftness || 0)))
            })
        } else {
            delete lens.rayTraced
            delete lens.pupilSoftness
        }
        return lens
    }
    function updateLensSettings(overrides: var): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = settings.tone ? "framehouse-gaussian-photo-settings-v5" : settings.post ? "framehouse-gaussian-photo-settings-v4" : settings.film ? "framehouse-gaussian-photo-settings-v3" : "framehouse-gaussian-photo-settings-v2"
        settings.lens = root.configuredLens(overrides, false)
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function configuredFilm(overrides: var): var {
        const film = Object.assign({schema: "framehouse-creative-film-v1", grainAmount: 0, grainSize: 1.5,
            halationAmount: 0, halationRadius: 4, halationThreshold: 0.8, seed: 1}, root.filmSettings, overrides)
        const plane = film.schema === "framehouse-film-plane-v2" || film.schema === "framehouse-film-plane-v3"
        if (plane) {
            film.schema = film.highlightHalation ? "framehouse-film-plane-v3" : "framehouse-film-plane-v2"
            if (!film.highlightHalation) delete film.highlightHalation
        } else {
            film.schema = "framehouse-creative-film-v1"
            delete film.grainMicrons; delete film.halationMicrons
            delete film.formatWidthMm; delete film.formatHeightMm
            delete film.returnRgb; delete film.highlightHalation
        }
        return film
    }
    function configuredTone(overrides: var): var {
        return Object.assign({warmth: 0, tint: 0, vibrance: 0, fade: 0}, root.toneSettings,
            overrides, {schema: "framehouse-camera-tone-v2"})
    }
    function applyFilmLook(key: string): void {
        const look = SceneLooks.find(SceneLooks.filmLooks, key)
        if (!look) return
        const settings = Object.assign({}, root.photoSettings, {schema: "framehouse-gaussian-photo-settings-v5"})
        settings.lens = configuredLens({}, false)
        settings.look = key === "mono" ? "monochrome" : "neutral"
        settings.film = Object.assign({}, root.filmSettings, {schema: "framehouse-film-plane-v3",
            grainAmount: look.grain, grainSize: Number(root.filmSettings.grainSize || 1.5), grainMicrons: look.scale,
            halationAmount: look.halo, halationRadius: Number(root.filmSettings.halationRadius || 4),
            halationMicrons: look.diffusion, halationThreshold: look.threshold, highlightHalation: true,
            formatWidthMm: Number(root.filmSettings.formatWidthMm || 36), formatHeightMm: Number(root.filmSettings.formatHeightMm || 24),
            returnRgb: [1, 0.15, 0.025], seed: Number(root.filmSettings.seed || 1)})
        settings.post = Object.assign({}, root.postSettings, {schema: "framehouse-camera-post-v1", saturation: look.saturation})
        settings.tone = configuredTone({warmth: look.warmth, tint: look.tint, vibrance: look.vibrance,
            fade: look.fade, contrast: look.contrast, highlightCompression: look.shoulder})
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function applyLensLook(key: string): void {
        const look = SceneLooks.find(SceneLooks.lensLooks, key)
        if (!look) return
        const settings = Object.assign({}, root.photoSettings, {schema: "framehouse-gaussian-photo-settings-v5"})
        const lensOverrides = {bokehAspect: look.aspect, blades: look.blades, roundness: look.roundness,
            rotationDegrees: 0, apertureEdge: look.edge, vignettingDistance: 0, vignettingScale: 1,
            sphericalDistortion: look.distortion, astigmatism: look.astigmatism, fieldCurvature: look.curvature}
        settings.lens = configuredLens(lensOverrides, false)
        settings.film = configuredFilm({})
        settings.tone = configuredTone({})
        settings.post = Object.assign({}, root.postSettings, {schema: "framehouse-camera-post-v1",
            bloomAmount: look.bloom, bloomRadius: look.spread, glareAmount: look.flare, glareRays: look.rays,
            glareRadius: look.length, glareRotation: 0, chromaticAberration: look.fringe, spectralIntensity: 0, threshold: 0.35})
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function resetColor(): void {
        const settings = Object.assign({}, root.photoSettings, {schema: "framehouse-gaussian-photo-settings-v5", look: "neutral"})
        settings.lens = configuredLens({}, false); settings.film = configuredFilm({})
        settings.post = Object.assign({}, root.postSettings, {schema: "framehouse-camera-post-v1", saturation: 1})
        settings.tone = configuredTone({contrast: 1, highlightCompression: 0, warmth: 0, tint: 0, vibrance: 0, fade: 0})
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function updateLensSetting(key: string, value: var): void {
        const edit = {}; edit[key] = value
        updateLensSettings(edit)
    }
    function updatePrimaryBloom(value: real): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = settings.tone ? "framehouse-gaussian-photo-settings-v5" : "framehouse-gaussian-photo-settings-v4"
        settings.lens = root.configuredLens({}, false)
        settings.film = configuredFilm({})
        settings.post = Object.assign({}, root.postSettings, {schema: "framehouse-camera-post-v1", bloomAmount: value})
        const savedPost = root.photoSettings.post || ({})
        if (Number(root.postSettings.bloomAmount || 0) <= 0 && value > 0
                && !Object.prototype.hasOwnProperty.call(savedPost, "threshold"))
            settings.post.threshold = 0.25
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function updatePrimaryHalation(value: real): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = settings.tone ? "framehouse-gaussian-photo-settings-v5" : settings.post ? "framehouse-gaussian-photo-settings-v4" : "framehouse-gaussian-photo-settings-v3"
        settings.lens = root.configuredLens({}, false)
        const savedFilm = root.photoSettings.film || ({})
        settings.film = configuredFilm({halationAmount: value})
        if (Number(root.filmSettings.halationAmount || 0) <= 0 && value > 0
                && !Object.prototype.hasOwnProperty.call(savedFilm, "halationThreshold"))
            settings.film.halationThreshold = 0.3
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function updateFilmSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = settings.tone ? "framehouse-gaussian-photo-settings-v5" : settings.post ? "framehouse-gaussian-photo-settings-v4" : "framehouse-gaussian-photo-settings-v3"
        settings.lens = root.configuredLens({}, false)
        const overrides = {}; overrides[key] = value
        settings.film = configuredFilm(overrides)
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function setFilmPlaneModel(enabled: bool): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = settings.tone ? "framehouse-gaussian-photo-settings-v5" : settings.post ? "framehouse-gaussian-photo-settings-v4" : "framehouse-gaussian-photo-settings-v3"
        settings.lens = root.configuredLens({}, false)
        const f = root.filmSettings
        settings.film = {schema: "framehouse-creative-film-v1", grainAmount: f.grainAmount, grainSize: f.grainSize,
            halationAmount: f.halationAmount, halationRadius: f.halationRadius, halationThreshold: f.halationThreshold, seed: f.seed}
        if (enabled) Object.assign(settings.film, {schema: "framehouse-film-plane-v2",
            grainMicrons: 12, halationMicrons: 80, formatWidthMm: 36, formatHeightMm: 24, returnRgb: [1, 0.15, 0.025]})
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function updatePostSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = settings.tone ? "framehouse-gaussian-photo-settings-v5" : "framehouse-gaussian-photo-settings-v4"
        settings.lens = root.configuredLens({}, false)
        settings.film = configuredFilm({})
        settings.post = Object.assign({}, root.postSettings, {schema: "framehouse-camera-post-v1"})
        settings.post[key] = value
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    function updateToneSetting(key: string, value: var): void {
        const settings = Object.assign({}, root.photoSettings)
        settings.schema = "framehouse-gaussian-photo-settings-v5"
        settings.lens = root.configuredLens({}, false)
        settings.film = configuredFilm({})
        settings.post = Object.assign({}, root.postSettings, {schema: "framehouse-camera-post-v1"})
        const overrides = {}; overrides[key] = value
        settings.tone = configuredTone(overrides)
        root.actionError = String(viewport.setPhotoSettings(settings).error || "")
    }
    property url sequenceParentFolder
    function exportTimedFilmTo(destination: string): var { return root.exportTimedOutputTo(destination, false) }
    function exportTimedOutputTo(destination: string, sequence: bool): var {
        if (!filmSceneStart.acceptableInput || !filmSceneEnd.acceptableInput || !timedFilmDuration.acceptableInput) {
            root.actionError = "Enter valid scene times and film length."
            return {ok: false, error: root.actionError}
        }
        const settings = {
            startSeconds: Number(filmSceneStart.text), endSeconds: Number(filmSceneEnd.text),
            durationSeconds: Number(timedFilmDuration.text), fps: Number(timedFilmFps.currentText),
            cameraMotion: timedFilmCamera.currentIndex === 1 ? "saved-path" : "current-camera"
        }
        const result = sequence ? viewport.exportTimedFrameSequence(destination, settings, Number(exportEdge.currentText))
            : viewport.exportTimedFilm(destination, settings, Number(exportEdge.currentText))
        root.actionError = String(result.error || "")
        if (result.ok) timedFilmPopup.close()
        return result
    }
    function exportSequenceTo(destination: string): var {
        const result = root.spacetimeProject ? root.exportTimedOutputTo(destination, true)
            : viewport.exportFrameSequence(destination, Number(exportEdge.currentText))
        root.actionError = String(result.error || "")
        if (result.ok) filmExportOptions.close()
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
            if (result.ok) filmExportOptions.close()
        }
    }
    FolderDialog {
        id: sequenceParentPicker
        title: "Choose a parent folder for the frame sequence"
        onAccepted: root.sequenceParentFolder = selectedFolder
    }
    AppPopup {
        id: filmExportOptions
        objectName: "sceneFilmExportOptions"
        anchors.centerIn: Overlay.overlay
        width: Math.min(popupAvailableWidth, 440)
        height: Math.min(popupAvailableHeight, filmExportContents.implicitHeight + topPadding + bottomPadding)
        padding: theme.space16
        readonly property string folderName: sequenceName.text.trim()
        readonly property bool validName: folderName.length > 0 && folderName !== "." && folderName !== ".."
            && folderName.indexOf("/") < 0 && folderName.indexOf(String.fromCharCode(92)) < 0
        contentItem: Flickable {
            implicitHeight: filmExportContents.implicitHeight
            contentHeight: filmExportContents.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: filmExportContents
                width: parent.width; spacing: theme.space12
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Export film"; color: theme.textMain; Layout.fillWidth: true }
                    ActionButton { text: "Done"; onClicked: filmExportOptions.close() }
                }
                ToneComboBox {
                    id: filmFormat; objectName: "sceneFilmFormatCombo"; Layout.fillWidth: true
                    accessibleLabel: "Film output format"
                    model: ["MP4 video", "PNG frame sequence"]
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: filmFormat.currentIndex === 1
                        ? "Preserve the rendered 8-bit sRGB pixels in numbered PNGs, with frame rate and camera settings saved alongside. Choose a parent folder and a name for the new sequence folder."
                        : "Compressed MP4 for playback and sharing. Film export supports up to 4096 pixels on the long edge."
                }
                ColumnLayout {
                    visible: filmFormat.currentIndex === 1; Layout.fillWidth: true; spacing: theme.space12
                    ActionButton { objectName: "sceneSequenceParentButton"; text: "Choose parent folder…"; onClicked: sequenceParentPicker.open() }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: theme.textMuted
                        text: String(root.sequenceParentFolder).length ? decodeURIComponent(String(root.sequenceParentFolder).replace(/^file:\/\//, "")) : "No parent folder selected"
                    }
                    Label { text: "New folder name"; color: theme.textMain }
                    ToneTextField {
                        id: sequenceName; objectName: "sceneSequenceFolderName"; Layout.fillWidth: true
                        text: "Scene frames"; maximumLength: 80
                        Accessible.name: "New frame sequence folder name"
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                        text: String(exportEdge.currentText) + " px long edge · Lossless PNG · A new folder is required"
                    }
                }
                ActionButton {
                    objectName: "sceneFilmDestinationButton"
                    text: filmFormat.currentIndex === 1 ? "Render PNG sequence" : "Choose MP4 destination…"
                    tone: "primary"; Layout.alignment: Qt.AlignRight
                    enabled: !viewport.status.filmExporting && (filmFormat.currentIndex === 1
                        ? filmExportOptions.validName && String(root.sequenceParentFolder).length > 0 && Number(exportEdge.currentText) <= 8192
                        : Number(exportEdge.currentText) <= 4096)
                    disabledReason: filmFormat.currentIndex === 1 ? "Choose a parent folder and a valid new folder name." : "Choose an export edge of 4096 pixels or less for MP4."
                    onClicked: {
                        if (filmFormat.currentIndex === 1)
                            root.exportSequenceTo(String(root.sequenceParentFolder).replace(/\/+$/, "") + "/" + encodeURIComponent(filmExportOptions.folderName))
                        else filmDestination.open()
                    }
                }
            }
        }
        background: PopupSurface {}
    }
    AppPopup {
        id: timedFilmPopup
        objectName: "sceneTimedFilmPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(520), root.width - theme.space24)))
        modal: true
        padding: standardPopupPadding
        background: PopupSurface {}
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
                ToneTextField {
                    numericEntry: true
                    id: filmSceneStart; objectName: "sceneFilmTimeStart"; Layout.fillWidth: true; placeholderText: "Start"; validator: DoubleValidator { bottom: 0; top: Number(viewport.status.timeDurationSeconds || 0); locale: "C" } }
                Label { text: "→"; color: theme.textMuted }
                ToneTextField {
                    numericEntry: true
                    id: filmSceneEnd; objectName: "sceneFilmTimeEnd"; Layout.fillWidth: true; placeholderText: "End"; validator: DoubleValidator { bottom: 0; top: Number(viewport.status.timeDurationSeconds || 0); locale: "C" } }
            }
            Label { Layout.fillWidth: true; text: "Reverse the range to reverse motion. Equal times freeze the scene."; wrapMode: Text.WordWrap; color: theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Film length"; color: theme.textMain }
                ToneTextField {
                    numericEntry: true
                    id: timedFilmDuration; objectName: "sceneTimedFilmDuration"; Layout.preferredWidth: 90; validator: DoubleValidator { bottom: 0.5; top: 600; locale: "C" } }
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
                    onClicked: filmExportOptions.open()
                }
            }
        }
    }
    AppPopup {
        id: pathPopup
        objectName: "sceneCameraPathPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(520), root.width - theme.space24)))
        modal: true
        padding: standardPopupPadding
        background: PopupSurface {}
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
    SceneSaveDialog {
        id: saveSceneDialog
        projectPath: String(root.scene.projectPath || "")
        defaultName: root.fileNameForPath(projectPath) || "Untitled scene"
        sceneViewport: viewport
        beforeSave: function(name) { return viewport.saveView(name) }
    }
    SceneStudioLayout {
        id: studioLayout
        visible: !root.depthMotionActive
        anchors.fill: parent
        active: true
        viewport: root.photoViewport
        cameraPanel: photoControls
        compositionPanel: compositionControls
        workspaceSettings: sceneWorkspaceSettings
    }
    AppPopup {
        id: saveViewPopup
        objectName: "sceneSaveViewPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(360), root.width - theme.space24)))
        modal: true
        padding: standardPopupPadding
        onOpened: { viewName.clear(); viewName.forceActiveFocus() }
        background: PopupSurface {}
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
        id: importPlyPicker
        title: "Import a trained Gaussian PLY"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Gaussian scene (*.ply *.PLY)"]
        onAccepted: {
            const result = root.controller.importPly(selectedFile.toString(), root.controller.suggestedProjectPath(selectedFile.toString()))
            root.actionError = String(result.error || "")
        }
    }
    FileDialog {
        id: meshPicker
        objectName: "sceneMeshPicker"
        title: "Import polygon geometry into this scene"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Polygon geometry (*.obj *.OBJ)"]
        onAccepted: compositionControls.errorMessage = String(viewport.importSceneMesh(selectedFile.toString(), compositionControls.preserveMeshCoordinates, compositionControls.importMaterials).error || "")
    }
    SceneCompositionPanel {
        id: compositionControls
        viewport: root.photoViewport
        parent: studioLayout.inspectorHost
        anchors.fill: parent
        studioHost: studioLayout.inspectorHost
        panelKey: "composition"
        onImportMeshRequested: meshPicker.open()
    }
    FileDialog {
        id: exportSplatPicker
        title: "Export cleaned Gaussian scene"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Gaussian scene (*.ply)"]
        defaultSuffix: "ply"
        onAccepted: root.actionError = String(viewport.exportSplat(selectedFile.toString()).error || "")
    }
    SceneInspectorPanel {
        id: cleanupControls
        parent: studioLayout.inspectorHost
        anchors.fill: parent
        studioHost: studioLayout.inspectorHost
        panelKey: "cleanup"
        objectName: "sceneCleanupPopup"
        padding: theme.space16
        contentItem: Flickable {
            clip: true; contentWidth: width; contentHeight: cleanupContents.implicitHeight
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: cleanupContents
                width: parent.width; spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Clean scene"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "Done"; onClicked: cleanupControls.close() }
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMain
                    text: Number(viewport.status.pointCount || 0).toLocaleString(Qt.locale(), "f", 0) + " of " + Number(viewport.status.originalPointCount || 0).toLocaleString(Qt.locale(), "f", 0) + " splats visible"
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: "Draw around the area to remove or keep. Selection uses splat centers. Edits save automatically; your original PLY stays intact."
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Selection shape"; color: theme.textMain }
                    ToneComboBox {
                        id: cleanupShape
                        objectName: "sceneCleanupShape"
                        accessibleLabel: "Cleanup selection shape"
                        model: ["Rectangle", "Lasso"]
                        enabled: !viewport.status.cleanupBusy && !viewport.status.exporting && !viewport.status.filmExporting
                        Layout.fillWidth: true
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Selection depth"; color: theme.textMain }
                    ToneComboBox {
                        id: cleanupDepthMode
                        objectName: "sceneCleanupDepthMode"
                        accessibleLabel: "Cleanup selection depth"
                        model: ["All depths", "Depth range"]
                        Layout.fillWidth: true
                        enabled: !viewport.status.cleanupBusy && !viewport.status.exporting && !viewport.status.filmExporting
                    }
                }
                RowLayout {
                    visible: root.cleanupDepthLimited
                    Layout.fillWidth: true
                    Label { text: "Near"; color: theme.textMain }
                    ToneTextField {
                        id: cleanupNear; objectName: "sceneCleanupNearDepth"
                        numericEntry: true; text: "0"; Layout.fillWidth: true
                        Accessible.name: "Near cleanup depth in scene units"
                        validator: DoubleValidator { bottom: 0; top: 1000000000000; locale: "C" }
                    }
                    Label { text: "Far"; color: theme.textMain }
                    ToneTextField {
                        id: cleanupFar; objectName: "sceneCleanupFarDepth"
                        numericEntry: true; text: "10"; Layout.fillWidth: true
                        Accessible.name: "Far cleanup depth in scene units"
                        validator: DoubleValidator { bottom: 0; top: 1000000000000; locale: "C" }
                    }
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: root.cleanupDepthLimited
                        ? (root.cleanupDepthValid ? "Depth is forward distance from the current camera, in scene units. Remove affects the outlined volume; Keep retains only that volume."
                            : "Enter a nonnegative near depth and a larger far depth.")
                        : "The outline includes centers at every depth."
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        objectName: "sceneCleanupRemoveButton"; text: root.cleanupLassoMode ? "Remove lasso" : "Remove rectangle"
                        enabled: Boolean(viewport.status.cleanupAvailable) && root.cleanupDepthValid && !viewport.status.cleanupBusy && !viewport.status.exporting && !viewport.status.filmExporting
                        onClicked: { viewport.playCameraPath(false); root.cleanupKeepInside = false; root.cleanupSelectionArmed = true; root.focusPickArmed = false; root.forceActiveFocus() }
                    }
                    ActionButton {
                        objectName: "sceneCleanupKeepButton"; text: root.cleanupLassoMode ? "Keep lasso" : "Keep rectangle"
                        enabled: Boolean(viewport.status.cleanupAvailable) && root.cleanupDepthValid && !viewport.status.cleanupBusy && !viewport.status.exporting && !viewport.status.filmExporting
                        onClicked: { viewport.playCameraPath(false); root.cleanupKeepInside = true; root.cleanupSelectionArmed = true; root.focusPickArmed = false; root.forceActiveFocus() }
                    }
                }
                Label { text: "Remove opacity below " + Number(cleanupOpacity.value).toFixed(2); color: theme.textMain }
                ToolValueSlider {
                    id: cleanupOpacity; objectName: "sceneCleanupOpacitySlider"; Layout.fillWidth: true
                    label: "Minimum splat opacity"; valueText: Number(value).toFixed(2)
                    from: 0; to: 1; value: 0.02; stepSize: 0.01
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Maximum RMS radius"; color: theme.textMain }
                    ToneTextField {
                        id: cleanupRadius; objectName: "sceneCleanupRadiusField"; Layout.fillWidth: true
                        text: "0"; numericEntry: true
                        validator: DoubleValidator { bottom: 0; top: 100000000; locale: "C" }
                        Accessible.name: "Maximum RMS splat radius in scene units; zero disables the size filter"
                    }
                }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted; text: "Radius uses scene units. Set 0 to keep all sizes. Filtering can remove fine detail; undo to compare." }
                ActionButton {
                    objectName: "sceneCleanupFilterButton"; text: "Apply opacity and size filter"
                    enabled: Boolean(viewport.status.cleanupAvailable) && !viewport.status.cleanupBusy && cleanupRadius.acceptableInput && !viewport.status.exporting && !viewport.status.filmExporting
                    onClicked: root.actionError = String(viewport.cleanupFilter(cleanupOpacity.value, Number(cleanupRadius.text)).error || "")
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton { objectName: "sceneCleanupUndoButton"; text: "Undo"; enabled: Boolean(viewport.status.cleanupCanUndo); onClicked: root.actionError = String(viewport.undoCleanup().error || "") }
                    ActionButton { objectName: "sceneCleanupRedoButton"; text: "Redo"; enabled: Boolean(viewport.status.cleanupCanRedo); onClicked: root.actionError = String(viewport.redoCleanup().error || "") }
                    ActionButton { objectName: "sceneCleanupResetButton"; text: "Restore all"; enabled: Boolean(viewport.status.cleanupAvailable) && !viewport.status.cleanupBusy; onClicked: root.actionError = String(viewport.resetCleanup().error || "") }
                }
                ActionButton {
                    objectName: "sceneExportSplatButton"; text: "Export cleaned PLY…"; tone: "primary"
                    enabled: Boolean(viewport.status.cleanupAvailable) && !viewport.status.cleanupBusy
                    onClicked: exportSplatPicker.open()
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: theme.textMain
                    text: viewport.status.cleanupBusy ? "Saving scene…" : root.actionError || String((viewport.status.cleanupResult || {}).error || (viewport.status.cleanupResult || {}).outputPath || "")
                    visible: text.length > 0
                }
            }
        }
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
            parent: studioLayout.viewportHost
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
                activeFocusOnTab: true
                onStatusChanged: root.restoreSavedSceneView()
                onSourceChanged: { root.focusPickArmed = false; root.orbitPickArmed = false; root.cleanupSelectionArmed = false }
                Keys.onPressed: event => SceneNavigation.handleKey(event, viewport, root)
                SceneCameraInput {
                    navigation: root
                    viewport: root.photoViewport
                }
                Rectangle {
                    objectName: "sceneOrbitTargetMarker"
                    property var targetView: viewport.status.orbitTargetView || ({})
                    visible: Boolean(viewport.status.orbitTargetView) && !root.orbitPickArmed
                    x: (parent.width - root.displayedImageWidth) / 2 + Number(targetView[0] || 0) * root.displayedImageWidth - width / 2
                    y: (parent.height - root.displayedImageHeight) / 2 + Number(targetView[1] || 0) * root.displayedImageHeight - height / 2
                    width: 20; height: 20; radius: width / 2
                    color: "transparent"; border.width: 1.5; border.color: theme.textStrong
                    Rectangle { anchors.centerIn: parent; width: 4; height: 4; radius: 2; color: theme.textStrong }
                }
                SceneCameraControls {
                    navigation: root
                    viewport: root.photoViewport
                }
                Rectangle {
                    objectName: "sceneAutofocusTarget"
                    anchors.centerIn: parent
                    width: 32; height: 32; radius: 4
                    color: "transparent"; border.width: 1; border.color: theme.textStrong
                    visible: root.autoFocusActive && !root.cleanupSelectionArmed && Boolean(viewport.status.frameAvailable)
                    opacity: 0.7
                    Label { anchors.bottom: parent.top; anchors.horizontalCenter: parent.horizontalCenter; text: "AF"; color: theme.textStrong; font.pixelSize: theme.typeCaption }
                }
                MouseArea {
                    id: cleanupMarquee
                    objectName: "sceneCleanupMarquee"
                    anchors.fill: parent
                    enabled: root.cleanupSelectionArmed
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.CrossCursor
                    property point start: Qt.point(0, 0)
                    property point end: Qt.point(0, 0)
                    property var outline: []
                    property real sampleSpacing: 3
                    readonly property real imageWidth: Math.min(width, height * Number(viewport.status.imageAspectRatio || 1.6))
                    readonly property real imageHeight: imageWidth / Number(viewport.status.imageAspectRatio || 1.6)
                    readonly property real imageX: (width - imageWidth) / 2
                    readonly property real imageY: (height - imageHeight) / 2
                    function boundedPoint(x: real, y: real): point {
                        return Qt.point(Math.max(imageX, Math.min(imageX + imageWidth, x)), Math.max(imageY, Math.min(imageY + imageHeight, y)))
                    }
                    function appendOutline(point: point, force: bool): void {
                        const previous = outline[outline.length - 1]
                        if (!force && previous && Math.hypot(previous.x-point.x, previous.y-point.y) < sampleSpacing) return
                        let next = outline.slice()
                        if (next.length >= 512) {
                            next = next.filter((value, index) => index % 2 === 0)
                            sampleSpacing *= 2
                        }
                        // QML point properties expose live value-type wrappers.
                        // Snapshot numbers so earlier vertices do not follow end.
                        next.push({x: Number(point.x), y: Number(point.y)})
                        outline = next
                        cleanupOutline.requestPaint()
                    }
                    onPressed: mouse => {
                        if (mouse.button === Qt.RightButton) { root.cleanupSelectionArmed = false; cleanupControls.open(); return }
                        start = boundedPoint(mouse.x, mouse.y); end = start
                        outline = []; sampleSpacing = 3
                        if (root.cleanupLassoMode) appendOutline(start, true)
                    }
                    onPositionChanged: mouse => {
                        if (pressed) {
                            end = boundedPoint(mouse.x, mouse.y)
                            if (root.cleanupLassoMode) appendOutline(end, false)
                        }
                    }
                    onCanceled: { outline = []; root.cleanupSelectionArmed = false }
                    onWidthChanged: if (pressed) root.cleanupSelectionArmed = false
                    onHeightChanged: if (pressed) root.cleanupSelectionArmed = false
                    onReleased: mouse => {
                        if (!root.cleanupSelectionArmed || mouse.button !== Qt.LeftButton) return
                        end = boundedPoint(mouse.x, mouse.y)
                        const left = Math.min(start.x, end.x), top = Math.min(start.y, end.y)
                        const w = Math.abs(start.x - end.x), h = Math.abs(start.y - end.y)
                        if (root.cleanupLassoMode) {
                            appendOutline(end, true)
                            const points = outline.map(point => ({x: (point.x-cleanupMarquee.imageX)/cleanupMarquee.imageWidth, y: (point.y-cleanupMarquee.imageY)/cleanupMarquee.imageHeight}))
                            root.cleanupSelectionArmed = false
                            if (points.length >= 3) {
                                root.actionError = String(viewport.cleanupLasso(points,root.cleanupKeepInside,root.cleanupNearDepth,root.cleanupFarDepth).error || "")
                            }
                        } else {
                            root.cleanupSelectionArmed = false
                            if (w >= 3 && h >= 3) root.actionError = String(viewport.cleanupSelection((left - imageX) / imageWidth, (top - imageY) / imageHeight, w / imageWidth, h / imageHeight, root.cleanupKeepInside,root.cleanupNearDepth,root.cleanupFarDepth).error || "")
                        }
                        cleanupControls.open()
                    }
                    Rectangle {
                        visible: cleanupMarquee.pressed && root.cleanupSelectionArmed && !root.cleanupLassoMode
                        x: Math.min(cleanupMarquee.start.x, cleanupMarquee.end.x); y: Math.min(cleanupMarquee.start.y, cleanupMarquee.end.y)
                        width: Math.abs(cleanupMarquee.end.x - cleanupMarquee.start.x); height: Math.abs(cleanupMarquee.end.y - cleanupMarquee.start.y)
                        color: theme.alpha(theme.textStrong, 0.12); border.color: theme.textStrong; border.width: 1
                    }
                    Canvas {
                        id: cleanupOutline
                        objectName: "sceneCleanupLassoOutline"
                        anchors.fill: parent
                        visible: cleanupMarquee.pressed && root.cleanupSelectionArmed && root.cleanupLassoMode
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const points = cleanupMarquee.outline
                            if (points.length < 2) return
                            ctx.beginPath(); ctx.moveTo(points[0].x,points[0].y)
                            for (let i = 1; i < points.length; ++i) ctx.lineTo(points[i].x,points[i].y)
                            ctx.closePath(); ctx.fillRule = Qt.OddEvenFill
                            ctx.fillStyle = theme.alpha(theme.textStrong,0.12); ctx.fill()
                            ctx.strokeStyle = theme.textStrong; ctx.lineWidth = 1; ctx.stroke()
                        }
                    }
                }
            }
            ColumnLayout {
                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: theme.space12
                width: Math.min(620, parent.width - theme.space24)
                visible: !viewport.status.frameAvailable
                spacing: theme.space6
                ProgressStrip { Layout.fillWidth: true; visible: Boolean(root.scene.busy); progress: -1 }
                Label {
                    objectName: "sceneCaptureStatus"; Layout.fillWidth: true
                    text: root.actionError || String(viewport.status.error || root.scene.error || root.scene.persistenceError || "") ||
                        (root.scene.busy ? root.stageLabel : "Load a scene from Library or the toolbar.")
                    color: theme.textMuted; font.pixelSize: theme.typeCaption; wrapMode: Text.WordWrap
                }
                Label {
                    objectName: "sceneCaptureCounts"; visible: Number(root.scene.sourceCount || 0) > 0
                    text: String(root.scene.sourceCount || 0) + " captures · " + String(root.scene.registeredViews || 0) + " registered views"
                    color: theme.textMuted; font.pixelSize: theme.typeCaption
                }
            }
        }
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: false
        height: 100
        gradient: Gradient {
            GradientStop { position: 0; color: "#b3000000" }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    AppPopup {
        id: sceneControls
        objectName: "sceneControlsPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(880, root.width - 48)))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(sceneControlContents.implicitHeight + padding * 2, root.height - 48)))
        padding: standardPopupPadding
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: PopupSurface {}
        contentItem: Flickable {
            objectName: "sceneCaptureScroll"
            clip: true
            contentWidth: width
            contentHeight: sceneControlContents.implicitHeight
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: sceneControlContents
                width: sceneControls.availableWidth
                spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Label { text: root.captureReady && !root.showCaptureSetup ? "Your scene" : "Scene setup"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
                        Label {
                            visible: !root.captureReady || root.showCaptureSetup
                            text: "Choose source media and a destination. Everything is built locally."
                            color: theme.textMuted
                            font.pixelSize: 13
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        visible: root.captureReady
                        text: root.showCaptureSetup ? "Hide capture setup" : "New capture"
                        onClicked: root.showCaptureSetup = !root.showCaptureSetup
                    }
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
                ToneCheckBox {
                    id: photoSameCamera
                    objectName: "scenePhotoSameCamera"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: (!root.captureReady || root.showCaptureSetup) && !root.videoInput && !root.timedInput && root.advancedCapture
                    text: "Same camera and zoom · Experimental"
                    description: "Share estimated lens calibration. Use only photos from one camera and lens with unchanged zoom, crop and stabilization. Conflicting metadata or dimensions are rejected."
                    checked: false
                    enabled: !root.scene.busy
                }
                ToneCheckBox {
                    id: photoStrictMatching
                    objectName: "scenePhotoStrictMatching"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: (!root.captureReady || root.showCaptureSetup) && !root.videoInput && !root.timedInput && root.advancedCapture
                    text: "Reject ambiguous matches · Experimental"
                    description: "Use stricter feature matching for repeated textures. May connect fewer photos; review alignment coverage before building a partial scene."
                    checked: false
                    enabled: !root.scene.busy
                }
                ToneCheckBox {
                    id: videoPreferSharpFrames
                    objectName: "sceneVideoPreferSharpFrames"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: (!root.captureReady || root.showCaptureSetup) && root.videoInput
                    text: "Prefer sharper frames · Experimental"
                    description: "Select nearby frames with higher edge contrast. Adds processing time and can reduce alignment coverage; review the connected-view result."
                    checked: false
                    enabled: !root.scene.busy
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
                                : root.videoInput ? root.controller.startVideo(capturePath.text, projectPath.text, iterations.value, Number(resolution.currentText), splatBudget.maximumSplats, videoFrames.value, videoPreferSharpFrames.checked)
                                : root.controller.startPhotoCapture(capturePath.text, projectPath.text, iterations.value, Number(resolution.currentText), splatBudget.maximumSplats, photoSameCamera.checked, photoStrictMatching.checked)
                            root.actionError = String(result.error || "")
                            if (result.ok) { root.showCaptureSetup = false; sceneControls.close() }
                        }
                    }
                }
                Label {
                    objectName: "sceneTrainingResourceHint"
                    Layout.fillWidth: true
                    visible: (!root.captureReady || root.showCaptureSetup) &&
                             !root.timedInput && root.showDetails
                    wrapMode: Text.WordWrap
                    color: theme.textMuted
                    text: String(iterations.value) + " steps · " + resolution.currentText + " px · up to " + splatBudget.currentText + " splats. "
                        + "Explore gets you into a first scene sooner; Detail takes substantially longer. Neither guarantees sharp geometry. Originals remain untouched."
                }
                Label {
                    objectName: "sceneCaptureQualityGuidance"
                    Layout.fillWidth: true
                    visible: (!root.captureReady || root.showCaptureSetup) && !root.timedInput
                    wrapMode: Text.WordWrap; color: theme.textMain
                    text: "For a clean scan: use a fast shutter (around 1/800 s when light allows), lock exposure and white balance, and turn off electronic stabilization. Move through the space at low, eye-level and high viewpoints with overlap; include the floor and ceiling. Avoid moving people and changing shadows."
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
                    objectName: "sceneTimeControls"
                    Layout.fillWidth: true
                    visible: !root.showCaptureSetup && root.spacetimeProject && Boolean(viewport.status.temporal)
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
                        enabled: Boolean(viewport.status.filmExporting) || (Boolean(viewport.status.frameAvailable) && Number(exportEdge.currentText) <= 8192)
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
                        ? (viewport.status.filmResult.ok ? (viewport.status.filmResult.outputFormat === "png-sequence" ? "Frames saved: " : "Film saved: ") + String(viewport.status.filmResult.outputPath)
                            : (viewport.status.filmResult.canceled ? "Film export canceled." : String(viewport.status.filmResult.error || "Film export failed.")))
                        : "") || (viewport.status.lastExport
                        ? "Photo saved: " + String(viewport.status.lastExport)
                        : "Drag to look · WASD to move · Q/E up/down · Alt-drag to roll · double-click a surface to orbit. "
                            + (root.spacetimeProject ? "Scene time is independent of the camera. " : "Static 3D scene. ")
                            + "Unobserved surfaces and reconstruction artifacts remain. Exports are sRGB PNG/JPG.")
                }
                Label {
                    objectName: "sceneCameraPriorStatus"
                    Layout.fillWidth: true
                    visible: root.showDetails && root.scene.focalPriorFrames !== undefined
                    wrapMode: Text.WordWrap
                    color: theme.textMuted
                    text: String(root.scene.focalPriorFrames || 0) + " views with EXIF focal priors · "
                        + String(root.scene.estimatedIntrinsicsFrames || 0) + " with heuristic focal estimates. Refined during reconstruction."
                }
                Label {
                    objectName: "sceneReconstructionQualityStatus"
                    Layout.fillWidth: true
                    visible: root.showDetails && root.renderable &&
                             root.scene.heldOutPsnr !== undefined && root.scene.heldOutSsim !== undefined
                    wrapMode: Text.WordWrap; color: theme.textMuted
                    text: "Training evaluation · PSNR " + Number(root.scene.heldOutPsnr).toFixed(2) + " dB · SSIM " + Number(root.scene.heldOutSsim).toFixed(3)
                        + ". Compare neutral held-out photos and detail crops before adding blur or film effects. These scores do not certify new viewpoints."
                }
                ActionButton {
                    objectName: "sceneDetailsDisclosureButton"
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
    }
    SceneInspectorPanel {
        id: photoControls
        parent: studioLayout.inspectorHost
        anchors.fill: parent
        studioHost: studioLayout.inspectorHost
        panelKey: "camera"
        objectName: "scenePhotoControlsPopup"
        property int page: 4
        property bool halationPage: false
        property int postPage: 0
        property bool lensAberrations: false
        property bool renderDetailsExpanded: false
        property bool advancedVisible: false
        property bool rayTracingOptedOut: false
        Component.onCompleted: {
            if (Number(root.lensSettings.apertureRadius || 0) > 0 && !Boolean(root.lensSettings.rayTraced))
                rayTracingOptedOut = true
        }
        onPageChanged: {
            if (contentItem) contentItem.contentY = 0
            advancedVisible = page !== 4
        }
        padding: theme.space16
        contentItem: Flickable {
            objectName: "scenePhotoControlsScroll"
            clip: true
            contentWidth: width
            contentHeight: photoControlContents.implicitHeight
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: photoControlContents
                width: parent.width
                spacing: theme.space6

                RowLayout {
                    id: sceneCameraHeaderRow
                    visible: photoControls.page === 4 || photoControls.advancedVisible
                    Layout.fillWidth: true
                    spacing: theme.space4
                    AppIcon { name: "camera-lens"; color: theme.textMuted; Layout.preferredWidth: theme.scaled(18); Layout.preferredHeight: theme.scaled(18) }
                    Label {
                        text: "Camera"
                        color: theme.textStrong
                        font.pixelSize: theme.typeSection
                        font.weight: theme.weightSection
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        objectName: "sceneQuickAutofocusButton"
                        visible: photoControls.page === 4 && root.renderable
                        compact: true; showText: false
                        iconName: "focus-frame"
                        accent: theme.textStrong
                        accessibleName: root.autoFocusActive ? "Autofocus on · click to lock" : "Autofocus"
                        tooltipText: root.autoFocusActive
                            ? "Autofocus is active at the centre of frame. Click to lock the current focus."
                            : "Focus on the centre of the image as you move."
                        active: root.autoFocusActive
                        onClicked: root.actionError = String(viewport.setFocusMode(root.autoFocusActive ? "manual" : "auto-centre").error || "")
                    }
                    ActionButton {
                        objectName: "sceneQuickPickFocusButton"
                        visible: photoControls.page === 4 && root.renderable
                        compact: true; showText: false
                        iconName: "target"
                        accent: theme.textStrong
                        accessibleName: root.focusPickArmed ? "Cancel subject focus" : "Pick subject focus"
                        tooltipText: root.focusPickArmed ? "Cancel subject focus selection (Esc)." : "Click a subject in the photo to focus there."
                        enabled: !viewport.status.focusPicking && Boolean(viewport.status.frameAvailable)
                        active: root.focusPickArmed
                        onClicked: { root.focusPickArmed = !root.focusPickArmed; root.actionError = ""; root.forceActiveFocus() }
                    }
                    ActionButton {
                        objectName: "scenePhotoAdvancedToggle"
                        compact: true; showText: false
                        iconName: "settings"
                        accent: theme.textStrong
                        accessibleName: photoControls.advancedVisible ? "Hide advanced camera settings" : "Advanced camera settings"
                        tooltipText: accessibleName
                        active: photoControls.advancedVisible
                        onClicked: {
                            if (photoControls.advancedVisible) {
                                photoControls.advancedVisible = false
                                photoControls.page = 4
                            } else photoControls.advancedVisible = true
                        }
                    }
                    ActionButton {
                        objectName: "sceneResetPhotoButton"
                        compact: true; showText: false; iconName: "reset"
                        accent: theme.textStrong
                        accessibleName: "Reset photo settings"
                        tooltipText: "Reset camera, lens, exposure, glow and film settings."
                        onClicked: {
                            photoControls.rayTracingOptedOut = false
                            const settings = {schema: "framehouse-gaussian-photo-settings-v1", focalScale: 1, exposureEv: 0, look: "neutral"}
                            if (root.physicalCameraActive) {
                                settings.schema = "framehouse-gaussian-photo-settings-v2"
                                settings.lens = {
                                    schema: "framehouse-thin-lens-v1",
                                    focusDistance: Number(root.lensSettings.focusDistance),
                                    apertureRadius: Number(root.lensSettings.apertureRadius),
                                    blades: 0, rotationDegrees: 0, roundness: 0, samples: 32
                                }
                            }
                            const result = viewport.setPhotoSettings(settings)
                            root.actionError = String(result.error || "")
                        }
                    }
                }
                GridLayout {
                    id: primaryCameraControlsGrid
                    readonly property bool stacked: width < theme.scaled(340)
                    objectName: "scenePrimaryCameraControls"
                    visible: root.renderable && (photoControls.page === 1 || photoControls.page === 2 || photoControls.page === 4)
                    Layout.fillWidth: true
                    columns: stacked ? 1 : 2
                    rowSpacing: stacked ? theme.space6 : theme.space12
                    columnSpacing: theme.space12

                    Rectangle {
                        id: focusControlCard
                        objectName: "sceneFocusControlCard"
                        visible: photoControls.page === 1 || photoControls.page === 4
                        Layout.row: 0; Layout.column: 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.scaled(primaryCameraControlsGrid.stacked ? 70 : 82)
                        radius: theme.radius6
                        color: theme.alpha(theme.ink800, 0.32)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.55)
                        AppIcon {
                            name: "focus-frame"; color: theme.textMuted
                            x: theme.space8; y: theme.space8
                            width: theme.scaled(16); height: width
                        }
                        Label {
                            text: "Focus"
                            color: theme.textStrong
                            font.pixelSize: theme.typeLabel
                            font.weight: theme.weightSection
                            anchors.left: parent.left; anchors.leftMargin: theme.scaled(30)
                            anchors.verticalCenter: parent.top; anchors.verticalCenterOffset: theme.scaled(16)
                        }
                        Label {
                            text: root.physicalCameraActive ? "m" : "scene units"
                            color: theme.textMuted
                            font.pixelSize: theme.typeMicro
                            anchors.right: parent.right; anchors.rightMargin: theme.space8
                            anchors.verticalCenter: parent.top; anchors.verticalCenterOffset: theme.scaled(16)
                        }
                    }
                    Rectangle {
                        id: focalControlCard
                        objectName: "sceneFocalControlCard"
                        visible: photoControls.page === 1 || photoControls.page === 4
                        Layout.row: primaryCameraControlsGrid.stacked ? 1 : 0; Layout.column: primaryCameraControlsGrid.stacked ? 0 : 1
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.scaled(primaryCameraControlsGrid.stacked ? 70 : 82)
                        radius: theme.radius6
                        color: theme.alpha(theme.ink800, 0.32)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.55)
                        AppIcon {
                            name: "camera-lens"; color: theme.textMuted
                            x: theme.space8; y: theme.space8
                            width: theme.scaled(16); height: width
                        }
                        Label {
                            text: root.physicalCameraActive ? "Focal length" : "Framing"
                            color: theme.textStrong
                            font.pixelSize: theme.typeLabel
                            font.weight: theme.weightSection
                            anchors.left: parent.left; anchors.leftMargin: theme.scaled(30)
                            anchors.verticalCenter: parent.top; anchors.verticalCenterOffset: theme.scaled(16)
                        }
                        Label {
                            text: root.physicalCameraActive ? "mm" : "×"
                            color: theme.textMuted
                            font.pixelSize: theme.typeMicro
                            anchors.right: parent.right; anchors.rightMargin: theme.space8
                            anchors.verticalCenter: parent.top; anchors.verticalCenterOffset: theme.scaled(16)
                        }
                    }
                    Rectangle {
                        id: apertureControlCard
                        objectName: "sceneApertureControlCard"
                        visible: photoControls.page === 2 || photoControls.page === 4
                        Layout.row: primaryCameraControlsGrid.stacked ? 2 : 1; Layout.column: 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.scaled(primaryCameraControlsGrid.stacked ? 70 : 82)
                        radius: theme.radius6
                        color: theme.alpha(theme.ink800, 0.32)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.55)
                        AppIcon {
                            name: "aperture"; color: theme.textMuted
                            x: theme.space8; y: theme.space8
                            width: theme.scaled(16); height: width
                        }
                        Label {
                            text: "Aperture"
                            color: theme.textStrong
                            font.pixelSize: theme.typeLabel
                            font.weight: theme.weightSection
                            anchors.left: parent.left; anchors.leftMargin: theme.scaled(30)
                            anchors.verticalCenter: parent.top; anchors.verticalCenterOffset: theme.scaled(16)
                        }
                        Label {
                            text: root.physicalCameraActive ? "f/" : "units"
                            color: theme.textMuted
                            font.pixelSize: theme.typeMicro
                            anchors.right: parent.right; anchors.rightMargin: theme.space8
                            anchors.verticalCenter: parent.top; anchors.verticalCenterOffset: theme.scaled(16)
                        }
                    }
                    Rectangle {
                        id: exposureControlCard
                        objectName: "sceneExposureControlCard"
                        visible: photoControls.page === 4
                        Layout.row: primaryCameraControlsGrid.stacked ? 3 : 1; Layout.column: primaryCameraControlsGrid.stacked ? 0 : 1
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.scaled(primaryCameraControlsGrid.stacked ? 70 : 82)
                        radius: theme.radius6
                        color: theme.alpha(theme.ink800, 0.32)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.55)
                        AppIcon {
                            name: "exposure"; color: theme.textMuted
                            x: theme.space8; y: theme.space8
                            width: theme.scaled(16); height: width
                        }
                        Label {
                            text: "Exposure"
                            color: theme.textStrong
                            font.pixelSize: theme.typeLabel
                            font.weight: theme.weightSection
                            anchors.left: parent.left; anchors.leftMargin: theme.scaled(30)
                            anchors.verticalCenter: parent.top; anchors.verticalCenterOffset: theme.scaled(16)
                        }
                        Label {
                            text: "EV"
                            color: theme.textMuted
                            font.pixelSize: theme.typeMicro
                            anchors.right: parent.right; anchors.rightMargin: theme.space8
                            anchors.verticalCenter: parent.top; anchors.verticalCenterOffset: theme.scaled(16)
                        }
                    }
                }
                ToneComboBox {
                    objectName: "scenePhotoSettingsSectionCombo"
                    accessibleLabel: "Photo settings section"
                    visible: photoControls.advancedVisible
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: photoControlContents.width
                    currentIndex: photoControls.page
                    model: ["Color & exposure", "Focus detail", "Lens & bokeh", "Film & halation", "Camera", "Bloom & flare"]
                    onActivated: photoControls.page = currentIndex
                }
                Label {
                    visible: photoControls.page === 2
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: "Shape the bokeh, bloom and highlight streaks independently. These are editable lens-character approximations; oval bokeh does not change the field of view."
                }
                ToolValueSlider {
                    objectName: "sceneBokehAspectSlider"
                    visible: photoControls.page === 2; Layout.fillWidth: true
                    label: "Bokeh shape · horizontal to vertical"; showCaption: true
                    from: 0.5; to: 3; stepSize: 0.05
                    value: Number(root.lensSettings.bokehAspect || 1)
                    valueText: Math.abs(value - 1) < 0.01 ? "Round" : Number(value).toFixed(2) + ":1"
                    onPressedChanged: pressed ? viewport.beginCameraAdjustment() : viewport.endCameraAdjustment()
                    onMoved: root.updateLensSetting("bokehAspect", value)
                }
                Repeater {
                    model: [
                        {key: "bloomAmount", label: "Bloom · highlight glow", low: 0, high: 4, step: 0.05},
                        {key: "bloomRadius", label: "Bloom spread", low: 0.5, high: 32, step: 0.5},
                        {key: "glareAmount", label: "Highlight streaks", low: 0, high: 4, step: 0.05},
                        {key: "glareRadius", label: "Streak length", low: 1, high: 64, step: 1},
                        {key: "threshold", label: "Glow & streak highlight cutoff", low: 0, high: 2, step: 0.01}
                    ]
                    delegate: ToolValueSlider {
                        required property var modelData
                        objectName: "sceneLensCharacter_" + modelData.key
                        visible: photoControls.page === 2; Layout.fillWidth: true
                        label: modelData.label; showCaption: true
                        from: modelData.low; to: modelData.high; stepSize: modelData.step
                        value: Number(root.postSettings[modelData.key])
                        valueText: Number(value).toFixed(2)
                        onMoved: root.updatePostSetting(modelData.key, value)
                    }
                }
                Label {
                    visible: root.physicalSetupVisible
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMain
                    text: "Ideal thin lens · Sensor dimensions set framing. Measured scene scale sets the physical size of focus and bokeh."
                }
                Label {
                    objectName: "sceneQuickFocusStatus"
                    visible: photoControls.page === 4 && (root.focusPickArmed || viewport.status.focusPicking)
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMain
                    font.pixelSize: theme.typeCaption
                    text: root.focusPickArmed ? "Click a subject to focus · Esc cancels"
                        : viewport.status.focusPicking ? "Finding focus…"
                        : root.autoFocusActive ? root.autofocusStatus.status === "fallback"
                            ? "Autofocus fallback · pick a subject to focus"
                            : "Autofocus · centre of frame" + (root.autofocusStatus.locked ? " · locked" : " · finding…")
                        : "Autofocus"
                }
                RowLayout {
                    id: sceneFocusControlRow
                    parent: focusControlCard
                    x: theme.space8
                    y: theme.scaled(30)
                    width: parent.width - theme.space16
                    height: parent.height - theme.scaled(38)
                    visible: photoControls.page === 1 || photoControls.page === 4
                    ActionButton {
                        objectName: "sceneFocusNearerButton"
                        compact: true; showText: false; iconName: "minus"
                        accessibleName: "Focus nearer"
                        tooltipText: "Move the focus plane nearer by a small step."
                        enabled: !root.autoFocusActive
                        onClicked: root.adjustFocus(-1)
                    }
                    ToneTextField {
                        numericEntry: true
                        objectName: "sceneLensFocusField"
                        enabled: !root.autoFocusActive
                        property bool userEdited: false
                        onTextEdited: userEdited = true
                        font.pixelSize: theme.scaled(18)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        leftPadding: theme.space4; rightPadding: theme.space4
                        inlineField: true
                        text: Number(root.physicalCameraActive ? root.physicalCamera.focusDistanceM : root.lensSettings.focusDistance).toPrecision(5)
                        validator: DoubleValidator { bottom: root.physicalCameraActive ? 0.001 : 0.01; top: 100000000; locale: "C" }
                        onEditingFinished: {
                            if (!userEdited) return
                            userEdited = false
                            if (acceptableInput) root.setManualFocus(Number(text))
                            else root.actionError = "Enter a valid focus distance in the displayed units."
                        }
                        Accessible.name: root.physicalCameraActive ? "Focus distance in metres from the lens plane" : "Focus distance in scene units"
                        Accessible.description: root.autoFocusActive ? "Lock autofocus before editing the fallback distance." : "Exact focus plane distance."
                    }
                    ActionButton {
                        objectName: "sceneFocusFartherButton"
                        compact: true; showText: false; iconName: "plus"
                        accessibleName: "Focus farther"
                        tooltipText: "Move the focus plane farther by a small step."
                        enabled: !root.autoFocusActive
                        onClicked: root.adjustFocus(1)
                    }
                }
                ToolValueSlider {
                    objectName: "sceneFocusDistanceSlider"
                    focusPolicy: Qt.StrongFocus
                    visible: (photoControls.page === 1 || (photoControls.page === 4 && photoControls.advancedVisible)) && !root.autoFocusActive
                    Layout.fillWidth: true
                    label: "Focus · near to far"; showCaption: true
                    from: Math.log(root.minimumFocusDistance)
                    to: Math.log(Math.max(root.minimumFocusDistance * 100, root.physicalCameraActive ? 1000 : Number(viewport.status.sceneFocusDistance || 1) * 20))
                    value: Math.log(Math.max(root.minimumFocusDistance, root.focusDistanceValue))
                    valueText: Number(Math.exp(value)).toFixed(3) + (root.physicalCameraActive ? " m" : " units")
                    onPressedChanged: pressed ? viewport.beginCameraAdjustment() : viewport.endCameraAdjustment()
                    onMoved: root.setManualFocus(Math.exp(value))
                }
                Label {
                    objectName: "sceneCameraOpticsReadout"
                    visible: Boolean(root.cameraOptics.ok) && (photoControls.page === 1 || (photoControls.page === 4 && photoControls.advancedVisible))
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: theme.textStrong
                    text: "Field of view  " + Number(root.cameraOptics.horizontalFovDegrees || 0).toFixed(2)
                        + "° × " + Number(root.cameraOptics.verticalFovDegrees || 0).toFixed(2) + "°"
                        + (root.cameraOptics.physical && photoControls.renderDetailsExpanded ? "\nActive gate  " + Number(root.cameraOptics.effectiveGateWidthMm).toFixed(2)
                            + " × " + Number(root.cameraOptics.effectiveGateHeightMm).toFixed(2) + " mm"
                            + (Number(root.lensSettings.bokehAspect || 1) !== 1 ? "\nEquivalent pupil  " : "\nEntrance pupil  ")
                            + Number(root.cameraOptics.entrancePupilDiameterMm).toFixed(2) + " mm" : "")
                }
                Label {
                    objectName: "sceneCameraDepthOfFieldReadout"
                    visible: photoControls.page === 4 && photoControls.advancedVisible && root.renderable
                    Layout.fillWidth: true
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    color: theme.textMain
                    font.pixelSize: theme.typeCaption
                    text: root.physicalCameraActive && root.cameraOptics.physical
                        ? photoControls.advancedVisible
                            ? "Focus plane " + Number(root.cameraOptics.focusFromLensM).toFixed(3) + " m · DoF "
                                + Number(root.cameraOptics.depthOfFieldNearM).toFixed(3) + "–"
                                + (root.cameraOptics.depthOfFieldFarInfinite ? "∞" : Number(root.cameraOptics.depthOfFieldFarM).toFixed(3)) + " m"
                            : "Focus plane " + Number(root.cameraOptics.focusFromLensM).toFixed(3) + " m"
                        : "Focus plane " + Number(root.focusDistanceValue).toPrecision(4) + (root.physicalCameraActive ? " m" : " units")
                }
                RowLayout {
                    visible: root.physicalSetupVisible; Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: "Metres / scene unit"; color: theme.textMain; wrapMode: Text.WordWrap }
                    ToneTextField {
                        numericEntry: true
                        id: physicalScale; objectName: "scenePhysicalScaleField"
                        Layout.preferredWidth: theme.inspectorValueWidth
                        Layout.maximumWidth: theme.inspectorValueWidth
                        Layout.minimumWidth: 0
                        text: root.physicalCameraActive ? Number(root.physicalCamera.metresPerSceneUnit).toPrecision(8) : ""
                        placeholderText: "Measured scale"
                        validator: DoubleValidator { bottom: 0.000001; top: 1000000; locale: "C" }
                        Accessible.name: "User-supplied metres per scene unit"
                    }
                }
                ToneComboBox {
                    id: physicalGate; objectName: "scenePhysicalGateCombo"
                    accessibleLabel: "Sensor size preset"
                    visible: root.physicalSetupVisible; Layout.fillWidth: true
                    model: root.sensorGates.map(gate => gate.name).concat(["Custom sensor"])
                    currentIndex: sceneWorkspaceSettings.sensorPresetIndex
                    onActivated: {
                        sceneWorkspaceSettings.sensorPresetIndex = currentIndex
                        if (currentIndex < root.sensorGates.length) {
                            sceneWorkspaceSettings.sensorDraftWidthMm = root.sensorGates[currentIndex].width
                            sceneWorkspaceSettings.sensorDraftHeightMm = root.sensorGates[currentIndex].height
                        }
                    }
                }
                RowLayout {
                    visible: root.physicalSetupVisible
                    Layout.fillWidth: true
                    Label { text: "Sensor (mm)"; color: theme.textMain }
                    ToneTextField {
                        id: physicalSensorWidth
                        objectName: "scenePhysicalSensorWidthField"
                        Layout.minimumWidth: 0
                        Layout.fillWidth: true
                        numericEntry: true
                        property bool userEdited: false
                        onTextEdited: userEdited = true
                        text: Number(sceneWorkspaceSettings.sensorDraftWidthMm).toString()
                        validator: DoubleValidator { bottom: 1; top: 300; locale: "C" }
                        Accessible.name: "Sensor width in millimetres"
                        onEditingFinished: if (userEdited && acceptableInput) {
                            userEdited = false
                            sceneWorkspaceSettings.sensorDraftWidthMm = Number(text)
                            sceneWorkspaceSettings.sensorPresetIndex = root.sensorGates.length
                        }
                    }
                    Label { text: "×"; color: theme.textMuted }
                    ToneTextField {
                        id: physicalSensorHeight
                        objectName: "scenePhysicalSensorHeightField"
                        Layout.minimumWidth: 0
                        Layout.fillWidth: true
                        numericEntry: true
                        property bool userEdited: false
                        onTextEdited: userEdited = true
                        text: Number(sceneWorkspaceSettings.sensorDraftHeightMm).toString()
                        validator: DoubleValidator { bottom: 1; top: 300; locale: "C" }
                        Accessible.name: "Sensor height in millimetres"
                        onEditingFinished: if (userEdited && acceptableInput) {
                            userEdited = false
                            sceneWorkspaceSettings.sensorDraftHeightMm = Number(text)
                            sceneWorkspaceSettings.sensorPresetIndex = root.sensorGates.length
                        }
                    }
                }
                ActionButton {
                    objectName: "sceneApplyPhysicalCameraButton"
                    visible: root.physicalSetupVisible
                    enabled: physicalScale.acceptableInput && physicalSensorWidth.acceptableInput && physicalSensorHeight.acceptableInput
                    text: root.physicalCameraActive ? "Update physical camera" : "Use physical camera"
                    onClicked: {
                        const settings = root.physicalCameraActive ? Object.assign({}, root.physicalCamera)
                            : {schema: "framehouse-physical-thin-lens-v1", focalLengthMm: 50, fNumber: 2.8, focusDistanceM: 2}
                        settings.metresPerSceneUnit = Number(physicalScale.text)
                        settings.sensorWidthMm = Number(physicalSensorWidth.text)
                        settings.sensorHeightMm = Number(physicalSensorHeight.text)
                        root.actionError = String(viewport.setPhysicalCamera(settings).error || "")
                    }
                }
                ActionButton {
                    objectName: "sceneClearPhysicalCameraButton"
                    visible: root.physicalSetupVisible && root.physicalCameraActive
                    text: "Use scene units"
                    tooltipText: "Switch to scene units while keeping the current view."
                    onClicked: root.actionError = String(viewport.clearPhysicalCamera().error || "")
                }
                RowLayout {
                    parent: focalControlCard
                    x: theme.space8
                    y: theme.scaled(30)
                    width: parent.width - theme.space16
                    height: parent.height - theme.scaled(38)
                    visible: (photoControls.page === 1 || photoControls.page === 4) && root.physicalCameraActive; Layout.fillWidth: true
                    ActionButton {
                        objectName: "scenePhysicalFocalDecreaseButton"
                        visible: photoControls.page === 4
                        compact: true; showText: false; iconName: "minus"; accessibleName: "Shorter focal length"
                        tooltipText: "Shorten the lens and widen the framing."
                        onClicked: root.adjustFocalLength(-1)
                    }
                    ToneTextField {
                        numericEntry: true
                        objectName: "scenePhysicalFocalField"; Layout.fillWidth: true
                        property bool userEdited: false
                        text: Number(root.physicalCamera.focalLengthMm || 50).toPrecision(5)
                        font.pixelSize: theme.scaled(18)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        Layout.minimumWidth: 0
                        leftPadding: theme.space4; rightPadding: theme.space4
                        inlineField: true
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
                    ActionButton {
                        objectName: "scenePhysicalFocalIncreaseButton"
                        visible: photoControls.page === 4
                        compact: true; showText: false; iconName: "plus"; accessibleName: "Longer focal length"
                        tooltipText: "Lengthen the lens and tighten the framing."
                        onClicked: root.adjustFocalLength(1)
                    }
                }
                ToolValueSlider {
                    objectName: "scenePhysicalZoomSlider"
                    visible: (photoControls.page === 1 || (photoControls.page === 4 && photoControls.advancedVisible)) && root.physicalCameraActive
                    Layout.fillWidth: true
                    label: "Zoom · focal length"; showCaption: true
                    valueText: Number(value).toFixed(1) + " mm"
                    from: Math.min(14, Number(root.physicalCamera.focalLengthMm || 50))
                    to: Math.max(Number(root.physicalCamera.focalLengthMm || 50), Math.min(200, Number(root.physicalCamera.focusDistanceM || 2) * 990))
                    stepSize: 0.1
                    value: Number(root.physicalCamera.focalLengthMm || 50)
                    onPressedChanged: { if (pressed) viewport.beginCameraAdjustment(); else viewport.endCameraAdjustment() }
                    onMoved: root.updatePhysicalSetting("focalLengthMm", value)
                }
                RowLayout {
                    id: sceneApertureControlRow
                    parent: apertureControlCard
                    x: theme.space8
                    y: theme.scaled(30)
                    width: parent.width - theme.space16
                    height: parent.height - theme.scaled(38)
                    visible: photoControls.page === 2 || photoControls.page === 4
                    ActionButton {
                        objectName: "sceneApertureDecreaseButton"
                        visible: photoControls.page === 4
                        compact: true; showText: false; iconName: "minus"
                        accessibleName: root.physicalCameraActive ? "Close aperture by one stop" : "Close aperture"
                        tooltipText: root.physicalCameraActive ? "Close aperture by one stop. Exposure is separate." : "Close the aperture; at zero the lens is pinhole."
                        enabled: !root.physicalCameraActive || Number(root.physicalCamera.fNumber) < 128
                        onClicked: root.adjustAperture(-1)
                    }
                    ToneTextField {
                        numericEntry: true
                        objectName: "scenePhysicalFNumberField"; Layout.fillWidth: true
                        visible: root.physicalCameraActive
                        property bool userEdited: false
                        text: Number(root.physicalCamera.fNumber || 2.8).toPrecision(4)
                        font.pixelSize: theme.scaled(18)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        Layout.minimumWidth: 0
                        leftPadding: theme.space4; rightPadding: theme.space4
                        inlineField: true
                        validator: DoubleValidator { bottom: 0.1; top: 128; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: {
                            if (!userEdited) return
                            userEdited = false
                            if (acceptableInput) root.setPhysicalFNumber(Number(text))
                            else root.actionError = "Enter an f-number from 0.1 to 128."
                        }
                        Accessible.name: "Physical aperture f-number"
                    }
                    ToneTextField {
                        objectName: "sceneSceneApertureValue"
                        visible: !root.physicalCameraActive
                        numericEntry: true
                        Layout.fillWidth: true
                        property bool userEdited: false
                        text: Number(root.lensSettings.apertureRadius || 0).toPrecision(4)
                        font.pixelSize: theme.scaled(18)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        Layout.minimumWidth: 0
                        leftPadding: theme.space4; rightPadding: theme.space4
                        inlineField: true
                        validator: DoubleValidator { bottom: 0; top: Math.max(root.sceneApertureMaximum, Number(root.lensSettings.apertureRadius || 0)); locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: {
                            if (!userEdited) return
                            userEdited = false
                            if (!acceptableInput) { root.actionError = "Enter an aperture radius from zero to the displayed scene range."; return }
                            const radius = Number(text)
                            const edit = {apertureRadius: radius}
                            if (radius > 0 && !photoControls.rayTracingOptedOut) {
                                edit.rayTraced = true
                                edit.samples = Math.max(128, Number(root.lensSettings.samples || 32))
                            }
                            root.updateLensSettings(edit)
                        }
                        Accessible.name: "Aperture radius in scene units"
                        Accessible.description: "Zero is pinhole. A positive radius creates depth of field."
                    }
                    ActionButton {
                        objectName: "sceneApertureIncreaseButton"
                        visible: photoControls.page === 4
                        compact: true; showText: false; iconName: "plus"
                        accessibleName: root.physicalCameraActive ? "Open aperture by one stop" : "Open aperture"
                        tooltipText: root.physicalCameraActive ? "Open aperture by one stop for shallower depth of field." : "Open the aperture to add depth of field and bokeh."
                        enabled: !root.physicalCameraActive || Number(root.physicalCamera.fNumber) > 0.1
                        onClicked: root.adjustAperture(1)
                    }
                }
                ToolValueSlider {
                    objectName: "scenePhysicalFStopSlider"
                    visible: photoControls.page === 2 || (photoControls.page === 4 && photoControls.advancedVisible && root.physicalCameraActive)
                    Layout.fillWidth: true
                    label: "Aperture"; showCaption: true
                    valueText: "f/" + Number(root.physicalCamera.fNumber || 2.8).toPrecision(3)
                    // Equal travel changes aperture in stops. Keep typed values
                    // beyond f/22 visible without clipping their slider position.
                    from: Math.log(0.1) / Math.LN2
                    to: Math.log(Math.max(22, Number(root.physicalCamera.fNumber || 2.8))) / Math.LN2
                    stepSize: 0
                    value: Math.log(Number(root.physicalCamera.fNumber || 2.8)) / Math.LN2
                    onPressedChanged: pressed ? viewport.beginCameraAdjustment() : viewport.endCameraAdjustment()
                    onMoved: root.updatePhysicalSetting("fNumber", Math.round(Math.pow(2, value) * 1000) / 1000)
                }
                Label {
                    visible: photoControls.page === 2 && root.physicalCameraActive && Number(root.physicalCamera.fNumber) < 0.5
                    Layout.fillWidth: true
                    text: "Experimental ideal pupil · extreme aperture"
                    color: theme.textMuted
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    parent: focalControlCard
                    x: theme.space8
                    y: theme.scaled(30)
                    width: parent.width - theme.space16
                    height: parent.height - theme.scaled(38)
                    objectName: "sceneUncalibratedFocalStepper"
                    visible: (photoControls.page === 1 || photoControls.page === 4) && !root.physicalCameraActive
                    ActionButton {
                        objectName: "sceneFocalDecreaseButton"
                        visible: photoControls.page === 4
                        compact: true; showText: false; iconName: "minus"; accessibleName: "Widen framing"
                        tooltipText: "Widen the current camera framing."
                        onClicked: root.adjustFocalLength(-1)
                    }
                    ToneTextField {
                        objectName: "sceneFocalValue"
                        Layout.fillWidth: true
                        numericEntry: true
                        property bool userEdited: false
                        text: Number(root.photoSettings.focalScale).toFixed(2)
                        font.pixelSize: theme.scaled(18)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        Layout.minimumWidth: 0
                        leftPadding: theme.space4; rightPadding: theme.space4
                        inlineField: true
                        validator: DoubleValidator { bottom: 0.25; top: 4; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: {
                            if (!userEdited) return
                            userEdited = false
                            if (acceptableInput) root.updatePhotoSetting("focalScale", Number(text))
                            else root.actionError = "Enter framing from 0.25× to 4×."
                        }
                        Accessible.name: "Lens framing relative to the scene camera"
                    }
                    ActionButton {
                        objectName: "sceneFocalIncreaseButton"
                        visible: photoControls.page === 4
                        compact: true; showText: false; iconName: "plus"; accessibleName: "Tighten framing"
                        tooltipText: "Tighten the current camera framing."
                        onClicked: root.adjustFocalLength(1)
                    }
                }
                ToolValueSlider {
                    id: photoFocal
                    label: "Lens framing relative to the current camera"
                    valueText: Number(value).toFixed(2) + "×"
                    visible: (photoControls.page === 1 || (photoControls.page === 4 && photoControls.advancedVisible)) && !root.physicalCameraActive
                    objectName: "scenePhotoFocalSlider"
                    Layout.fillWidth: true
                    from: 0.25; to: 4; stepSize: 0.05
                    value: Number(root.photoSettings.focalScale)
                    onPressedChanged: pressed ? viewport.beginCameraAdjustment() : viewport.endCameraAdjustment()
                    onMoved: root.updatePhotoSetting("focalScale", value)
                }
                RowLayout {
                    id: scenePrimaryExposureRow
                    parent: exposureControlCard
                    x: theme.space8
                    y: theme.scaled(30)
                    width: parent.width - theme.space16
                    height: parent.height - theme.scaled(38)
                    objectName: "scenePrimaryExposureStepper"
                    visible: photoControls.page === 4
                    ActionButton { objectName: "sceneExposureDecreaseButton"; compact: true; showText: false; iconName: "minus"; accessibleName: "Decrease exposure by one quarter stop"; tooltipText: "Decrease exposure by 0.25 EV."; onClicked: root.adjustExposure(-1) }
                    ToneTextField {
                        objectName: "sceneExposureValue"
                        Layout.fillWidth: true
                        numericEntry: true
                        property bool userEdited: false
                        text: Number(root.photoSettings.exposureEv).toFixed(2)
                        font.pixelSize: theme.scaled(18)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        Layout.minimumWidth: 0
                        leftPadding: theme.space4; rightPadding: theme.space4
                        inlineField: true
                        validator: DoubleValidator { bottom: -6; top: 6; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: {
                            if (!userEdited) return
                            userEdited = false
                            if (acceptableInput) root.updatePhotoSetting("exposureEv", Number(text))
                            else root.actionError = "Enter exposure from -6 to +6 EV."
                        }
                        Accessible.name: "Exposure compensation in EV"
                    }
                    ActionButton { objectName: "sceneExposureIncreaseButton"; compact: true; showText: false; iconName: "plus"; accessibleName: "Increase exposure by one quarter stop"; tooltipText: "Increase exposure by 0.25 EV."; onClicked: root.adjustExposure(1) }
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
                    visible: photoControls.page === 0 || (photoControls.page === 4 && photoControls.advancedVisible)
                    objectName: "scenePhotoExposureSlider"
                    Layout.fillWidth: true
                    from: -6; to: 6; stepSize: 0.25
                    value: Number(root.photoSettings.exposureEv)
                    onMoved: root.updatePhotoSetting("exposureEv", value)
                }
                Repeater {
                    model: [
                        {key: "warmth", label: "Temperature · cool to warm", low: -1, high: 1},
                        {key: "tint", label: "Tint · green to magenta", low: -1, high: 1},
                        {key: "vibrance", label: "Vibrance", low: -1, high: 1},
                        {key: "fade", label: "Faded blacks", low: 0, high: 0.25}
                    ]
                    delegate: ToolValueSlider {
                        required property var modelData
                        objectName: "sceneColor_" + modelData.key
                        focusPolicy: Qt.StrongFocus
                        Layout.fillWidth: true
                        visible: photoControls.page === 0
                        label: modelData.label; showCaption: true
                        from: modelData.low; to: modelData.high; stepSize: 0.01
                        value: Number(root.toneSettings[modelData.key] || 0)
                        valueText: Math.round(value * 100).toString()
                        onMoved: root.updateToneSetting(modelData.key, value)
                    }
                }
                ToolValueSlider {
                    objectName: "sceneColorSaturationSlider"
                    visible: photoControls.page === 0; Layout.fillWidth: true
                    label: "Saturation"; showCaption: true
                    from: 0; to: 2; stepSize: 0.01; value: Number(root.postSettings.saturation)
                    valueText: Math.round(value * 100) + "%"
                    onMoved: root.updatePostSetting("saturation", value)
                }
                ActionButton {
                    objectName: "sceneResetColorButton"
                    visible: photoControls.page === 0
                    text: "Reset color"
                    onClicked: root.resetColor()
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
                Label {
                    visible: photoControls.page === 0
                    Layout.fillWidth: true; color: theme.textMain
                    text: "Contrast  " + Number(root.toneSettings.contrast).toFixed(2)
                }
                ToolValueSlider {
                    objectName: "scenePhotoContrastSlider"
                    label: "Photographic contrast"
                    visible: photoControls.page === 0; Layout.fillWidth: true
                    from: 0.25; to: 2; stepSize: 0.05
                    value: Number(root.toneSettings.contrast); valueText: Number(value).toFixed(2)
                    onMoved: root.updateToneSetting("contrast", value)
                }
                Label {
                    visible: photoControls.page === 0
                    Layout.fillWidth: true; color: theme.textMain
                    text: "Highlight compression  " + Math.round(Number(root.toneSettings.highlightCompression) * 100) + "%"
                }
                ToolValueSlider {
                    objectName: "scenePhotoHighlightCompressionSlider"
                    label: "Photographic highlight compression"
                    visible: photoControls.page === 0; Layout.fillWidth: true
                    from: 0; to: 1; stepSize: 0.01
                    value: Number(root.toneSettings.highlightCompression); valueText: Math.round(value * 100) + "%"
                    onMoved: root.updateToneSetting("highlightCompression", value)
                }
                ToneComboBox {
                    objectName: "sceneFocusModeCombo"
                    accessibleLabel: "Camera focus mode"
                    visible: photoControls.page === 1; Layout.fillWidth: true
                    model: ["Manual focus", "Continuous centre autofocus"]
                    currentIndex: root.autoFocusActive ? 1 : 0
                    onActivated: index => root.actionError = String(viewport.setFocusMode(index === 1 ? "auto-centre" : "manual").error || "")
                }
                Label {
                    objectName: "sceneAutofocusReadout"
                    visible: photoControls.page === 1 && root.autoFocusActive
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMain
                    text: root.autofocusStatus.status
                        ? (root.autofocusStatus.locked ? "Centre focus · " : "Fallback focus · ")
                            + Number(root.physicalCameraActive ? root.autofocusStatus.resolvedFocusDistanceM : root.autofocusStatus.resolvedFocusDistance).toPrecision(5)
                            + (root.physicalCameraActive ? " m" : " scene units")
                            + (root.autofocusStatus.reason ? "\n" + root.autofocusStatus.reason : "")
                        : "Finding centre focus…"
                }
                GridLayout {
                    objectName: "sceneRayTracedControlRow"
                    visible: photoControls.page === 4 && photoControls.advancedVisible
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: theme.space8
                    rowSpacing: theme.space6
                    ToneSwitch {
                        objectName: "sceneRayTracedOpticsToggle"
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        text: "Ray trace"
                        accessibleLabel: "Ray-traced depth of field"
                        accessibleDescription: "Sample lens rays for final depth of field. Interactive preview stays fast while the camera moves."
                        enabled: true
                        checked: Boolean(root.lensSettings.rayTraced)
                        onToggled: {
                            photoControls.rayTracingOptedOut = !checked
                            const samples = Number(root.lensSettings.samples || 32)
                            const edit = checked
                                ? root.rayTracedOpticsEdit({samples: Math.max(128, samples)})
                                : {rayTraced: false, samples: samples}
                            root.updateLensSettings(edit)
                        }
                    }
                    Label {
                        objectName: "sceneOpticsQualityLabel"
                        text: "Samples"
                        color: theme.textMuted
                        Accessible.name: "Ray samples per image pixel"
                    }
                    ToneComboBox {
                        id: rayTracingSamplesCombo
                        objectName: "sceneRayTracedQualityCombo"
                        Layout.fillWidth: true
                        Layout.minimumWidth: theme.scaled(60)
                        Layout.preferredWidth: theme.scaled(72)
                        accessibleLabel: "Ray-traced samples per pixel"
                        readonly property int actualSamples: Math.max(1, Number(root.lensSettings.samples || 32))
                        readonly property bool standardQuality: [128, 512, 1024, 4096].indexOf(actualSamples) >= 0
                        enabled: true
                        model: ["128", "512", "1024", "4096"]
                        currentIndex: actualSamples <= 128 ? 0
                            : actualSamples <= 512 ? 1
                            : actualSamples <= 1024 ? 2 : 3
                        displayText: standardQuality ? String(actualSamples) : "Custom"
                        Accessible.description: String(actualSamples) + " rays per pixel currently saved. Choose a standard refinement target."
                        AppToolTip {
                            targetItem: rayTracingSamplesCombo
                            visible: rayTracingSamplesCombo.hovered
                            delay: 300
                            text: "Current setting: " + String(rayTracingSamplesCombo.actualSamples) +
                                  " rays per pixel. Choose a refinement target."
                        }
                        onActivated: index => {
                            photoControls.rayTracingOptedOut = false
                            viewport.beginCameraAdjustment()
                            root.updateLensSettings(root.rayTracedOpticsEdit({samples: [128, 512, 1024, 4096][index]}))
                            viewport.endCameraAdjustment()
                        }
                    }
                    Label {
                        text: "Bokeh edge"
                        color: theme.textMuted
                    }
                    ToneComboBox {
                        id: pupilSoftnessCombo
                        objectName: "scenePupilSoftnessCombo"
                        Layout.fillWidth: true
                        Layout.minimumWidth: theme.scaled(68)
                        Layout.preferredWidth: theme.scaled(84)
                        accessibleLabel: "Bokeh edge softness"
                        readonly property real actualSoftness: Math.max(0, Number(root.lensSettings.pupilSoftness || 0))
                        readonly property bool standardSoftness: [0, 1, 3].indexOf(actualSoftness) >= 0
                        enabled: Boolean(root.lensSettings.rayTraced)
                        model: ["Clean", "Soft", "Feathered"]
                        currentIndex: actualSoftness <= 0 ? 0 : actualSoftness <= 2 ? 1 : 2
                        displayText: standardSoftness ? ["Clean", "Soft", "Feathered"][[0, 1, 3].indexOf(actualSoftness)] : "Custom"
                        Accessible.description: "Saved pupil softness is " + String(actualSoftness) + " of 4. Choose Clean, Soft or Feathered."
                        AppToolTip {
                            targetItem: pupilSoftnessCombo
                            visible: pupilSoftnessCombo.hovered
                            delay: 300
                            text: "Saved pupil softness: " + String(pupilSoftnessCombo.actualSoftness) + " of 4."
                        }
                        onActivated: index => {
                            photoControls.rayTracingOptedOut = false
                            root.updateLensSettings(root.rayTracedOpticsEdit({
                                pupilSoftness: [0, 1, 3][index], samples: Number(root.lensSettings.samples || 32)}))
                        }
                    }
                }
                Label {
                    objectName: "sceneRayTracedQualityHint"
                    visible: photoControls.page === 4 && photoControls.advancedVisible && Boolean(root.lensSettings.rayTraced)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: theme.textMuted
                    text: "Final depth of field · " + Number(root.lensSettings.samples || 32) + " rays per pixel. Quality refines after you stop moving."
                }
                RowLayout {
                    objectName: "scenePrimaryBloomStepper"
                    visible: photoControls.page === 4 && photoControls.advancedVisible
                    Layout.fillWidth: true
                    AppIcon { name: "sparkles"; color: theme.textMuted; Layout.preferredWidth: theme.scaled(16); Layout.preferredHeight: theme.scaled(16) }
                    Label { text: "Bloom"; color: theme.textMain; Layout.preferredWidth: theme.scaled(72) }
                    ActionButton { objectName: "sceneBloomDecreaseButton"; compact: true; showText: false; iconName: "minus"; accessibleName: "Reduce bloom"; tooltipText: "Reduce highlight glow."; onClicked: root.adjustPrimaryBloom(-1) }
                    ToneTextField {
                        objectName: "sceneBloomValue"; Layout.fillWidth: true; Layout.minimumWidth: 0; numericEntry: true
                        property bool userEdited: false
                        text: Number(root.postSettings.bloomAmount || 0).toFixed(2)
                        validator: DoubleValidator { bottom: 0; top: 4; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: { if (!userEdited) return; userEdited = false; if (acceptableInput) root.updatePrimaryBloom(Number(text)) }
                        Accessible.name: "Bloom amount from zero to four"
                    }
                    ActionButton { objectName: "sceneBloomIncreaseButton"; compact: true; showText: false; iconName: "plus"; accessibleName: "Increase bloom"; tooltipText: "Increase highlight glow."; onClicked: root.adjustPrimaryBloom(1) }
                }
                RowLayout {
                    objectName: "scenePrimaryHalationStepper"
                    visible: photoControls.page === 4 && photoControls.advancedVisible
                    Layout.fillWidth: true
                    AppIcon { name: "film"; color: theme.textMuted; Layout.preferredWidth: theme.scaled(16); Layout.preferredHeight: theme.scaled(16) }
                    Label { text: root.filmSettings.highlightHalation ? "Halation" : root.filmPlaneActive ? "Film return" : "Halation"; color: theme.textMain; Layout.preferredWidth: theme.scaled(72) }
                    ActionButton { objectName: "sceneHalationDecreaseButton"; compact: true; showText: false; iconName: "minus"; accessibleName: "Reduce halation"; tooltipText: "Reduce light return around highlights."; onClicked: root.adjustPrimaryHalation(-1) }
                    ToneTextField {
                        objectName: "sceneHalationValue"; Layout.fillWidth: true; Layout.minimumWidth: 0; numericEntry: true
                        property bool userEdited: false
                        text: Number(root.filmSettings.halationAmount || 0).toFixed(2)
                        validator: DoubleValidator { bottom: 0; top: 1; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: { if (!userEdited) return; userEdited = false; if (acceptableInput) root.updatePrimaryHalation(Number(text)) }
                        Accessible.name: "Halation amount from zero to one"
                    }
                    ActionButton { objectName: "sceneHalationIncreaseButton"; compact: true; showText: false; iconName: "plus"; accessibleName: "Increase halation"; tooltipText: "Increase light return around highlights."; onClicked: root.adjustPrimaryHalation(1) }
                }
                ToolValueSlider {
                    objectName: "scenePrimaryBloomSlider"
                    visible: photoControls.page === 4 && photoControls.advancedVisible
                    Layout.fillWidth: true
                    label: "Bloom · highlight glow"
                    showCaption: true
                    from: 0; to: 4; stepSize: 0.05
                    value: Number(root.postSettings.bloomAmount || 0)
                    valueText: Number(value).toFixed(2)
                    onMoved: root.updatePrimaryBloom(value)
                }
                ToolValueSlider {
                    objectName: "scenePrimaryHalationSlider"
                    visible: photoControls.page === 4 && photoControls.advancedVisible
                    Layout.fillWidth: true
                    label: root.filmSettings.highlightHalation ? "Highlight halation"
                        : root.filmPlaneActive ? "Film light return" : "Halation · highlight glow"
                    showCaption: true
                    from: 0; to: 1; stepSize: 0.01
                    value: Number(root.filmSettings.halationAmount || 0)
                    valueText: Number(value).toFixed(2)
                    onMoved: root.updatePrimaryHalation(value)
                }
                RowLayout {
                    objectName: "sceneCameraStartingPointsRow"
                    visible: (photoControls.page === 4 && photoControls.advancedVisible) || photoControls.page === 2 || photoControls.page === 3
                    Layout.fillWidth: true
                    spacing: theme.space8
                    AppIcon {
                        visible: photoControls.page === 2 || photoControls.page === 4
                        name: "camera-lens"
                        color: theme.textMuted
                        Layout.preferredWidth: theme.scaled(16)
                        Layout.preferredHeight: theme.scaled(16)
                    }
                    ToneComboBox {
                        objectName: "sceneLensStartingPoints"
                        visible: photoControls.page === 2 || photoControls.page === 4
                        Layout.fillWidth: true
                        accessibleLabel: "Lens starting point"
                        displayText: "Lens look…"
                        model: SceneLooks.lensLooks.map(look => look.label)
                        currentIndex: -1
                        onActivated: index => root.applyLensLook(SceneLooks.lensLooks[index].key)
                    }
                    AppIcon {
                        visible: photoControls.page === 3 || photoControls.page === 4
                        name: "film"
                        color: theme.textMuted
                        Layout.preferredWidth: theme.scaled(16)
                        Layout.preferredHeight: theme.scaled(16)
                    }
                    ToneComboBox {
                        objectName: "sceneFilmStartingPoints"
                        visible: photoControls.page === 3 || photoControls.page === 4
                        Layout.fillWidth: true
                        accessibleLabel: "Film starting point"
                        displayText: "Film look…"
                        model: SceneLooks.filmLooks.map(look => look.label)
                        currentIndex: -1
                        onActivated: index => root.applyFilmLook(SceneLooks.filmLooks[index].key)
                    }
                }
                ActionButton {
                    objectName: "scenePickFocusButton"
                    text: root.autoFocusActive ? "Pick focus · switch to manual" : "Pick focus in image"
                    visible: photoControls.page === 1
                    enabled: !viewport.status.focusPicking && Boolean(viewport.status.frameAvailable)
                    onClicked: { root.focusPickArmed = true; root.actionError = ""; root.forceActiveFocus() }
                }
                RowLayout {
                    visible: root.physicalCameraActive && (photoControls.page === 1 ||
                        (photoControls.page === 4 && photoControls.advancedVisible && photoControls.renderDetailsExpanded))
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: "Focus criterion (µm)"; color: theme.textMain; wrapMode: Text.WordWrap }
                    ToneTextField {
                        objectName: "sceneFocusCriterionField"
                        Layout.preferredWidth: theme.inspectorValueWidth
                        Layout.maximumWidth: theme.inspectorValueWidth
                        Layout.minimumWidth: 0
                        numericEntry: true
                        text: Number(sceneWorkspaceSettings.permissibleCocMicrons).toString()
                        validator: DoubleValidator { bottom: 0.1; top: 1000; locale: "C" }
                        Accessible.name: "Permissible circle-of-confusion diameter on the sensor in micrometres"
                        onEditingFinished: if (acceptableInput) sceneWorkspaceSettings.permissibleCocMicrons = Number(text)
                    }
                }
                Label {
                    objectName: "sceneFocusPlaneReadout"
                    visible: Boolean(root.cameraOptics.physical) && (photoControls.page === 1 ||
                        (photoControls.page === 4 && photoControls.advancedVisible && photoControls.renderDetailsExpanded))
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: theme.textMuted
                    text: "Focus from lens  " + Number(root.cameraOptics.focusFromLensM).toFixed(3) + " m"
                        + "\nFrom sensor  " + Number(root.cameraOptics.focusFromSensorM).toFixed(3) + " m"
                        + "\nOn-axis focus range  " + Number(root.cameraOptics.depthOfFieldNearM).toFixed(3) + " m – "
                        + (root.cameraOptics.depthOfFieldFarInfinite ? "∞" : Number(root.cameraOptics.depthOfFieldFarM).toFixed(3) + " m")
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
                    onPressedChanged: pressed ? viewport.beginCameraAdjustment() : viewport.endCameraAdjustment()
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
                    onPressedChanged: pressed ? viewport.beginCameraAdjustment() : viewport.endCameraAdjustment()
                    onMoved: root.updateLensSetting("roundness", value)
                }
                Label { visible: photoControls.page === 2 && Number(root.lensSettings.blades) > 0; text: "Aperture rotation  " + Number(root.lensSettings.rotationDegrees).toFixed(1) + "°"; color: theme.textMain }
                ToolValueSlider {
                    label: "Aperture rotation in degrees"
                    valueText: Number(value).toFixed(1) + "°"
                    Layout.fillWidth: true
                    visible: photoControls.page === 2 && Number(root.lensSettings.blades) > 0
                    from: 0; to: 360; value: Number(root.lensSettings.rotationDegrees)
                    onPressedChanged: pressed ? viewport.beginCameraAdjustment() : viewport.endCameraAdjustment()
                    onMoved: root.updateLensSetting("rotationDegrees", value)
                }
                ToneComboBox {
                    objectName: "sceneLensCharacterCombo"
                    accessibleLabel: "Lens character controls"
                    visible: photoControls.page === 2; Layout.fillWidth: true
                    model: ["Pupil and barrel", "Distortion and aberrations"]
                    currentIndex: photoControls.lensAberrations ? 1 : 0
                    onActivated: index => photoControls.lensAberrations = index === 1
                }
                Repeater {
                    model: [
                        {key: "apertureEdge", label: "Aperture edge", low: 0, high: 8, step: 0.1},
                        {key: "vignettingDistance", label: "Barrel distance", low: 0, high: 1, step: 0},
                        {key: "vignettingScale", label: "Barrel opening", low: 0.5, high: 4, step: 0.05},
                        {key: "sphericalDistortion", label: "Spherical distortion", low: -0.5, high: 0.5, step: 0.01},
                        {key: "astigmatism", label: "Astigmatism", low: -2, high: 2, step: 0.05},
                        {key: "fieldCurvature", label: "Field curvature", low: -2, high: 2, step: 0.05}
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        readonly property real unitScale: modelData.key === "vignettingDistance" && root.physicalCameraActive ? Number(root.physicalCamera.metresPerSceneUnit)*1000 : 1
                        readonly property real sliderMaximum: modelData.key === "vignettingDistance" ? Math.min(Number(root.lensSettings.focusDistance)*0.25, Math.max(0.01,Number(root.lensSettings.apertureRadius)*8)) : modelData.high
                        Layout.fillWidth: true
                        visible: photoControls.page === 2 && (photoControls.lensAberrations === (["sphericalDistortion","astigmatism","fieldCurvature"].indexOf(modelData.key) >= 0))
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.label + (modelData.key === "vignettingDistance" ? (root.physicalCameraActive ? " · mm" : " · scene units") : modelData.key === "vignettingScale" ? " · × aperture" : ""); color: theme.textMain; Layout.fillWidth: true }
                            ToneTextField {
                                objectName: "sceneLens_" + modelData.key
                                numericEntry: true
                                property bool userEdited: false
                                Layout.preferredWidth: 96
                                text: Number(root.lensSettings[modelData.key] === undefined ? (modelData.key === "vignettingScale" ? 1 : 0) : root.lensSettings[modelData.key]*unitScale).toPrecision(5)
                                validator: DoubleValidator { bottom: modelData.low; top: (modelData.key === "vignettingDistance" ? Number(root.lensSettings.focusDistance)*0.25 : modelData.high)*unitScale; locale: "C" }
                                Accessible.name: modelData.label
                                onTextEdited: userEdited = true
                                onEditingFinished: {
                                    if (!userEdited) return
                                    userEdited = false
                                    if (acceptableInput) root.updateLensSetting(modelData.key, Number(text)/unitScale)
                                    else root.actionError = "Enter a value within the lens control's range."
                                }
                            }
                        }
                        ToolValueSlider {
                            objectName: "sceneLensSlider_" + modelData.key
                            Layout.fillWidth: true
                            label: modelData.label
                            valueText: Number(value).toPrecision(4)
                            from: modelData.low; to: sliderMaximum*unitScale; stepSize: modelData.step
                            value: Number(root.lensSettings[modelData.key] === undefined ? (modelData.key === "vignettingScale" ? 1 : 0) : root.lensSettings[modelData.key]*unitScale)
                            onPressedChanged: pressed ? viewport.beginCameraAdjustment() : viewport.endCameraAdjustment()
                            onMoved: root.updateLensSetting(modelData.key, value/unitScale)
                        }
                    }
                }
                Label {
                    visible: photoControls.page === 2
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: photoControls.lensAberrations ? "Distortion changes framing and focus. Astigmatism and field curvature change off-axis bokeh with a nonzero aperture. Pick focus again after changing lens character."
                        : "Aperture edge brightens the rim of bokeh. Optical vignetting creates cat-eye bokeh and darker corners. Needs a nonzero aperture."
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
                    objectName: "sceneFilmModelCombo"
                    accessibleLabel: "Film rendering model"
                    visible: photoControls.page === 3 || (photoControls.page === 4 && photoControls.advancedVisible); Layout.fillWidth: true
                    model: ["Creative film", "Film plane · physical sizes"]
                    currentIndex: root.filmPlaneActive ? 1 : 0
                    onActivated: index => root.setFilmPlaneModel(index === 1)
                }
                Label {
                    visible: (photoControls.page === 3 || (photoControls.page === 4 && photoControls.advancedVisible)) && root.filmPlaneActive
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: root.physicalCameraActive ? "Grain and diffusion follow the camera’s active sensor gate."
                        : "Set the recording format below. The image is cropped to its aspect ratio."
                }
                RowLayout {
                    visible: photoControls.page === 3 && root.filmPlaneActive && !root.physicalCameraActive
                    Layout.fillWidth: true
                    ToneTextField {
                        objectName: "sceneFilmFormatWidth"
                        numericEntry: true
                        Layout.minimumWidth: 0
                        Layout.fillWidth: true; accessibleLabel: "Recording format width in millimetres"
                        text: Number(root.filmSettings.formatWidthMm || 36).toString()
                        validator: DoubleValidator { bottom: 1; top: 300; locale: "C" }
                        onEditingFinished: if (acceptableInput) root.updateFilmSetting("formatWidthMm", Number(text))
                    }
                    Label { text: "×"; color: theme.textMuted }
                    ToneTextField {
                        objectName: "sceneFilmFormatHeight"
                        numericEntry: true
                        Layout.minimumWidth: 0
                        Layout.fillWidth: true; accessibleLabel: "Recording format height in millimetres"
                        text: Number(root.filmSettings.formatHeightMm || 24).toString()
                        validator: DoubleValidator { bottom: 1; top: 300; locale: "C" }
                        onEditingFinished: if (acceptableInput) root.updateFilmSetting("formatHeightMm", Number(text))
                    }
                    Label { text: "mm"; color: theme.textMuted }
                }
                ToneComboBox {
                    objectName: "sceneFilmSectionCombo"
                    accessibleLabel: "Film effect controls"
                    visible: photoControls.page === 3; Layout.fillWidth: true
                    model: ["Grain", "Halation"]
                    currentIndex: photoControls.halationPage ? 1 : 0
                    onActivated: index => photoControls.halationPage = index === 1
                }
                ToneCheckBox {
                    objectName: "sceneHighlightHalationToggle"
                    visible: photoControls.page === 3 && photoControls.halationPage && root.filmPlaneActive
                    text: "Highlight halation"
                    checked: Boolean(root.filmSettings.highlightHalation)
                    onToggled: root.updateFilmSetting("highlightHalation", checked)
                }
                Label {
                    visible: photoControls.page === 3 && photoControls.halationPage
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: root.filmPlaneActive && !root.filmSettings.highlightHalation
                        ? "Full light return is subtle. Enable Highlight halation for glow around bright edges."
                        : "Lower the highlight cutoff to catch dimmer lights. Try Tungsten night / red glow as a starting point."
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
                Label { visible: photoControls.page === 3 && !photoControls.halationPage; text: root.filmPlaneActive ? "Grain scale  " + Number(root.filmSettings.grainMicrons).toFixed(1) + " µm" : "Grain size  " + Number(root.filmSettings.grainSize).toFixed(1); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmGrainSizeSlider"
                    label: root.filmPlaneActive ? "Grain correlation length in microns" : "Grain size at 2048 pixel long edge"
                    valueText: Number(value).toFixed(1) + (root.filmPlaneActive ? " µm" : " px")
                    visible: photoControls.page === 3 && !photoControls.halationPage; Layout.fillWidth: true
                    from: root.filmPlaneActive ? 1 : 0.5; to: root.filmPlaneActive ? 100 : 6; stepSize: 0.1
                    value: Number(root.filmPlaneActive ? root.filmSettings.grainMicrons : root.filmSettings.grainSize)
                    onMoved: root.updateFilmSetting(root.filmPlaneActive ? "grainMicrons" : "grainSize", value)
                }
                ActionButton {
                    objectName: "sceneFilmNewGrainButton"
                    visible: photoControls.page === 3 && !photoControls.halationPage
                    text: "New grain pattern"
                    onClicked: root.updateFilmSetting("seed", (Number(root.filmSettings.seed) + 1) >>> 0)
                }
                Label { visible: photoControls.page === 3 && photoControls.halationPage; text: "Halation  " + Number(root.filmSettings.halationAmount).toFixed(2); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmHalationSlider"
                    label: root.filmSettings.highlightHalation ? "Highlight glow amount" : root.filmPlaneActive ? "Returning light amount" : "Red orange highlight halation amount"
                    valueText: Number(value).toFixed(2)
                    visible: photoControls.page === 3 && photoControls.halationPage; Layout.fillWidth: true
                    from: 0; to: 1; value: Number(root.filmSettings.halationAmount)
                    onMoved: root.updateFilmSetting("halationAmount", value)
                }
                Label { visible: photoControls.page === 3 && photoControls.halationPage; text: root.filmPlaneActive ? "Diffusion scale  " + Number(root.filmSettings.halationMicrons).toFixed(1) + " µm" : "Halation spread  " + Number(root.filmSettings.halationRadius).toFixed(1); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmRadiusSlider"
                    label: root.filmPlaneActive ? "Film plane diffusion sigma in microns" : "Halation spread at 2048 pixel long edge"
                    valueText: Number(value).toFixed(1) + (root.filmPlaneActive ? " µm" : " px")
                    visible: photoControls.page === 3 && photoControls.halationPage; Layout.fillWidth: true
                    from: root.filmPlaneActive ? 1 : 0.5; to: root.filmPlaneActive ? 300 : 16; stepSize: 0.5
                    value: Number(root.filmPlaneActive ? root.filmSettings.halationMicrons : root.filmSettings.halationRadius)
                    onMoved: root.updateFilmSetting(root.filmPlaneActive ? "halationMicrons" : "halationRadius", value)
                }
                Label { visible: photoControls.page === 3 && photoControls.halationPage && (!root.filmPlaneActive || Boolean(root.filmSettings.highlightHalation)); text: "Highlight cutoff  " + Number(root.filmSettings.halationThreshold).toFixed(2); color: theme.textMain }
                ToolValueSlider {
                    objectName: "sceneFilmThresholdSlider"
                    label: "Halation threshold in linearized luminance"
                    valueText: Number(value).toFixed(2)
                    visible: photoControls.page === 3 && photoControls.halationPage && (!root.filmPlaneActive || Boolean(root.filmSettings.highlightHalation)); Layout.fillWidth: true
                    from: 0.1; to: 4; stepSize: 0.05; value: Number(root.filmSettings.halationThreshold)
                    onMoved: root.updateFilmSetting("halationThreshold", value)
                }
                Repeater {
                    model: ["Red light return", "Green light return", "Blue light return"]
                    delegate: ToolValueSlider {
                        required property int index
                        required property string modelData
                        objectName: "sceneFilmReturn" + index
                        visible: photoControls.page === 3 && photoControls.halationPage && root.filmPlaneActive
                        Layout.fillWidth: true; label: modelData
                        showCaption: true
                        from: 0; to: 1; stepSize: 0.01
                        value: root.filmPlaneActive ? Number(root.filmSettings.returnRgb[index]) : 0
                        valueText: Number(value * 100).toFixed(0) + "%"
                        onMoved: {
                            const rgb = root.filmSettings.returnRgb.slice()
                            rgb[index] = value
                            root.updateFilmSetting("returnRgb", rgb)
                        }
                    }
                }
                Label {
                    visible: photoControls.page === 3 && root.filmPlaneActive
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: "Generic density-grain and light-diffusion approximation. Micron sizes stay consistent across output resolutions; this is not a measured film stock."
                }
                ToneComboBox {
                    objectName: "scenePostSectionCombo"
                    accessibleLabel: "Camera post effect controls"
                    visible: photoControls.page === 5; Layout.fillWidth: true
                    model: ["Bloom", "Glare", "Color"]
                    currentIndex: photoControls.postPage
                    onActivated: index => { photoControls.postPage = index; photoControls.contentItem.contentY = 0 }
                }
                Repeater {
                    model: [
                        {key: "bloomAmount", label: "Bloom", group: 0, low: 0, high: 4, step: 0.05},
                        {key: "bloomRadius", label: "Bloom spread", group: 0, low: 0.5, high: 32, step: 0.5},
                        {key: "glareAmount", label: "Glare", group: 1, low: 0, high: 4, step: 0.05},
                        {key: "glareRadius", label: "Streak length", group: 1, low: 1, high: 64, step: 1},
                        {key: "glareRays", label: "Streak pairs", group: 1, low: 1, high: 8, step: 1},
                        {key: "glareRotation", label: "Streak rotation", group: 1, low: 0, high: 180, step: 1},
                        {key: "spectralIntensity", label: "Spectral color", group: 1, low: 0, high: 1, step: 0.01},
                        {key: "threshold", label: "Highlight cutoff", group: -1, low: 0, high: 16, step: 0.05},
                        {key: "chromaticAberration", label: "Chromatic fringing", group: 2, low: 0, high: 8, step: 0.05},
                        {key: "saturation", label: "Saturation", group: 2, low: 0, high: 2, step: 0.01}
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        visible: photoControls.page === 5 && (modelData.group === photoControls.postPage || (modelData.group === -1 && photoControls.postPage < 2))
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.label; color: theme.textMain; Layout.fillWidth: true }
                            ToneTextField {
                                objectName: "scenePost_" + modelData.key
                                numericEntry: true
                                property bool userEdited: false
                                Layout.preferredWidth: 80
                                text: Number(root.postSettings[modelData.key]).toFixed(modelData.step === 1 ? 0 : 2)
                                validator: DoubleValidator { bottom: modelData.low; top: modelData.high; decimals: modelData.step === 1 ? 0 : 3; locale: "C" }
                                Accessible.name: modelData.label
                                onTextEdited: userEdited = true
                                onEditingFinished: {
                                    if (!userEdited) return
                                    userEdited = false
                                    if (acceptableInput) root.updatePostSetting(modelData.key, Number(text))
                                    else root.actionError = "Enter a value from " + modelData.low + " to " + modelData.high + "."
                                }
                            }
                        }
                        ToolValueSlider {
                            objectName: "scenePostSlider_" + modelData.key
                            Layout.fillWidth: true
                            label: modelData.label
                            valueText: Number(value).toFixed(modelData.step === 1 ? 0 : 2)
                            from: modelData.low; to: modelData.high; stepSize: modelData.step
                            value: Number(root.postSettings[modelData.key])
                            onMoved: root.updatePostSetting(modelData.key, value)
                        }
                    }
                }
                Label {
                    visible: photoControls.page === 5
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: "Sizes reference a 2048 px long edge. Stop moving to resolve effects. Spectral glare uses an RGB approximation."
                }
                ActionButton {
                    objectName: "sceneRenderDetailsToggle"
                    text: photoControls.renderDetailsExpanded ? "Hide render details" : "Render details"
                    compact: true
                    visible: photoControls.page !== 4 || photoControls.advancedVisible
                    onClicked: photoControls.renderDetailsExpanded = !photoControls.renderDetailsExpanded
                }
                Label {
                    visible: photoControls.renderDetailsExpanded && (photoControls.page !== 4 || photoControls.advancedVisible)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Move freely with this panel open. Stop to resolve your photo. Saved views and exports keep these settings."
                    color: theme.textMuted
                }
                Label {
                    visible: photoControls.renderDetailsExpanded && (photoControls.page !== 4 || photoControls.advancedVisible)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: photoControls.page === 3
                        ? "Film adjustments reuse the resolved lens image. The grain pattern stays fixed per saved shot. Creative film uses 2048 px reference sizes; Film plane uses microns."
                        : root.physicalCameraActive
                        ? "Ideal thin lens with user-supplied scene scale, not a measured lens profile. F-stop changes blur, not absolute exposure. Film calibration is still in development."
                        : "Focus and aperture use scene units, not metres or calibrated f-stops. Motion uses a pinhole draft; stopping resolves the lens. Measured film profiles are still in development."
                    color: theme.textMuted
                    font.pixelSize: 12
                }
                Label { Layout.fillWidth: true; visible: root.actionError.length > 0; text: root.actionError; wrapMode: Text.WordWrap; color: theme.textMain }
            }
        }
    }
    RowLayout {
        visible: !root.depthMotionActive
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: theme.space12
        spacing: theme.space6
        ActionButton { objectName: "sceneLibraryButton"; text: "Library"; iconName: "chevron-left"; compact: true; onClicked: root.libraryScenesRequested() }
        Label { text: root.scene.partialScene ? "Partial scene · " + String(root.scene.registeredViews || 0) + " / " + String(root.scene.sourceCount || 0) + " views" : "Scene"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
        ActionButton { objectName: "sceneOpenProjectButton"; compact: true; text: "Open…"; enabled: !!root.controller && !root.scene.busy; onClicked: reopenPicker.open() }
        ActionButton { objectName: "sceneImportPlyButton"; compact: true; text: "Import PLY…"; visible: root.width >= 1200; enabled: !!root.controller && !root.scene.busy; onClicked: importPlyPicker.open() }
        ActionButton { objectName: "sceneChooseVideoButton"; compact: true; text: "Video…"; visible: root.width >= 1200 && !root.renderable; enabled: !!root.controller; onClicked: videoPicker.open() }
        ActionButton { compact: true; text: "Photos…"; visible: root.width >= 1200 && !root.renderable; enabled: !!root.controller; onClicked: capturePicker.open() }
        ActionButton { objectName: "sceneDepthMotionButton"; compact: true; text: "3D Motion"; onClicked: root.depthMotionActive = true }
        SceneRecordingControls {
            viewport: root.photoViewport
            catalog: root.catalog
            projectPath: String(root.scene.projectPath || "")
            ready: Boolean(viewport.status.frameAvailable) && !root.scene.busy
            iconOnly: root.width < 1400
        }
        ActionButton { objectName: "sceneSaveLibraryButton"; compact: true; text: "Save scene"; enabled: Boolean(viewport.status.frameAvailable) && !root.scene.busy; onClicked: saveSceneDialog.open() }
        ActionButton {
            objectName: "sceneContentsToggleButton"
            compact: true; showText: false; iconName: "panel-left"
            active: sceneWorkspaceSettings.outlinerVisible; accent: theme.textStrong
            accessibleName: sceneWorkspaceSettings.outlinerVisible ? "Hide scene contents" : "Show scene contents"
            tooltipText: accessibleName
            visible: root.renderable
            onClicked: sceneWorkspaceSettings.outlinerVisible = !sceneWorkspaceSettings.outlinerVisible
        }
        ActionButton {
            objectName: "sceneShotsToggleButton"
            compact: true; showText: false; iconName: "timeline"
            active: sceneWorkspaceSettings.shotsVisible; accent: theme.textStrong
            accessibleName: sceneWorkspaceSettings.shotsVisible ? "Hide shots and camera path" : "Show shots and camera path"
            tooltipText: accessibleName
            visible: root.renderable
            onClicked: sceneWorkspaceSettings.shotsVisible = !sceneWorkspaceSettings.shotsVisible
        }
        Item { Layout.fillWidth: true }
        ActionButton {
            objectName: "sceneObjectsButton"
            text: "Scene composition"
            visible: root.renderable && root.width > 1300 && !sceneWorkspaceSettings.inspectorVisible
            enabled: Boolean(viewport.status.frameAvailable)
            onClicked: compositionControls.open()
        }
        ActionButton {
            objectName: "sceneCleanupButton"
            text: "Clean scene"
            visible: root.renderable && root.width > theme.scaled(1100) && !sceneWorkspaceSettings.inspectorVisible
            enabled: Boolean(viewport.status.cleanupAvailable)
            onClicked: cleanupControls.open()
        }
        ActionButton {
            objectName: "scenePhotoSettingsButton"
            text: "Photo settings"
            visible: root.renderable && !sceneWorkspaceSettings.inspectorVisible
            enabled: Boolean(viewport.status.frameAvailable)
            onClicked: photoControls.visible ? photoControls.close() : photoControls.open()
        }
        ActionButton {
            objectName: "sceneTakePhotoButton"
            text: viewport.status.exporting ? "Saving…" : "Take photo"
            tone: "primary"
            visible: root.renderable
            enabled: viewport.status.previewPhase === "photo" && !viewport.status.exporting
            onClicked: { root.editPhotoAfterRender = false; photoDestination.open() }
        }
        MenuButton {
            objectName: "sceneProjectMenuButton"
            text: "Project"
            iconName: "folder"
            compact: true
            primaryOpensMenu: true
            enabled: root.controller && !root.scene.busy
            tooltipText: "Create or open a scene project, import Gaussian PLY, or return to a saved scene."
            menuItems: root.sceneProjectMenuItems()
            menuWidth: 280
            onMenuItemTriggered: function(key) { root.handleSceneProjectMenuAction(key) }
        }
        MenuButton {
            objectName: "sceneToolsMenuButton"
            text: "Tools"
            iconName: "adjust"
            compact: true
            visible: root.renderable && root.sceneToolsMenuItems().length > 0
            primaryOpensMenu: true
            tooltipText: "Open photo settings, scene composition, or clean-up tools."
            menuItems: root.sceneToolsMenuItems()
            menuWidth: 264
            onMenuItemTriggered: function(key) { root.handleSceneToolsMenuAction(key) }
        }
        ActionButton {
            objectName: "sceneOptionsButton"
            text: "Setup"
            visible: !sceneControls.visible && root.width >= theme.scaled(980)
            enabled: root.controller && !root.scene.busy
            onClicked: {
                root.showCaptureSetup = root.captureReady
                sceneControls.open()
            }
        }
        MenuButton {
            objectName: "sceneWorkspaceLayoutButton"
            text: "View"
            iconName: "grid"
            compact: true
            primaryOpensMenu: true
            tooltipText: "Choose the Scene workspace arrangement and visible panels."
            menuItems: [
                {key: "navigation", label: root.immersive ? "Show workspace navigation" : "Hide workspace navigation",
                    icon: "panel-left", checked: !root.immersive},
                {key: "studio", label: "Studio layout", icon: "grid", enabled: root.renderable},
                {key: "status", label: root.previewStatusVisible ? "Hide preview status" : "Show preview status",
                    icon: "info", checked: root.previewStatusVisible},
                {key: "viewport", label: "Photograph only", icon: "grid", enabled: root.renderable},
                {key: "outliner", label: "Scene contents", icon: "grid", checked: sceneWorkspaceSettings.outlinerVisible, enabled: root.renderable},
                {key: "inspector", label: "Inspector", icon: "adjust", checked: sceneWorkspaceSettings.inspectorVisible, enabled: root.renderable},
                {key: "shots", label: "Shots & camera path", icon: "timeline", checked: sceneWorkspaceSettings.shotsVisible, enabled: root.renderable}
            ]
            onMenuItemTriggered: key => root.handleLayoutMenuAction(key)
        }
    }
    ColumnLayout {
        parent: studioLayout.shotsHost
        anchors.fill: parent
        spacing: theme.space4
        Flow {
            objectName: "sceneCaptureActionsFlow"
            Layout.fillWidth: true
            visible: root.renderable && !root.showCaptureSetup
            spacing: theme.space8
            RowLayout {
                width: implicitWidth
                spacing: theme.space8
                Label { text: "Capture camera"; color: theme.textMain }
                ToneComboBox {
                    objectName: "sceneCaptureCameraCombo"
                    accessibleLabel: "Capture camera"
                    Layout.minimumWidth: 150
                    Layout.preferredWidth: 230
                    model: viewport.status.cameras || []
                    currentIndex: Number(viewport.status.cameraIndex || 0)
                    onActivated: viewport.selectCamera(currentIndex)
                }
                ActionButton { objectName: "sceneResetCameraButton"; text: "Reset camera"; onClicked: viewport.resetCamera() }
            }
            RowLayout {
                width: implicitWidth
                spacing: theme.space8
                Label { text: "Export edge"; color: theme.textMain }
                ToneComboBox {
                    id: exportEdge
                    objectName: "sceneExportEdge"
                    accessibleLabel: "Scene export image edge"
                    Layout.minimumWidth: 88
                    Layout.preferredWidth: 104
                    model: ["800", "1600", "4096", "6000", "8192"]
                    currentIndex: 2
                }
                ActionButton {
                    objectName: "sceneExportPhotoButton"
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
        }
        Flow {
            objectName: "sceneSavedViewActionsFlow"
            Layout.fillWidth: true
            visible: root.renderable && !root.showCaptureSetup
            spacing: theme.space8
            RowLayout {
                width: implicitWidth
                spacing: theme.space8
                Label { text: "Saved views"; color: theme.textMain }
                ToneComboBox {
                    objectName: "sceneSavedViews"
                    accessibleLabel: "Recall saved camera view"
                    Layout.minimumWidth: 180
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
            }
        }
        Flow {
            objectName: "sceneCameraPathActionsFlow"
            Layout.fillWidth: true
            visible: root.renderable && !root.showCaptureSetup && (!root.spacetimeProject || Number(viewport.status.pathDuration || 0) > 0)
            spacing: theme.space8
            ActionButton {
                objectName: "sceneCameraPathButton"
                text: "Camera path…"
                enabled: (viewport.status.savedViews || []).length >= 2
                onClicked: pathPopup.open()
            }
            ActionButton {
                objectName: "sceneCameraPathPlayButton"
                text: viewport.status.pathPlaying ? "Pause" : "Play"
                enabled: Number(viewport.status.pathDuration || 0) > 0
                onClicked: viewport.playCameraPath(!viewport.status.pathPlaying)
            }
            ToolValueSlider {
                objectName: "sceneCameraPathSlider"
                label: "Camera path position"
                valueText: Number(value).toFixed(1) + " s"
                width: Math.max(theme.scaled(150), parent.width - theme.scaled(500))
                from: 0; to: Number(viewport.status.pathDuration || 0)
                value: Number(viewport.status.pathSeconds || 0)
                enabled: to > 0
                onMoved: { viewport.playCameraPath(false); viewport.seekCameraPath(value) }
            }
            Label {
                objectName: "sceneCameraPathTimeLabel"
                color: theme.textMuted
                text: viewport.status.filmExporting
                    ? String(viewport.status.filmFramesCompleted || 0) + "/" + String(viewport.status.filmFrameCount || 0) + " frames"
                    : Number(viewport.status.pathSeconds || 0).toFixed(1) + " / " + Number(viewport.status.pathDuration || 0).toFixed(1) + " s"
            }
            ActionButton {
                objectName: "sceneFilmExportButton"
                text: viewport.status.filmExporting ? "Cancel film" : "Export film"
                visible: !root.spacetimeProject
                enabled: Boolean(viewport.status.filmExporting) || (Number(viewport.status.pathDuration || 0) > 0 && Number(exportEdge.currentText) <= 8192)
                disabledReason: "Save at least two shots and a camera path first."
                onClicked: { if (viewport.status.filmExporting) viewport.cancelFilm(); else filmExportOptions.open() }
            }
        }
    }
    Rectangle {
        id: scenePreviewStatusSurface
        objectName: "scenePreviewStatusSurface"
        parent: studioLayout.viewportHost
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: Math.min(parent.width - theme.space24, liveStatus.implicitWidth + theme.space24)
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
                : root.cleanupSelectionArmed ? ((root.cleanupKeepInside ? "KEEP" : "REMOVE") + (root.cleanupLassoMode ? " LASSO · Trace an outline and release" : " RECTANGLE · Drag in the image")
                    + (root.cleanupDepthLimited ? " · Depth " + Number(root.cleanupNearDepth).toPrecision(4) + "–" + Number(root.cleanupFarDepth).toPrecision(4) + " scene units" : "") + " · Esc to cancel")
                : viewport.status.cleanupBusy ? "Saving scene cleanup…"
                : root.focusPickArmed ? (root.actionError || "PICK FOCUS · Click a solid surface · Esc to cancel")
                : viewport.status.focusPicking ? "Finding surface focus…"
                : viewport.status.focusPickError ? String(viewport.status.focusPickError)
                : viewport.status.exporting ? (Number(viewport.status.exportTilesTotal || 0) > 0
                    && Number(viewport.status.exportTilesCompleted || 0) < Number(viewport.status.exportTilesTotal)
                    ? "RENDERING PHOTO · " + Number(viewport.status.exportTilesCompleted || 0) + "/" + Number(viewport.status.exportTilesTotal) + " tiles"
                    : viewport.status.softLightRefining ? "RENDERING LIGHTS…" : "SAVING PHOTO…")
                : viewport.status.previewPhase === "live" ? "Live preview"
                : viewport.status.previewPhase === "refining" ? (viewport.status.softLightRefining
                    ? (Number(viewport.status.lightingPreviewSamples || 0) > 0
                        ? (Number(viewport.status.lightingPreviewSamples) >= Number(viewport.status.lightingPreviewTargetSamples) && Number(viewport.status.refinementSamplesTotal || 1) > 1
                            ? "Resolving lens · " + Number(viewport.status.refinementSamplesCompleted || 0) + "/" + Number(viewport.status.refinementSamplesTotal) + " samples"
                            : "Resolving lights · " + Number(viewport.status.lightingPreviewSamples) + "/" + Number(viewport.status.lightingPreviewTargetSamples) + " samples")
                        : "Resolving lights…")
                    : Number(viewport.status.refinementSamplesTotal || 1) > 1
                    ? "Resolving lens · " + Number(viewport.status.refinementSamplesCompleted || 0) + "/" + Number(viewport.status.refinementSamplesTotal) + " samples"
                    : "Resolving photo…")
                : "Photo preview"
        }
        HoverHandler { id: sceneStatusHover }
        AppToolTip {
            targetItem: scenePreviewStatusSurface
            visible: sceneStatusHover.hovered
            text: root.focusPickArmed ? "Click a solid surface to focus. Press Escape to cancel."
                : "Drag to look. Right-drag to pan. Scroll or W/S to move. Alt-drag rolls; double-click a surface to orbit. R resets."
        }
    }
}
