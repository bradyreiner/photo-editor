pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

SceneInspectorPanel {
    id: panel
    required property var viewport
    signal importMeshRequested()
    property bool preserveMeshCoordinates: false
    property bool importMaterials: true
    property int materialIndex: 0
    property string lastImportedId: ""
    property int section: 0
    property string selectedId: ""
    property string errorMessage: ""
    property bool lightingDetailsExpanded: false
    property bool outputDetailsExpanded: false
    readonly property var composition: viewport ? viewport.status.sceneComposition : ({objects: [], lights: [], ambient: 0.15})
    readonly property var entries: (section === 0 ? composition.objects : section === 1 ? composition.lights : section === 4 ? (composition.objects || []).filter(item => Boolean(item.materialSetSha256)) : []) || []
    readonly property var captureSettings: composition.captureLighting || ({mode: "captured", power: 1, shadowStrength: 1})
    readonly property var hazeSettings: composition.haze || ({density: 0, color: [0.7, 0.8, 1], distance: 1000, anisotropy: 0})
    readonly property var environmentSettings: composition.environment || ({diffuse: false, strength: 1})
    readonly property var selected: {
        for (let i = 0; i < entries.length; ++i) if (entries[i].id === selectedId) return entries[i]
        return ({})
    }
    readonly property var selectedSurface: (selected.materials || [])[materialIndex] || ({})
    property string surfaceObjectId: ""
    onSelectedChanged: {
        const id = String(selected.id || "")
        if (id !== surfaceObjectId) { surfaceObjectId = id; materialIndex = (selected.materials || []).length > 1 ? 1 : 0 }
    }
    onSectionChanged: selectedId = section === 4 && entries.length ? String(entries[0].id) : ""
    objectName: "sceneCompositionPopup"
    padding: theme.space16
    Theme { id: theme }
    function apply(next: var): void {
        const receiver = next.captureLighting && next.captureLighting.mode === "shadow-receiver"
        if (receiver) next.hazeShadows = Boolean(next.hazeShadows)
        if (next.captureEmission !== undefined || next.captureEmissionSampling !== undefined || next.hazeShadows !== undefined) {
            next.schema = receiver ? "looksy-scene-composition-v17" : next.hazeShadows !== undefined ? "looksy-scene-composition-v16" : next.captureEmissionSampling !== undefined ? "looksy-scene-composition-v15" : "looksy-scene-composition-v14"
            if (next.hazeShadows !== undefined) next.captureEmissionSampling = next.captureEmissionSampling || "cosine"
            next.captureEmission = Boolean(next.captureEmission)
            next.reflections = Boolean(next.reflections)
            next.captureLighting = next.captureLighting || JSON.parse(JSON.stringify(captureSettings))
            next.haze = next.haze || JSON.parse(JSON.stringify(hazeSettings))
            next.environment = next.environment || Object.assign({}, environmentSettings)
            next.lightSamples = Number(next.lightSamples || 64)
            next.alphaModel = next.alphaModel || "brush-alpha-0999-v1"
            next.scanShadows = Boolean(next.scanShadows)
            next.reflectionMode = next.reflectionMode || "probe"
            next.illuminationMode = next.illuminationMode || "probe"
            next.surfaceBounces = Number(next.surfaceBounces || 1)
        }
        errorMessage = String(viewport.setSceneComposition(next).error || "")
    }
    function updateEffect(group: string, key: string, value: var): void {
        const next = JSON.parse(JSON.stringify(composition))
        next.schema = composition.schema === "looksy-scene-composition-v13" ? "looksy-scene-composition-v13" : composition.schema === "looksy-scene-composition-v12" ? "looksy-scene-composition-v12" : composition.schema === "looksy-scene-composition-v11" ? "looksy-scene-composition-v11" : composition.schema === "looksy-scene-composition-v10" ? "looksy-scene-composition-v10" : composition.schema === "looksy-scene-composition-v9" ? "looksy-scene-composition-v9" : composition.schema === "looksy-scene-composition-v8" ? "looksy-scene-composition-v8" : composition.schema === "looksy-scene-composition-v7" ? "looksy-scene-composition-v7" : composition.schema === "looksy-scene-composition-v6" ? "looksy-scene-composition-v6" : composition.schema === "looksy-scene-composition-v5" ? "looksy-scene-composition-v5" : next.objects.some(item => item.kind === "mesh") ? "looksy-scene-composition-v4" : "looksy-scene-composition-v3"; next.reflections = Boolean(next.reflections)
        next.captureLighting = JSON.parse(JSON.stringify(captureSettings)); next.haze = JSON.parse(JSON.stringify(hazeSettings))
        next[group][key] = value; apply(next)
    }
    function update(changes: var): void {
        const next = JSON.parse(JSON.stringify(composition))
        const items = section === 0 || section === 4 ? next.objects : next.lights
        for (let i = 0; i < items.length; ++i) if (items[i].id === selectedId) { items[i] = Object.assign({}, items[i], changes); apply(next); return }
    }
    function sampledComposition(): var {
        const next = JSON.parse(JSON.stringify(composition)); next.schema = composition.schema === "looksy-scene-composition-v13" ? "looksy-scene-composition-v13" : composition.schema === "looksy-scene-composition-v12" ? "looksy-scene-composition-v12" : composition.schema === "looksy-scene-composition-v11" ? "looksy-scene-composition-v11" : composition.schema === "looksy-scene-composition-v10" ? "looksy-scene-composition-v10" : composition.schema === "looksy-scene-composition-v9" ? "looksy-scene-composition-v9" : next.alphaModel ? "looksy-scene-composition-v8" : next.environment ? "looksy-scene-composition-v7" : "looksy-scene-composition-v6"
        next.reflections = Boolean(next.reflections); next.captureLighting = JSON.parse(JSON.stringify(captureSettings)); next.haze = JSON.parse(JSON.stringify(hazeSettings)); next.lightSamples = Number(next.lightSamples || 64)
        return next
    }
    function updateEnvironment(key: string, value: var): void {
        const next = sampledComposition(); next.schema = (next.schema === "looksy-scene-composition-v13" || next.schema === "looksy-scene-composition-v12" || next.schema === "looksy-scene-composition-v11" || next.schema === "looksy-scene-composition-v10" || next.schema === "looksy-scene-composition-v9") ? next.schema : next.alphaModel ? "looksy-scene-composition-v8" : "looksy-scene-composition-v7"
        next.environment = Object.assign({}, environmentSettings); next.environment[key] = value
        apply(next)
    }
    function updateAlphaModel(index: int): void {
        const next = sampledComposition(); next.schema = (next.schema === "looksy-scene-composition-v13" || next.schema === "looksy-scene-composition-v12" || next.schema === "looksy-scene-composition-v11" || next.schema === "looksy-scene-composition-v10" || next.schema === "looksy-scene-composition-v9") ? next.schema : "looksy-scene-composition-v8"
        next.environment = Object.assign({}, environmentSettings)
        next.alphaModel = index === 1 ? "inria-alpha-099-v1" : "brush-alpha-0999-v1"
        apply(next)
    }
    function showLightSource(show: bool): void {
        const next = sampledComposition(); next.schema = Number(next.surfaceBounces || 1) > 1 ? "looksy-scene-composition-v13" : next.illuminationMode === "spatial" ? "looksy-scene-composition-v12" : next.reflectionMode === "spatial" ? "looksy-scene-composition-v11" : next.scanShadows ? "looksy-scene-composition-v10" : "looksy-scene-composition-v9"
        next.environment = Object.assign({}, environmentSettings)
        next.alphaModel = next.alphaModel || "brush-alpha-0999-v1"
        for (let i = 0; i < next.lights.length; ++i) if (next.lights[i].id === selectedId) {
            if (show) next.lights[i].cameraVisible = true
            else delete next.lights[i].cameraVisible
            apply(next); return
        }
    }
    function toggleScanShadows(): void {
        const next = sampledComposition(); next.schema = Number(next.surfaceBounces || 1) > 1 ? "looksy-scene-composition-v13" : next.illuminationMode === "spatial" ? "looksy-scene-composition-v12" : next.reflectionMode === "spatial" ? "looksy-scene-composition-v11" : "looksy-scene-composition-v10"
        next.environment = Object.assign({}, environmentSettings)
        next.alphaModel = next.alphaModel || "brush-alpha-0999-v1"
        next.scanShadows = !Boolean(composition.scanShadows)
        apply(next)
    }
    function updateReflectionMode(index: int): void {
        const next = sampledComposition(); next.schema = Number(next.surfaceBounces || 1) > 1 ? "looksy-scene-composition-v13" : next.illuminationMode === "spatial" ? "looksy-scene-composition-v12" : "looksy-scene-composition-v11"
        next.environment = Object.assign({}, environmentSettings)
        next.alphaModel = next.alphaModel || "brush-alpha-0999-v1"
        next.scanShadows = Boolean(next.scanShadows)
        next.reflectionMode = index === 1 ? "spatial" : "probe"
        apply(next)
    }
    function updateIlluminationMode(index: int): void {
        const next = sampledComposition(); next.schema = Number(next.surfaceBounces || 1) > 1 ? "looksy-scene-composition-v13" : "looksy-scene-composition-v12"
        next.environment = Object.assign({}, environmentSettings)
        next.alphaModel = next.alphaModel || "brush-alpha-0999-v1"
        next.scanShadows = Boolean(next.scanShadows)
        next.reflectionMode = next.reflectionMode || "probe"
        next.illuminationMode = index === 1 ? "spatial" : "probe"
        apply(next)
    }
    function updateSurfaceBounces(count: int): void {
        const next = sampledComposition(); next.schema = "looksy-scene-composition-v13"
        next.environment = Object.assign({}, environmentSettings)
        next.alphaModel = next.alphaModel || "brush-alpha-0999-v1"
        next.scanShadows = Boolean(next.scanShadows)
        next.reflectionMode = next.reflectionMode || "probe"
        next.illuminationMode = next.illuminationMode || "probe"
        next.surfaceBounces = count; apply(next)
    }
    function updateLightShape(key: string, value: real): void {
        const next = sampledComposition()
        for (let i = 0; i < next.lights.length; ++i) if (next.lights[i].id === selectedId) {
            if (key === "width" || key === "height") next.lights[i].size[key === "width" ? 0 : 1] = value
            else if (key === "angularRadius") { if (value === 0) delete next.lights[i].angularRadius; else next.lights[i].angularRadius = value }
            else next.lights[i][key] = value
            apply(next); return
        }
    }
    function updateSurface(key: string, value: var): void {
        if (!selected.id || !selectedSurface.name) return
        const next = JSON.parse(JSON.stringify(composition))
        for (let i = 0; i < next.objects.length; ++i) if (next.objects[i].id === selectedId) {
            next.objects[i].materials[materialIndex][key] = value; apply(next); return
        }
    }
    function linearToDisplay(value: real): real { return value <= 0.0031308 ? value * 12.92 : 1.055 * Math.pow(value, 1 / 2.4) - 0.055 }
    function displayToLinear(value: real): real { return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4) }
    function updateVector(key: string, axis: int, value: real): void {
        if (!selected.id || !isFinite(value)) return
        const vector = selected[key].slice(); vector[axis] = value
        const changes = {}; changes[key] = vector; update(changes)
    }
    function add(kind: string): void {
        const result = section === 0 ? viewport.addSceneObject(kind) : viewport.addSceneLight(kind)
        errorMessage = String(result.error || ""); if (result.ok) selectedId = String(result.id)
    }
    function removeSelected(): void {
        const next = JSON.parse(JSON.stringify(composition))
        if (section === 0) next.objects = next.objects.filter(item => item.id !== selectedId)
        else next.lights = next.lights.filter(item => item.id !== selectedId)
        apply(next); if (!errorMessage.length) selectedId = ""
    }
    Connections {
        target: panel.viewport
        function onStatusChanged(): void {
            const result = panel.viewport.status.meshImportResult || ({})
            if (result.id && result.id !== panel.lastImportedId) { panel.lastImportedId = String(result.id); panel.selectedId = String(result.id); panel.errorMessage = "" }
        }
    }
    contentItem: Flickable {
        objectName: "sceneCompositionScroll"
        clip: true; contentWidth: width; contentHeight: contents.implicitHeight
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: contents
            width: parent.width; spacing: 12
            ToneComboBox {
                objectName: "sceneCompositionSection"; Layout.fillWidth: true
                accessibleLabel: "Scene composition section"
                model: ["Scene objects", "Light setup", "Captured scene", "Atmosphere", "Materials"]; currentIndex: panel.section
                onActivated: panel.section = currentIndex
            }
            RowLayout {
                visible: panel.section < 2; Layout.fillWidth: true
                ActionButton { objectName: "sceneAddSphereButton"; visible: panel.section === 0; text: "Sphere"; onClicked: panel.add("sphere") }
                ActionButton { objectName: "sceneAddBoxButton"; visible: panel.section === 0; text: "Box"; onClicked: panel.add("box") }
                ActionButton { objectName: "sceneAddFloorButton"; visible: panel.section === 0; text: "Floor"; onClicked: panel.add("floor") }
                ActionButton { objectName: "sceneAddDirectionalLightButton"; visible: panel.section === 1; text: "Directional"; onClicked: panel.add("directional") }
                ActionButton { objectName: "sceneAddPointLightButton"; visible: panel.section === 1; text: "Point light"; onClicked: panel.add("point") }
                ActionButton { objectName: "sceneAddRectangleLightButton"; visible: panel.section === 1; text: "Rectangle"; onClicked: panel.add("rectangle") }
                ActionButton { objectName: "sceneAddSphereLightButton"; visible: panel.section === 1; text: "Sphere"; onClicked: panel.add("sphere") }
            }
            ColumnLayout {
                visible: panel.section === 0; Layout.fillWidth: true; spacing: 8
                ActionButton {
                    objectName: "sceneImportMeshButton"; text: panel.viewport.status.importingMesh ? "Importing geometry…" : "Import OBJ…"
                    enabled: !panel.viewport.status.importingMesh
                    onClicked: panel.importMeshRequested()
                }
                ActionButton {
                    objectName: "sceneMeshCoordinatesButton"
                    text: panel.preserveMeshCoordinates ? "Placement · Original coordinates" : "Placement · Fit in front of camera"
                    onClicked: panel.preserveMeshCoordinates = !panel.preserveMeshCoordinates
                    tooltipText: "Keep original coordinates for reconstructed geometry aligned to this capture. Otherwise fit the object in front of the current camera."
                }
                ActionButton {
                    objectName: "sceneMeshMaterialsButton"
                    text: panel.importMaterials ? "Import · Geometry & materials" : "Import · Geometry only"
                    onClicked: panel.importMaterials = !panel.importMaterials
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                    text: "OBJ geometry and color images are copied into this project. Basic surfaces are supported; normal, displacement and transparency maps are not applied."
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMain
                    visible: Boolean((panel.viewport.status.meshImportResult || {}).error)
                    text: String((panel.viewport.status.meshImportResult || {}).error || "")
                }
            }
            RowLayout {
                visible: panel.section === 1; Layout.fillWidth: true
                Label { text: "Ambient fill"; color: theme.textMain }
                ToneTextField {
                    objectName: "sceneCompositionAmbient"; Layout.fillWidth: true; numericEntry: true
                    text: Number(panel.composition.ambient).toPrecision(5)
                    Accessible.name: "Ambient fill for scene lighting"
                    property bool userEdited: false
                    validator: DoubleValidator { bottom: 0; top: 10; locale: "C" }
                    onTextEdited: userEdited = true
                    onEditingFinished: {
                        if (userEdited) { userEdited = false; if (acceptableInput) { const next = JSON.parse(JSON.stringify(panel.composition)); next.ambient = Number(text); panel.apply(next) } }
                    }
                }
            }
            ActionButton {
                objectName: "sceneCompositionReflectionsButton"
                visible: panel.section === 0
                text: panel.composition.reflections ? "Scan reflections · On" : "Scan reflections · Off"
                onClicked: {
                    const next = JSON.parse(JSON.stringify(panel.composition))
                    if (next.schema === "looksy-scene-composition-v13" || next.schema === "looksy-scene-composition-v12" || next.schema === "looksy-scene-composition-v11" || next.schema === "looksy-scene-composition-v10" || next.schema === "looksy-scene-composition-v3" || next.schema === "looksy-scene-composition-v4" || next.schema === "looksy-scene-composition-v5" || next.schema === "looksy-scene-composition-v6" || next.schema === "looksy-scene-composition-v7" || next.schema === "looksy-scene-composition-v8" || next.schema === "looksy-scene-composition-v9") next.reflections = !next.reflections
                    else if (next.reflections) { delete next.reflections; next.schema = "looksy-scene-composition-v1" }
                    else { next.reflections = true; next.schema = "looksy-scene-composition-v2" }
                    panel.apply(next)
                }
                tooltipText: "Reflect the captured surroundings in added objects. Choose Spatial rays for nearby detail and depth."
            }
            ToneComboBox {
                objectName: "sceneReflectionModeCombo"
                visible: panel.section === 0 && Boolean(panel.composition.reflections)
                Layout.fillWidth: true
                accessibleLabel: "Reflection method"
                model: ["Environment probe", "Spatial rays"]
                currentIndex: panel.composition.reflectionMode === "spatial" ? 1 : 0
                onActivated: panel.updateReflectionMode(currentIndex)
            }
            ToneComboBox {
                objectName: "sceneReflectionSamples"
                visible: panel.section === 0 && Boolean(panel.composition.reflections) && panel.composition.reflectionMode === "spatial"
                Layout.fillWidth: true
                accessibleLabel: "Reflection samples"
                model: ["16 reflection samples", "32 reflection samples", "64 reflection samples", "128 reflection samples", "256 reflection samples", "512 reflection samples", "1024 reflection samples"]
                currentIndex: [16,32,64,128,256,512,1024].indexOf(Number(panel.composition.lightSamples || 64))
                onActivated: { const next = panel.sampledComposition(); next.lightSamples = [16,32,64,128,256,512,1024][currentIndex]; panel.apply(next) }
            }
            Label {
                visible: panel.section === 0 && Boolean(panel.composition.reflections) && panel.composition.reflectionMode === "spatial"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                text: "Spatial rays follow nearby scan details and transparency. More samples smooth rough reflections and also set scene lighting quality. Increase Surface bounces for repeated reflections and indirect light between added objects."
            }
            ActionButton {
                objectName: "sceneCompositionEnvironmentButton"
                visible: panel.section === 1
                text: panel.environmentSettings.diffuse ? "Scan illumination · On" : "Scan illumination · Off"
                tooltipText: "Light added surfaces from the captured surroundings. Choose Spatial rays to sample nearby detail and emission from added objects."
                onClicked: panel.updateEnvironment("diffuse", !panel.environmentSettings.diffuse)
            }
            ToneComboBox {
                objectName: "sceneIlluminationModeCombo"
                visible: panel.section === 1 && Boolean(panel.environmentSettings.diffuse)
                Layout.fillWidth: true
                accessibleLabel: "Captured illumination method"
                model: ["Environment probe", "Spatial rays"]
                currentIndex: panel.composition.illuminationMode === "spatial" ? 1 : 0
                onActivated: panel.updateIlluminationMode(currentIndex)
            }
            Label {
                visible: panel.section === 1 && Boolean(panel.environmentSettings.diffuse) && panel.composition.illuminationMode === "spatial"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                text: "Spatial rays sample captured light and emission from added objects at each surface. Increase Surface bounces for indirect light through more added surfaces. More bounces and samples take longer to render."
            }
            ToneComboBox {
                objectName: "sceneSurfaceBouncesCombo"
                visible: (panel.section === 0 || panel.section === 1) && ((Boolean(panel.composition.reflections) && panel.composition.reflectionMode === "spatial") || (Boolean(panel.environmentSettings.diffuse) && panel.composition.illuminationMode === "spatial"))
                Layout.fillWidth: true
                accessibleLabel: "Surface bounces"
                model: ["1 surface bounce", "2 surface bounces", "3 surface bounces", "4 surface bounces", "5 surface bounces", "6 surface bounces", "7 surface bounces", "8 surface bounces"]
                currentIndex: Number(panel.composition.surfaceBounces || 1) - 1
                onActivated: panel.updateSurfaceBounces(currentIndex + 1)
            }
            ActionButton {
                objectName: "sceneScanShadowsButton"
                visible: panel.section === 1
                text: panel.composition.scanShadows ? "Scan shadows · On" : "Scan shadows · Off"
                tooltipText: "Let the scan block direct and probe lighting on added objects. Spatial rays always follow captured opacity."
                onClicked: panel.toggleScanShadows()
            }
            RowLayout {
                visible: panel.section === 1 && panel.environmentSettings.diffuse
                Layout.fillWidth: true
                Label { text: panel.composition.illuminationMode === "spatial" ? "Indirect light strength" : "Scan light strength"; color: theme.textMain }
                ToneTextField {
                    objectName: "sceneEnvironmentStrength"; Layout.fillWidth: true; numericEntry: true
                    text: Number(panel.environmentSettings.strength).toPrecision(5)
                    Accessible.name: "Captured environment light strength"
                    property bool userEdited: false
                    validator: DoubleValidator { bottom: 0; top: 10; locale: "C" }
                    onTextEdited: userEdited = true
                    onEditingFinished: if (userEdited) {
                        userEdited = false
                        if (acceptableInput) panel.updateEnvironment("strength", Number(text))
                    }
                }
            }
            ToneComboBox {
                objectName: "sceneCompositionSelection"; visible: panel.section < 2 || panel.section === 4; Layout.fillWidth: true
                accessibleLabel: "Selected scene object or light"
                model: panel.entries; textRole: "name"; valueRole: "id"
                currentIndex: { for (let i = 0; i < panel.entries.length; ++i) if (panel.entries[i].id === panel.selectedId) return i; return -1 }
                displayText: currentIndex < 0 ? "Choose an item…" : currentText
                onActivated: panel.selectedId = String(currentValue)
            }
            Label {
                visible: panel.section < 2 && !panel.selected.id; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                text: panel.section === 0 ? "Add an object in front of the current camera, then adjust its position and size in scene units." : "Lights affect added objects and the optional captured-color lighting mode."
            }
            ToneTextField {
                objectName: "sceneCompositionName"; Layout.fillWidth: true; visible: panel.section < 2 && Boolean(panel.selected.id)
                text: String(panel.selected.name || ""); maximumLength: 80
                Accessible.name: "Object or light name"
                property bool userEdited: false
                onTextEdited: userEdited = true
                onEditingFinished: { if (userEdited) { userEdited = false; panel.update({name: text}) } }
            }
            ActionButton {
                objectName: "sceneLightCameraVisibleButton"
                visible: panel.section === 1 && (panel.selected.kind === "rectangle" || panel.selected.kind === "sphere")
                text: panel.selected.cameraVisible ? "Hide source from camera" : "Show source in camera"
                tooltipText: "Show the emitting surface, including depth of field and focus picking. Illumination stays the same; rectangle emission faces local +Z."
                onClicked: panel.showLightSource(!panel.selected.cameraVisible)
            }
            Repeater {
                model: !panel.selected.id || panel.section >= 2 ? [] : panel.section === 0 ? ["position", "size", "rotation", "color"] : panel.selected.kind === "rectangle" ? ["vector", "rotation", "color"] : ["vector", "color"]
                delegate: ColumnLayout {
                    id: vectorGroup
                    required property string modelData
                    Layout.fillWidth: true; spacing: 6
                    Label {
                        color: theme.textMain
                        text: ({position: "Position · XYZ scene units", size: panel.selected.kind === "mesh" ? "Mesh scale · XYZ" : "Half extents / radii · XYZ scene units", rotation: "Rotation · XYZ degrees", color: "Color · RGB"})[vectorGroup.modelData]
                            || (panel.selected.kind === "directional" ? "Direction toward light · XYZ" : "Light position · scene units")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Repeater {
                            model: 3
                            delegate: ToneTextField {
                                required property int index
                                property bool userEdited: false
                                Layout.fillWidth: true; Layout.preferredWidth: 70
                                objectName: "sceneComposition_" + vectorGroup.modelData + "_" + index
                                numericEntry: true
                                text: Number((panel.selected[vectorGroup.modelData] || [0, 0, 0])[index]).toPrecision(6)
                                Accessible.name: vectorGroup.modelData + " " + ["X", "Y", "Z"][index]
                                validator: DoubleValidator { bottom: vectorGroup.modelData === "color" ? 0 : vectorGroup.modelData === "size" ? 0.0001 : vectorGroup.modelData === "rotation" ? -360 : -100000000; top: vectorGroup.modelData === "color" ? 1 : vectorGroup.modelData === "rotation" ? 360 : vectorGroup.modelData === "size" ? 1000000 : 100000000; locale: "C" }
                                onTextEdited: userEdited = true
                                onEditingFinished: { if (userEdited) { userEdited = false; if (acceptableInput) panel.updateVector(vectorGroup.modelData, index, Number(text)); else panel.errorMessage = "Enter a valid value in the displayed units." } }
                            }
                        }
                    }
                }
            }
            Repeater {
                model: !panel.selected.id || panel.section >= 2 || (panel.section === 0 && panel.selected.materialSetSha256 && panel.selected.useImportedMaterials) ? [] : panel.section === 0 ? ["roughness", "metallic", "emission"] : ["intensity"]
                delegate: RowLayout {
                    id: materialRow
                    required property string modelData
                    Layout.fillWidth: true
                    Label { color: theme.textMain; text: ({roughness: "Roughness", metallic: "Metallic", emission: "Emission", intensity: panel.selected.kind === "rectangle" || panel.selected.kind === "sphere" ? "Radiance" : "Intensity"})[materialRow.modelData] }
                    ToneTextField {
                        property bool userEdited: false
                        objectName: "sceneComposition_" + materialRow.modelData; Layout.fillWidth: true; numericEntry: true
                        text: Number(panel.selected[materialRow.modelData] || 0).toPrecision(5)
                        Accessible.name: materialRow.modelData
                        validator: DoubleValidator { bottom: materialRow.modelData === "roughness" ? 0.04 : 0; top: materialRow.modelData === "intensity" ? 10000 : materialRow.modelData === "emission" ? 100 : 1; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: { if (userEdited) { userEdited = false; if (acceptableInput) { const changes = {}; changes[materialRow.modelData] = Number(text); panel.update(changes) } } }
                    }
                }
            }
            Repeater {
                model: panel.section !== 1 || !panel.selected.id ? [] : panel.selected.kind === "directional" ? ["angularRadius"] : panel.selected.kind === "sphere" ? ["radius"] : panel.selected.kind === "rectangle" ? ["width", "height"] : []
                delegate: RowLayout {
                    id: lightShapeRow
                    required property string modelData
                    Layout.fillWidth: true
                    Label { color: theme.textMain; text: ({angularRadius: "Sun radius · degrees", radius: "Radius · scene units", width: "Width · scene units", height: "Height · scene units"})[lightShapeRow.modelData] }
                    ToneTextField {
                        objectName: "sceneLight_" + lightShapeRow.modelData; Layout.fillWidth: true; numericEntry: true
                        text: Number(lightShapeRow.modelData === "width" ? (panel.selected.size || [1,1])[0] : lightShapeRow.modelData === "height" ? (panel.selected.size || [1,1])[1] : panel.selected[lightShapeRow.modelData] || 0).toPrecision(6)
                        Accessible.name: lightShapeRow.modelData
                        property bool userEdited: false
                        validator: DoubleValidator { bottom: lightShapeRow.modelData === "angularRadius" ? 0 : 0.0001; top: lightShapeRow.modelData === "angularRadius" ? 89 : 1000000; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: { if (userEdited) { userEdited = false; if (acceptableInput) panel.updateLightShape(lightShapeRow.modelData,Number(text)) } }
                    }
                }
            }
            ToneComboBox {
                objectName: "sceneLightSamples"; visible: panel.section === 1 || (panel.section === 2 && Boolean(panel.composition.captureEmission)) || (panel.section === 3 && Boolean(panel.composition.hazeShadows)); Layout.fillWidth: true
                accessibleLabel: "Light and environment samples"
                model: ["16 light samples", "32 light samples", "64 light samples", "128 light samples", "256 light samples", "512 light samples", "1024 light samples"]
                currentIndex: [16,32,64,128,256,512,1024].indexOf(Number(panel.composition.lightSamples || 64))
                onActivated: { const next = panel.sampledComposition(); next.lightSamples = [16,32,64,128,256,512,1024][currentIndex]; panel.apply(next) }
            }
            StudioCollapsibleGroup {
                objectName: "sceneLightingDetails"
                visible: panel.section === 1
                title: "Light behavior details"
                expanded: panel.lightingDetailsExpanded
                onToggled: panel.lightingDetailsExpanded = !panel.lightingDetailsExpanded
                Label {
                    objectName: "sceneLightingDetailsText"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: theme.textMuted
                    text: "Larger emitters soften shadows and highlights. Area radiance stays fixed as size changes; more area emits more light. Rectangle emission faces local +Z. Show source controls camera visibility. Navigation uses 8 samples; still and film use the selected quality."
                }
            }
            RowLayout {
                visible: panel.section < 2 && Boolean(panel.selected.id); Layout.fillWidth: true
                ActionButton { objectName: "sceneCompositionVisibilityButton"; text: panel.selected.visible ? "Hide item" : "Show item"; onClicked: panel.update({visible: !panel.selected.visible}) }
                ActionButton { objectName: "sceneCompositionDeleteButton"; text: "Remove item"; onClicked: panel.removeSelected() }
            }
            Label {
                visible: panel.section === 2; color: theme.textMain
                text: "Splat opacity convention"
            }
            ToneComboBox {
                objectName: "sceneAlphaModelCombo"; visible: panel.section === 2; Layout.fillWidth: true
                accessibleLabel: "Splat opacity convention"
                model: ["Brush opacity", "Original 3DGS opacity"]
                currentIndex: panel.composition.alphaModel === "inria-alpha-099-v1" ? 1 : 0
                onActivated: index => panel.updateAlphaModel(index)
                Accessible.description: "Match the opacity cap used to train this splat. Existing projects use Brush."
            }
            ToneComboBox {
                objectName: "sceneCaptureLightingMode"; visible: panel.section === 2; Layout.fillWidth: true
                accessibleLabel: "Captured scene lighting mode"
                model: ["Captured appearance", "Light captured colors", "Receive shadows"]
                currentIndex: panel.captureSettings.mode === "shadow-receiver" ? 2 : panel.captureSettings.mode === "lit-color" ? 1 : 0
                onActivated: panel.updateEffect("captureLighting", "mode", currentIndex === 2 ? "shadow-receiver" : currentIndex === 1 ? "lit-color" : "captured")
            }
            ToneCheckBox {
                objectName: "sceneCaptureEmission"
                visible: panel.section === 2
                Layout.fillWidth: true; Layout.minimumWidth: 0
                text: "Receive object emission"
                description: "Let glowing objects light captured colors. Target emitters helps small sources converge. Baked capture lighting remains."
                checked: Boolean(panel.composition.captureEmission)
                enabled: panel.captureSettings.mode === "lit-color"
                onToggled: {
                    const next = panel.sampledComposition(); next.captureEmission = checked
                    if (checked && next.captureEmissionSampling === undefined) next.captureEmissionSampling = "bounds-mixture"
                    panel.apply(next)
                }
            }
            ToneComboBox {
                objectName: "sceneCaptureEmissionSampling"
                visible: panel.section === 2 && Boolean(panel.composition.captureEmission)
                enabled: panel.captureSettings.mode === "lit-color"
                Layout.fillWidth: true
                accessibleLabel: "Object emission sampling"
                model: ["Cosine rays · Legacy", "Target emitters"]
                currentIndex: panel.composition.captureEmissionSampling === "bounds-mixture" ? 1 : 0
                onActivated: {
                    const next = panel.sampledComposition()
                    next.captureEmissionSampling = currentIndex === 1 ? "bounds-mixture" : "cosine"
                    panel.apply(next)
                }
            }
            ToneCheckBox {
                objectName: "sceneHazeShadows"
                visible: panel.section === 3
                text: "Shadowed haze"
                checked: Boolean(panel.composition.hazeShadows)
                onClicked: {
                    const next = panel.sampledComposition()
                    next.hazeShadows = checked
                    panel.apply(next)
                }
            }
            Repeater {
                model: panel.section === 2 ? ["power", "shadowStrength"] : panel.section === 3 ? ["density", "distance", "anisotropy"] : []
                delegate: RowLayout {
                    id: effectRow
                    required property string modelData
                    readonly property bool capture: panel.section === 2
                    Layout.fillWidth: true
                    Label { color: theme.textMain; text: ({power: panel.captureSettings.mode === "shadow-receiver" ? "Capture brightness" : "Lighting power", shadowStrength: "Shadow strength", density: "Density / scene unit", distance: "Background distance", anisotropy: "Forward scattering"})[effectRow.modelData] }
                    ToneTextField {
                        objectName: "sceneEffect_" + effectRow.modelData
                        Layout.fillWidth: true; Layout.preferredWidth: 100; numericEntry: true
                        text: Number((effectRow.capture ? panel.captureSettings : panel.hazeSettings)[effectRow.modelData]).toPrecision(5)
                        Accessible.name: effectRow.modelData
                        property bool userEdited: false
                        validator: DoubleValidator {
                            bottom: effectRow.modelData === "anisotropy" ? -0.95 : effectRow.modelData === "distance" ? 0.01 : 0
                            top: effectRow.modelData === "anisotropy" ? 0.95 : effectRow.modelData === "shadowStrength" ? 1 : effectRow.modelData === "distance" ? 100000000 : 100
                            locale: "C"
                        }
                        onTextEdited: userEdited = true
                        onEditingFinished: {
                            if (userEdited) {
                                userEdited = false
                                if (acceptableInput) panel.updateEffect(effectRow.capture ? "captureLighting" : "haze", effectRow.modelData, Number(text))
                                else panel.errorMessage = "Enter a valid value in the displayed units."
                            }
                        }
                    }
                }
            }
            Label { visible: panel.section === 3; color: theme.textMain; text: "Haze color · RGB" }
            RowLayout {
                visible: panel.section === 3; Layout.fillWidth: true
                Repeater {
                    model: 3
                    delegate: ToneTextField {
                        required property int index
                        objectName: "sceneHazeColor_" + index
                        Layout.fillWidth: true; Layout.preferredWidth: 75; numericEntry: true
                        text: Number(panel.hazeSettings.color[index]).toPrecision(5)
                        Accessible.name: "Haze color " + ["red", "green", "blue"][index]
                        property bool userEdited: false
                        validator: DoubleValidator { bottom: 0; top: 1; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: {
                            if (userEdited) {
                                userEdited = false
                                if (acceptableInput) { const color = panel.hazeSettings.color.slice(); color[index] = Number(text); panel.updateEffect("haze", "color", color) }
                            }
                        }
                    }
                }
            }
            Label {
                visible: panel.section === 2 || panel.section === 3; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                text: panel.section === 2
                    ? panel.captureSettings.mode === "shadow-receiver"
                        ? "Keep captured colors and receive shadows from added objects. Lights set the shadow direction and softness; ambient fill reduces shadow darkness. Unshadowed colors stay unchanged at Capture brightness 1. Shadows use estimated surface directions."
                        : "Light captured colors using estimated surface directions. Original lighting stays baked in. Added objects cast approximate shadows onto splats. Scan shadows also lets splats block received object emission. Set lights and ambient fill in Lights."
                    : "Distance haze uses scene units. Shadowed haze lets objects block light through the fog; Scan shadows also includes the captured scene. Final lighting samples control quality. Transparent splats use approximate layer blending. Density 0 disables haze."
            }
            ActionButton {
                objectName: "sceneImportedMaterialMode"
                visible: (panel.section === 0 || panel.section === 4) && Boolean(panel.selected.materialSetSha256)
                text: panel.selected.useImportedMaterials ? "Imported surfaces · On" : "Single surface override · On"
                onClicked: panel.update({useImportedMaterials: !panel.selected.useImportedMaterials})
            }
            ToneComboBox {
                id: surfaceSlot
                objectName: "sceneSurfaceSlot"; visible: panel.section === 4 && Boolean(panel.selected.id); Layout.fillWidth: true
                accessibleLabel: "Imported material slot"; model: panel.selected.materials || []; textRole: "name"
                currentIndex: -1
                displayText: String(panel.selectedSurface.name || "Choose a surface…")
                function synchronizeIndex(): void { currentIndex = panel.materialIndex < (model || []).length ? panel.materialIndex : -1 }
                onModelChanged: Qt.callLater(synchronizeIndex)
                onActivated: panel.materialIndex = currentIndex
                Connections { target: panel; function onMaterialIndexChanged(): void { surfaceSlot.synchronizeIndex() } }
            }
            ActionButton {
                objectName: "sceneSurfaceImageButton"; visible: panel.section === 4 && Boolean(panel.selectedSurface.textureSha256)
                text: panel.selectedSurface.useTexture ? "Color image · On" : "Color image · Off"
                onClicked: panel.updateSurface("useTexture", !panel.selectedSurface.useTexture)
            }
            Repeater {
                model: panel.section === 4 && panel.selectedSurface.name ? ["color", "emission"] : []
                delegate: ColumnLayout {
                    id: surfaceVector
                    required property string modelData
                    Layout.fillWidth: true
                    Label { color: theme.textMain; text: surfaceVector.modelData === "color" ? "Base color · RGB" : "Emission · RGB power" }
                    RowLayout {
                        Layout.fillWidth: true
                        Repeater {
                            model: 3
                            delegate: ToneTextField {
                                required property int index
                                objectName: "sceneSurface_" + surfaceVector.modelData + "_" + index
                                Layout.fillWidth: true; Layout.preferredWidth: 70; numericEntry: true
                                text: Number(surfaceVector.modelData === "color" ? panel.linearToDisplay((panel.selectedSurface.color || [0,0,0])[index]) : (panel.selectedSurface.emission || [0,0,0])[index]).toPrecision(6)
                                Accessible.name: surfaceVector.modelData + " " + ["red", "green", "blue"][index]
                                property bool userEdited: false
                                validator: DoubleValidator { bottom: 0; top: surfaceVector.modelData === "color" ? 1 : 10000; locale: "C" }
                                onTextEdited: userEdited = true
                                onEditingFinished: {
                                    if (userEdited) {
                                        userEdited = false
                                        if (acceptableInput && panel.selectedSurface[surfaceVector.modelData]) { const values = panel.selectedSurface[surfaceVector.modelData].slice(); values[index] = surfaceVector.modelData === "color" ? panel.displayToLinear(Number(text)) : Number(text); panel.updateSurface(surfaceVector.modelData, values) }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Repeater {
                model: panel.section === 4 && panel.selectedSurface.name ? ["roughness", "metallic"] : []
                delegate: RowLayout {
                    id: surfaceScalar
                    required property string modelData
                    Layout.fillWidth: true
                    Label { color: theme.textMain; text: surfaceScalar.modelData === "roughness" ? "Roughness" : "Metallic" }
                    ToneTextField {
                        objectName: "sceneSurface_" + surfaceScalar.modelData; Layout.fillWidth: true; numericEntry: true
                        text: Number(panel.selectedSurface[surfaceScalar.modelData]).toPrecision(5)
                        Accessible.name: surfaceScalar.modelData
                        property bool userEdited: false
                        validator: DoubleValidator { bottom: surfaceScalar.modelData === "roughness" ? 0.04 : 0; top: 1; locale: "C" }
                        onTextEdited: userEdited = true
                        onEditingFinished: { if (userEdited) { userEdited = false; if (acceptableInput) panel.updateSurface(surfaceScalar.modelData, Number(text)) } }
                    }
                }
            }
            Repeater {
                model: panel.section === 4 && panel.selectedSurface.name ? ["uvScale", "uvOffset"] : []
                delegate: ColumnLayout {
                    id: surfaceUv
                    required property string modelData
                    Layout.fillWidth: true
                    Label { color: theme.textMain; text: surfaceUv.modelData === "uvScale" ? "Image scale · UV" : "Image offset · UV" }
                    RowLayout {
                        Layout.fillWidth: true
                        Repeater {
                            model: 2
                            delegate: ToneTextField {
                                required property int index
                                objectName: "sceneSurface_" + surfaceUv.modelData + "_" + index
                                Layout.fillWidth: true; Layout.preferredWidth: 70; numericEntry: true
                                text: Number((panel.selectedSurface[surfaceUv.modelData] || [0,0])[index]).toPrecision(6)
                                Accessible.name: surfaceUv.modelData + " " + ["U", "V"][index]
                                property bool userEdited: false
                                validator: DoubleValidator { bottom: -1000000; top: 1000000; locale: "C" }
                                onTextEdited: userEdited = true
                                onEditingFinished: {
                                    if (userEdited) { userEdited = false; if (acceptableInput && panel.selectedSurface[surfaceUv.modelData]) { const values = panel.selectedSurface[surfaceUv.modelData].slice(); values[index] = Number(text); panel.updateSurface(surfaceUv.modelData, values) } }
                                }
                            }
                        }
                    }
                }
            }
            ActionButton {
                objectName: "sceneSurfaceClampButton"; visible: panel.section === 4 && Boolean(panel.selectedSurface.name)
                text: panel.selectedSurface.clamp ? "Image edges · Clamp" : "Image edges · Repeat"
                onClicked: panel.updateSurface("clamp", !panel.selectedSurface.clamp)
            }
            Label {
                visible: panel.section === 4; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                text: panel.entries.length ? "Edit each imported surface here. Object color acts as a tint. Switch to a single surface override in Objects to replace all face materials." : "Import an OBJ with its accompanying MTL and color images to edit its surfaces here."
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted
                visible: (panel.viewport.status.materialNotices || []).length > 0
                text: (panel.viewport.status.materialNotices || []).join("\n")
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { objectName: "sceneCompositionUndoButton"; text: "Undo"; enabled: Boolean(panel.viewport.status.compositionCanUndo); onClicked: panel.errorMessage = String(panel.viewport.undoSceneComposition().error || "") }
                ActionButton { objectName: "sceneCompositionRedoButton"; text: "Redo"; enabled: Boolean(panel.viewport.status.compositionCanRedo); onClicked: panel.errorMessage = String(panel.viewport.redoSceneComposition().error || "") }
            }
            StudioCollapsibleGroup {
                objectName: "sceneCompositionOutputDetails"
                title: "Photo and PLY details"
                expanded: panel.outputDetailsExpanded
                onToggled: panel.outputDetailsExpanded = !panel.outputDetailsExpanded
                Label {
                    objectName: "sceneCompositionOutputDetailsText"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: theme.textMuted
                    text: "Photos and films include scene composition and atmosphere. PLY export contains splats only. Splat/object compositing and shadows received by the capture use Gaussian center depth. Scan shadows on added objects use 3D Gaussian visibility."
                }
            }
            Label { Layout.fillWidth: true; visible: panel.errorMessage.length > 0; text: panel.errorMessage; wrapMode: Text.WordWrap; color: theme.textMain }
        }
    }
}
