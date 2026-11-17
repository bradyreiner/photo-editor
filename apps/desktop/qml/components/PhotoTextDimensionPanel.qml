pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "." as Components

ColumnLayout {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    required property bool locked

    property string documentToken: ""
    property string revision: ""
    property string canvasMode: ""
    property string canvasTargetMode: "dimension"
    property bool shapeExpanded: true
    property bool viewExpanded: false
    property bool surfaceExpanded: false
    property bool editSessionOpen: false
    property bool editSessionInvalid: false
    property bool coalescedEditActive: false
    property bool ownWriteInProgress: false
    property int selectedPresetIndex: -1
    property int editLayerIndex: -1
    property string editLayerId: ""
    property string editDocumentToken: ""
    property string editRevision: ""
    property var editController: null
    property int dialogLayerIndex: -1
    property string dialogLayerId: ""
    property string dialogDocumentToken: ""
    property string dialogRevision: ""
    property var dialogController: null
    property string expectedOwnAcceptedRevision: ""
    property int expectedOwnIndex: -1
    property string expectedOwnLayerId: ""
    property string expectedOwnDocumentToken: ""
    property var expectedOwnController: null
    property string editingColorKey: ""

    Theme { id: theme }
    spacing: theme.space4

    readonly property var defaults: ({
        enabled: false,
        depth: 32,
        bevelWidth: 2,
        bevelSegments: 3,
        bevelProfile: "rounded",
        rotationX: -12,
        rotationY: -18,
        rotationZ: 0,
        pivotX: 0.5,
        pivotY: 0.5,
        projection: "perspective",
        focalLength: 1000,
        useTextColor: true,
        frontColor: "#fff6f1e8",
        sideColor: "#ff353941",
        bevelColor: "#ffe6e8ec",
        roughness: 0.42,
        metallic: 0.08,
        lightAzimuth: -38,
        lightElevation: 35,
        lightIntensity: 1.1,
        ambient: 0.28,
        cornerPinEnabled: false,
        cornerPins: [
            {x: 0, y: 0}, {x: 1, y: 0},
            {x: 1, y: 1}, {x: 0, y: 1}
        ]
    })
    readonly property var storedDimension: {
        const data = root.layerData || ({})
        const fallback = data.textDimension && typeof data.textDimension === "object"
            ? data.textDimension : ({})
        let current = fallback
        if (root.controller && typeof root.controller.photoLayerTextDimension === "function") {
            try {
                const result = root.controller.photoLayerTextDimension(root.layerIndex)
                if (result && typeof result === "object" && Object.keys(result).length > 0)
                    current = result
            } catch (error) {
                current = fallback
            }
        }
        return current
    }
    readonly property var dimension: Object.assign({}, root.defaults, root.storedDimension || ({}))
    readonly property string targetLayerId: String((root.layerData || ({})).id || "")
    readonly property string currentDocumentToken: root.documentToken.length > 0
        ? root.documentToken : String(root.controller && root.controller.assetId !== undefined
                                       ? root.controller.assetId : "")
    readonly property bool canEdit: !root.locked && root.layerIndex >= 0 &&
        root.targetLayerId.length > 0 && root.controller &&
        typeof root.controller.setPhotoLayerTextDimension === "function"
    readonly property var presets: [
        {
            id: "solid-studio",
            name: qsTr("Solid studio"),
            patch: {
                enabled: true, depth: 32, bevelWidth: 1.5, bevelSegments: 3,
                bevelProfile: "rounded", rotationX: -12, rotationY: -18,
                rotationZ: 0, projection: "perspective", focalLength: 1000,
                useTextColor: true, sideColor: "#ff343943", bevelColor: "#ffe5e9ed",
                roughness: 0.42, metallic: 0.08, lightAzimuth: -38,
                lightElevation: 35, lightIntensity: 1.1, ambient: 0.28
            }
        },
        {
            id: "beveled-metal",
            name: qsTr("Beveled metal"),
            patch: {
                enabled: true, depth: 24, bevelWidth: 3, bevelSegments: 5,
                bevelProfile: "chamfer", rotationX: -10, rotationY: -22,
                rotationZ: 0, projection: "perspective", focalLength: 1200,
                useTextColor: false, frontColor: "#ffc6d0d8", sideColor: "#ff49525c",
                bevelColor: "#fff4f7fa", roughness: 0.24, metallic: 0.88,
                lightAzimuth: -42, lightElevation: 48, lightIntensity: 1.35, ambient: 0.2
            }
        },
        {
            id: "retro-deep",
            name: qsTr("Retro deep"),
            patch: {
                enabled: true, depth: 48, bevelWidth: 2.5, bevelSegments: 3,
                bevelProfile: "rounded", rotationX: -8, rotationY: -15,
                rotationZ: -2, projection: "perspective", focalLength: 900,
                useTextColor: false, frontColor: "#ffffc04f", sideColor: "#ff6f2942",
                bevelColor: "#ffffe4a0", roughness: 0.5, metallic: 0.12,
                lightAzimuth: -52, lightElevation: 32, lightIntensity: 1.15, ambient: 0.32
            }
        }
    ]

    signal canvasEditRequested(string mode)
    signal canvasTargetModeRequested(string mode)

    function currentTargetRevision(index, targetController) {
        const source = typeof targetController === "undefined" ? root.controller : targetController
        if (!source)
            return ""
        if (source && typeof source.photoTextDimensionTargetRevision === "function") {
            try {
                const revision = String(source.photoTextDimensionTargetRevision(Number(index)) || "")
                if (revision.length > 0)
                    return revision
            } catch (error) {
                // Older test and preview providers may only expose the bound revision.
            }
        }
        return String(root.revision || "")
    }

    function identityMatches(index, layerId, token, targetController) {
        const controller = typeof targetController === "undefined" ? root.controller : targetController
        return !!controller && root.controller === controller &&
            root.layerIndex === index && root.targetLayerId === layerId &&
            root.currentDocumentToken === token && !root.locked
    }

    function rememberExpectedOwnRevision(index, layerId, token, targetController) {
        const controller = typeof targetController === "undefined" ? root.controller : targetController
        const revision = root.currentTargetRevision(index, controller)
        if (!controller || revision.length === 0)
            return false
        root.expectedOwnController = controller
        root.expectedOwnIndex = Number(index)
        root.expectedOwnLayerId = String(layerId)
        root.expectedOwnDocumentToken = String(token)
        root.expectedOwnAcceptedRevision = revision
        return true
    }

    function expectedOwnRevisionMatches(index, layerId, token, targetController) {
        const controller = typeof targetController === "undefined" ? root.controller : targetController
        return !!controller && root.expectedOwnController === controller &&
            root.controller === controller && root.layerIndex === Number(index) &&
            root.targetLayerId === String(layerId) && root.currentDocumentToken === String(token) &&
            root.expectedOwnIndex === Number(index) &&
            root.expectedOwnLayerId === String(layerId) &&
            root.expectedOwnDocumentToken === String(token) &&
            root.currentTargetRevision(index, controller) === root.expectedOwnAcceptedRevision
    }

    function clearExpectedOwnRevision() {
        root.expectedOwnAcceptedRevision = ""
        root.expectedOwnIndex = -1
        root.expectedOwnLayerId = ""
        root.expectedOwnDocumentToken = ""
        root.expectedOwnController = null
    }

    function writePatchForTarget(patch, index, layerId, token, targetRevision, targetController) {
        const controller = typeof targetController === "undefined" ? root.controller : targetController
        const currentRevision = root.currentTargetRevision(index, controller)
        if (!root.canEdit || !patch || typeof patch !== "object" ||
                !root.identityMatches(Number(index), String(layerId), String(token), controller) ||
                currentRevision !== String(targetRevision))
            return false
        let accepted = false
        root.ownWriteInProgress = true
        try {
            accepted = controller.setPhotoLayerTextDimension(Number(index), patch) === true
        } catch (error) {
            accepted = false
        } finally {
            root.ownWriteInProgress = false
        }
        if (accepted) {
            const acceptedRevision = root.currentTargetRevision(index, controller)
            root.rememberExpectedOwnRevision(index, layerId, token, controller)
            if (root.editSessionOpen && !root.editSessionInvalid &&
                    root.identityMatches(root.editLayerIndex, root.editLayerId,
                                         root.editDocumentToken, root.editController))
                root.editRevision = acceptedRevision
        }
        return accepted
    }

    function captureEditorTarget(editor) {
        if (!editor || editor.draftActive)
            return
        editor.draftActive = true
        editor.capturedIndex = root.layerIndex
        editor.capturedLayerId = root.targetLayerId
        editor.capturedDocumentToken = root.currentDocumentToken
        editor.capturedController = root.controller
        editor.capturedRevision = root.currentTargetRevision(root.layerIndex, root.controller)
    }

    function clearEditorTarget(editor) {
        if (!editor)
            return
        editor.draftActive = false
        editor.capturedIndex = -1
        editor.capturedLayerId = ""
        editor.capturedDocumentToken = ""
        editor.capturedRevision = ""
        editor.capturedController = null
    }

    function fieldPatch(key, value) {
        const patch = ({})
        patch[String(key)] = value
        return patch
    }

    function beginDimensionEdit() {
        if (!root.canEdit || root.editSessionOpen ||
                typeof root.controller.beginCoalescedEdit !== "function")
            return false
        root.editLayerIndex = root.layerIndex
        root.editLayerId = root.targetLayerId
        root.editDocumentToken = root.currentDocumentToken
        root.editController = root.controller
        root.editRevision = root.currentTargetRevision(root.layerIndex, root.editController)
        root.editSessionInvalid = false
        try {
            root.editController.beginCoalescedEdit()
            root.editSessionOpen = true
            root.coalescedEditActive = true
            return true
        } catch (error) {
            root.editLayerIndex = -1
            root.editLayerId = ""
            root.editDocumentToken = ""
            root.editRevision = ""
            root.editController = null
            return false
        }
    }

    function closeCoalescedEdit() {
        if (!root.coalescedEditActive)
            return
        root.coalescedEditActive = false
        try {
            if (root.editController && typeof root.editController.endCoalescedEdit === "function")
                root.editController.endCoalescedEdit()
        } catch (error) {
            // The controller owns history failure reporting.
        }
    }

    function endDimensionEdit() {
        root.closeCoalescedEdit()
        root.editSessionOpen = false
        root.editSessionInvalid = false
        root.editLayerIndex = -1
        root.editLayerId = ""
        root.editDocumentToken = ""
        root.editRevision = ""
        root.editController = null
    }

    function invalidatePanelEdit() {
        if (!root.editSessionOpen)
            return
        root.editSessionInvalid = true
        // Keep the captured identity until the slider reports its finish so a
        // late release can never be redirected to a newly selected layer.
        root.closeCoalescedEdit()
    }

    function handleRevisionChanged() {
        if (colorDialog.visible && !root.dialogTargetMatches())
            colorDialog.close()
        if (root.ownWriteInProgress) {
            if (root.editSessionOpen && !root.editSessionInvalid &&
                    root.identityMatches(root.editLayerIndex, root.editLayerId,
                                         root.editDocumentToken, root.editController))
                root.editRevision = root.currentTargetRevision(root.editLayerIndex, root.editController)
            return
        }
        if (root.editSessionOpen) {
            const currentRevision = root.currentTargetRevision(root.editLayerIndex, root.editController)
            const expectedOwn = root.expectedOwnRevisionMatches(root.editLayerIndex,
                root.editLayerId, root.editDocumentToken, root.editController)
            if (!root.editSessionInvalid &&
                    root.identityMatches(root.editLayerIndex, root.editLayerId,
                                         root.editDocumentToken, root.editController) &&
                    (currentRevision === root.editRevision || expectedOwn)) {
                root.editRevision = currentRevision
                return
            }
            root.invalidatePanelEdit()
            root.clearExpectedOwnRevision()
            return
        }
        if (root.expectedOwnRevisionMatches(root.expectedOwnIndex, root.expectedOwnLayerId,
                root.expectedOwnDocumentToken, root.expectedOwnController))
            return
        root.clearExpectedOwnRevision()
        root.dismissStaleColorDialog()
    }

    function writePatch(patch) {
        if (!root.canEdit || !patch || typeof patch !== "object")
            return false
        if (root.editSessionOpen) {
            const currentRevision = root.currentTargetRevision(root.editLayerIndex, root.editController)
            if (root.editSessionInvalid ||
                    !root.identityMatches(root.editLayerIndex, root.editLayerId,
                                          root.editDocumentToken, root.editController) ||
                    currentRevision !== root.editRevision) {
                root.invalidatePanelEdit()
                return false
            }
            return root.writePatchForTarget(patch, root.editLayerIndex, root.editLayerId,
                root.editDocumentToken, root.editRevision, root.editController)
        }
        return root.writePatchForTarget(patch, root.layerIndex, root.targetLayerId,
            root.currentDocumentToken, root.currentTargetRevision(root.layerIndex, root.controller),
            root.controller)
    }

    function setEnabled(enabled) {
        if (!root.canEdit)
            return false
        if (!enabled)
            return root.writePatch({enabled: false})

        // The getter supplies defaults even before a layer has an authored
        // envelope. Detect first opt-in from layerData so its initial pose is
        // dimensional; later enable only toggles the flag.
        const authored = root.layerData && root.layerData.textDimension &&
            typeof root.layerData.textDimension === "object" &&
            Object.keys(root.layerData.textDimension).length > 0
        if (!authored) {
            return root.writePatch({
                enabled: true,
                depth: root.defaults.depth,
                bevelWidth: root.defaults.bevelWidth,
                bevelSegments: root.defaults.bevelSegments,
                bevelProfile: root.defaults.bevelProfile,
                rotationX: root.defaults.rotationX,
                rotationY: root.defaults.rotationY,
                rotationZ: root.defaults.rotationZ,
                pivotX: root.defaults.pivotX,
                pivotY: root.defaults.pivotY,
                projection: root.defaults.projection,
                focalLength: root.defaults.focalLength
            })
        }

        return root.writePatch({enabled: true})
    }

    function applyPreset(index) {
        if (index < 0 || index >= root.presets.length)
            return false
        return root.writePatch(root.presets[index].patch)
    }

    function resetShape() {
        return root.writePatch({
            depth: root.defaults.depth,
            bevelWidth: root.defaults.bevelWidth,
            bevelSegments: root.defaults.bevelSegments,
            bevelProfile: root.defaults.bevelProfile
        })
    }

    function resetRotation() {
        return root.writePatch({rotationX: root.defaults.rotationX,
                                rotationY: root.defaults.rotationY,
                                rotationZ: root.defaults.rotationZ,
                                pivotX: root.defaults.pivotX,
                                pivotY: root.defaults.pivotY})
    }

    function resetProjection() {
        return root.writePatch({projection: root.defaults.projection,
                                focalLength: root.defaults.focalLength})
    }

    function commitCornerPin(key, axis, text, index, layerId, token, targetRevision, targetController) {
        const source = String(text).trim()
        if (!source.length || !/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(source))
            return false
        const value = Number(source)
        if (!Number.isFinite(value))
            return false
        const cornerIndex = root.cornerPinIndex(key)
        if (cornerIndex < 0)
            return false
        const sourcePins = Array.isArray(root.dimension.cornerPins)
            ? root.dimension.cornerPins : root.defaults.cornerPins
        const pins = JSON.parse(JSON.stringify(sourcePins))
        const corner = pins[cornerIndex] || ({x: 0, y: 0})
        corner[String(axis)] = Math.max(-2, Math.min(3, value))
        pins[cornerIndex] = corner
        const patch = {cornerPins: pins}
        return index >= 0
            ? root.writePatchForTarget(patch, index, layerId, token, targetRevision, targetController)
            : root.writePatch(patch)
    }

    function resetCornerPins() {
        return root.writePatch({cornerPinEnabled: false,
                                cornerPins: JSON.parse(JSON.stringify(root.defaults.cornerPins))})
    }

    function cornerPinIndex(key) {
        return ["topLeft", "topRight", "bottomRight", "bottomLeft"].indexOf(String(key))
    }

    function cornerPin(key) {
        const index = root.cornerPinIndex(key)
        const pins = Array.isArray(root.dimension.cornerPins)
            ? root.dimension.cornerPins : root.defaults.cornerPins
        return index >= 0 && pins[index] ? pins[index] : root.defaults.cornerPins[Math.max(0, index)]
    }

    function resetSurface() {
        return root.writePatch({
            useTextColor: root.defaults.useTextColor,
            frontColor: root.defaults.frontColor,
            sideColor: root.defaults.sideColor,
            bevelColor: root.defaults.bevelColor,
            roughness: root.defaults.roughness,
            metallic: root.defaults.metallic,
            lightAzimuth: root.defaults.lightAzimuth,
            lightElevation: root.defaults.lightElevation,
            lightIntensity: root.defaults.lightIntensity,
            ambient: root.defaults.ambient
        })
    }

    function currentColor(key) {
        const value = root.dimension[String(key)]
        return value === undefined || value === null ? String(root.defaults[String(key)]) : String(value)
    }

    function captureColorDialogTarget(key) {
        if (!root.canEdit)
            return false
        root.editingColorKey = String(key)
        root.dialogLayerIndex = root.layerIndex
        root.dialogLayerId = root.targetLayerId
        root.dialogDocumentToken = root.currentDocumentToken
        root.dialogController = root.controller
        root.dialogRevision = root.currentTargetRevision(root.layerIndex, root.dialogController)
        return true
    }

    function openColorDialog(key) {
        if (!root.captureColorDialogTarget(key))
            return false
        colorDialog.selectedColor = root.currentColor(key)
        colorDialog.open()
        return true
    }

    function dialogTargetMatches() {
        return !root.locked && root.controller === root.dialogController &&
            root.layerIndex === root.dialogLayerIndex &&
            root.targetLayerId === root.dialogLayerId &&
            root.currentDocumentToken === root.dialogDocumentToken &&
            root.currentTargetRevision(root.dialogLayerIndex, root.dialogController) === root.dialogRevision
    }

    function closeColorDialog() {
        root.dialogLayerIndex = -1
        root.dialogLayerId = ""
        root.dialogDocumentToken = ""
        root.dialogRevision = ""
        root.dialogController = null
        root.editingColorKey = ""
    }

    function dismissStaleColorDialog() {
        if (colorDialog.visible && !root.dialogTargetMatches())
            colorDialog.close()
    }

    function commitHexColorForTarget(key, text, index, layerId, token, targetRevision, targetController) {
        const value = String(text).trim()
        if (!/^#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$/.test(value))
            return false
        return root.writePatchForTarget(root.fieldPatch(key, value), index,
                                        layerId, token, targetRevision, targetController)
    }

    function groupChanged(keys) {
        return keys.some(key => JSON.stringify(root.dimension[key]) !== JSON.stringify(root.defaults[key]))
    }

    onLayerIndexChanged: { invalidatePanelEdit(); dismissStaleColorDialog() }
    onTargetLayerIdChanged: { invalidatePanelEdit(); dismissStaleColorDialog() }
    onDocumentTokenChanged: { invalidatePanelEdit(); dismissStaleColorDialog() }
    onControllerChanged: {
        invalidatePanelEdit()
        dismissStaleColorDialog()
        clearExpectedOwnRevision()
    }
    onRevisionChanged: handleRevisionChanged()
    onLockedChanged: if (locked) { invalidatePanelEdit(); dismissStaleColorDialog() }
    Component.onDestruction: {
        root.endDimensionEdit()
        if (colorDialog.visible) colorDialog.close()
    }

    RowLayout {
        objectName: "photoTextDimensionTargetModeStrip"
        Layout.fillWidth: true
        spacing: theme.space8

        Label {
            Layout.fillWidth: true
            text: root.canvasTargetMode === "text" ? qsTr("Source text editing") : qsTr("3D controls")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }
        Components.ActionButton {
            objectName: "photoTextDimensionTargetModeAction"
            text: root.canvasTargetMode === "text" ? qsTr("Done") : qsTr("Edit source text")
            tone: "secondary"
            compact: true
            enabled: !root.locked
            onClicked: {
                const next = root.canvasTargetMode === "text" ? "dimension" : "text"
                root.canvasTargetModeRequested(next)
            }
        }
    }
    Label {
        objectName: "photoTextDimensionSourceTextHint"
        Layout.fillWidth: true
        visible: root.canvasTargetMode === "text"
        text: qsTr("Finish editing to update the 3D preview.")
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeMicro
        wrapMode: Text.WordWrap
    }

    RowLayout {
        objectName: "photoTextDimensionPresetRow"
        Layout.fillWidth: true
        spacing: theme.space8

        Label {
            text: qsTr("Preset")
            color: theme.textMain
            font.family: theme.fontUi
            font.pixelSize: theme.typeLabel
        }
        Components.ToneComboBox {
            id: presetPicker
            objectName: "photoTextDimensionPresetPicker"
            Layout.fillWidth: true
            accessibleLabel: qsTr("Dimensional text preset")
            model: root.presets.map(preset => preset.name)
            enabled: root.canEdit
            currentIndex: root.selectedPresetIndex
            displayText: currentIndex >= 0 ? textAt(currentIndex) : qsTr("Choose a look")
            onActivated: function(index) {
                root.selectedPresetIndex = index
                root.applyPreset(index)
            }
        }
    }

    Components.InspectorSection {
        objectName: "photoTextDimensionShapeSection"
        Layout.fillWidth: true
        title: qsTr("Shape")
        subtitle: qsTr("Editable extrusion and bevel geometry")
        helpText: qsTr("Text content, font, and character runs remain editable.")
        expandable: true
        expanded: root.shapeExpanded
        resetAvailable: root.groupChanged(["depth", "bevelWidth", "bevelSegments", "bevelProfile"])
        resetObjectName: "photoTextDimensionResetShape"
        resetTooltip: qsTr("Reset depth and bevel settings")
        enabled: root.canEdit
        onToggled: root.shapeExpanded = !root.shapeExpanded
        onResetRequested: root.resetShape()

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space4

            Components.ToneSwitch {
                objectName: "photoTextDimensionEnabled"
                Layout.fillWidth: true
                text: qsTr("Enable 3D text")
                accessibleLabel: qsTr("Enable 3D text")
                accessibleDescription: qsTr("Opt in to editable extrusion, rotation, and perspective.")
                checked: root.dimension.enabled === true
                enabled: root.canEdit
                onToggled: function() { root.setEnabled(checked) }
            }
            Label {
                objectName: "photoTextDimensionRetentionHint"
                Layout.fillWidth: true
                visible: root.dimension.enabled !== true
                text: qsTr("Turning this off keeps the configured shape and finish for later.")
                wrapMode: Text.WordWrap
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            GridLayout {
                Layout.fillWidth: true
                visible: root.dimension.enabled === true
                enabled: root.canEdit
                columns: root.width >= theme.scaled(480) ? 2 : 1
                columnSpacing: theme.space8
                rowSpacing: theme.space4

                Repeater {
                    model: [
                        { key: "depth", label: qsTr("Depth"), from: 0, to: 256, step: 0.25, digits: 2, unit: " px" },
                        { key: "bevelWidth", label: qsTr("Bevel width"), from: 0, to: 64, step: 0.25, digits: 2, unit: " px" },
                        { key: "bevelSegments", label: qsTr("Bevel segments"), from: 1, to: 16, step: 1, digits: 0, unit: "" }
                    ]
                    delegate: Components.InspectorSlider {
                        required property var modelData
                        Layout.fillWidth: true
                        stackedHeader: root.width < theme.scaled(300)
                        objectName: "photoTextDimension_" + modelData.key
                        label: modelData.label
                        from: modelData.from
                        to: modelData.to
                        stepSize: modelData.step
                        defaultValue: Number(root.defaults[modelData.key])
                        value: Number(root.dimension[modelData.key])
                        valueText: Number(root.dimension[modelData.key]).toFixed(modelData.digits) + modelData.unit
                        enabled: root.canEdit
                        onInteractionStarted: root.beginDimensionEdit()
                        onInteractionFinished: root.endDimensionEdit()
                        onValueInputDirtyChanged: {
                            if (valueInputDirty) root.beginDimensionEdit()
                            else if (!pressed) Qt.callLater(function() {
                                if (root.editSessionOpen && !root.editSessionInvalid) root.endDimensionEdit()
                            })
                        }
                        onValueDragged: function(value) {
                            const rounded = modelData.digits === 0 ? Math.round(value) : Number(value.toFixed(modelData.digits))
                            root.writePatch(root.fieldPatch(modelData.key, rounded))
                            if (!pressed) root.endDimensionEdit()
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2
                    Label {
                        text: qsTr("Bevel profile")
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    Components.ToneComboBox {
                        objectName: "photoTextDimensionBevelProfile"
                        Layout.fillWidth: true
                        accessibleLabel: qsTr("Bevel profile")
                        model: [qsTr("Rounded"), qsTr("Chamfer")]
                        currentIndex: root.dimension.bevelProfile === "chamfer" ? 1 : 0
                        enabled: root.canEdit
                        onActivated: function(index) { root.writePatch({bevelProfile: index === 1 ? "chamfer" : "rounded"}) }
                    }
                }
            }
        }
    }

    Components.InspectorSection {
        objectName: "photoTextDimensionViewSection"
        Layout.fillWidth: true
        title: qsTr("View")
        subtitle: qsTr("Object rotation, projection, and planar placement")
        helpText: qsTr("Depth and view shape the letters first; corner pins place the result on a plane. The layer transform moves the finished text.")
        expandable: true
        expanded: root.viewExpanded
        enabled: root.canEdit
        onToggled: root.viewExpanded = !root.viewExpanded

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space4

            Flow {
                objectName: "photoTextDimensionCanvasModes"
                Layout.fillWidth: true
                spacing: theme.space4

                Components.ActionButton {
                    objectName: "photoTextDimensionCanvasRotate"
                    text: qsTr("Rotate on canvas")
                    tone: "secondary"
                    active: root.canvasMode === "rotate"
                    compact: true
                    enabled: root.canEdit && root.dimension.enabled === true
                    onClicked: {
                        root.canvasMode = "rotate"
                        root.canvasEditRequested("rotate")
                    }
                }
                Components.ActionButton {
                    objectName: "photoTextDimensionCanvasDepth"
                    text: qsTr("Adjust depth")
                    tone: "secondary"
                    active: root.canvasMode === "depth"
                    compact: true
                    enabled: root.canEdit && root.dimension.enabled === true
                    onClicked: {
                        root.canvasMode = "depth"
                        root.canvasEditRequested("depth")
                    }
                }
                Components.ActionButton {
                    objectName: "photoTextDimensionCanvasCorners"
                    text: qsTr("Corner pin")
                    tone: "secondary"
                    active: root.canvasMode === "corner-pin"
                    compact: true
                    enabled: root.canEdit
                    onClicked: {
                        root.canvasMode = "corner-pin"
                        root.writePatch({cornerPinEnabled: true})
                        root.canvasEditRequested("corner-pin")
                    }
                }
                Components.ActionButton {
                    objectName: "photoTextDimensionResetRotation"
                    text: qsTr("Reset rotation")
                    tone: "quiet"
                    compact: true
                    enabled: root.canEdit && root.groupChanged(["rotationX", "rotationY", "rotationZ", "pivotX", "pivotY"])
                    onClicked: root.resetRotation()
                }
                Components.ActionButton {
                    objectName: "photoTextDimensionResetProjection"
                    text: qsTr("Reset projection")
                    tone: "quiet"
                    compact: true
                    enabled: root.canEdit && root.groupChanged(["projection", "focalLength"])
                    onClicked: root.resetProjection()
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(480) ? 2 : 1
                columnSpacing: theme.space8
                rowSpacing: theme.space4

                Repeater {
                    model: [
                        { key: "rotationX", label: qsTr("Rotate X"), from: -180, to: 180, step: 0.25, digits: 1, unit: "°" },
                        { key: "rotationY", label: qsTr("Rotate Y"), from: -180, to: 180, step: 0.25, digits: 1, unit: "°" },
                        { key: "rotationZ", label: qsTr("Rotate Z"), from: -180, to: 180, step: 0.25, digits: 1, unit: "°" },
                        { key: "pivotX", label: qsTr("Pivot X"), from: 0, to: 1, step: 0.001, digits: 3, unit: "" },
                        { key: "pivotY", label: qsTr("Pivot Y"), from: 0, to: 1, step: 0.001, digits: 3, unit: "" }
                    ]
                    delegate: Components.InspectorSlider {
                        required property var modelData
                        Layout.fillWidth: true
                        stackedHeader: root.width < theme.scaled(300)
                        objectName: "photoTextDimension_" + modelData.key
                        label: modelData.label
                        from: modelData.from
                        to: modelData.to
                        stepSize: modelData.step
                        defaultValue: Number(root.defaults[modelData.key])
                        value: Number(root.dimension[modelData.key])
                        valueText: Number(root.dimension[modelData.key]).toFixed(modelData.digits) + modelData.unit
                        enabled: root.canEdit && root.dimension.enabled === true
                        onInteractionStarted: root.beginDimensionEdit()
                        onInteractionFinished: root.endDimensionEdit()
                        onValueInputDirtyChanged: {
                            if (valueInputDirty) root.beginDimensionEdit()
                            else if (!pressed) Qt.callLater(function() {
                                if (root.editSessionOpen && !root.editSessionInvalid) root.endDimensionEdit()
                            })
                        }
                        onValueDragged: function(value) {
                            root.writePatch(root.fieldPatch(modelData.key, Number(value.toFixed(3))))
                            if (!pressed) root.endDimensionEdit()
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2
                    Label {
                        text: qsTr("Projection")
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    Components.ToneComboBox {
                        objectName: "photoTextDimensionProjection"
                        Layout.fillWidth: true
                        accessibleLabel: qsTr("Projection")
                        model: [qsTr("Perspective"), qsTr("Orthographic")]
                        currentIndex: root.dimension.projection === "orthographic" ? 1 : 0
                        enabled: root.canEdit && root.dimension.enabled === true
                        onActivated: function(index) { root.writePatch({projection: index === 1 ? "orthographic" : "perspective"}) }
                    }
                }
                Components.InspectorSlider {
                    objectName: "photoTextDimensionFocalLength"
                    Layout.fillWidth: true
                    stackedHeader: root.width < theme.scaled(300)
                    label: qsTr("Focal distance")
                    from: 100
                    to: 10000
                    stepSize: 10
                    defaultValue: Number(root.defaults.focalLength)
                    value: Number(root.dimension.focalLength)
                    valueText: Number(root.dimension.focalLength).toFixed(0) + " px"
                    enabled: root.canEdit && root.dimension.enabled === true && root.dimension.projection === "perspective"
                    onInteractionStarted: root.beginDimensionEdit()
                    onInteractionFinished: root.endDimensionEdit()
                    onValueInputDirtyChanged: {
                        if (valueInputDirty) root.beginDimensionEdit()
                        else if (!pressed) Qt.callLater(function() {
                            if (root.editSessionOpen && !root.editSessionInvalid) root.endDimensionEdit()
                        })
                    }
                    onValueDragged: function(value) {
                        root.writePatch({focalLength: Math.round(value)})
                        if (!pressed) root.endDimensionEdit()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Corner pin")
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    wrapMode: Text.WordWrap
                }
                Components.ToneSwitch {
                    objectName: "photoTextDimensionCornerPinEnabled"
                    text: qsTr("Enable")
                    accessibleLabel: qsTr("Enable four-corner perspective")
                    checked: root.dimension.cornerPinEnabled === true
                    enabled: root.canEdit
                    onToggled: function() { root.writePatch({cornerPinEnabled: checked}) }
                }
                Components.ActionButton {
                    objectName: "photoTextDimensionResetCornerPins"
                    text: qsTr("Reset pins")
                    tone: "quiet"
                    compact: true
                    enabled: root.canEdit && root.groupChanged(["cornerPins", "cornerPinEnabled"])
                    onClicked: root.resetCornerPins()
                }
            }
            Label {
                Layout.fillWidth: true
                visible: root.dimension.cornerPinEnabled === true
                text: qsTr("Drag each canvas corner independently. Corner pinning is a planar transform separate from object rotation.")
                wrapMode: Text.WordWrap
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            Label {
                Layout.fillWidth: true
                visible: root.dimension.cornerPinEnabled === true
                text: qsTr("Corner pins replace the legacy plane sliders while active. Saved 3D depth and rotation remain independent.")
                wrapMode: Text.WordWrap
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            Label {
                Layout.fillWidth: true
                visible: root.dimension.cornerPinEnabled === true
                text: qsTr("Normalized corner positions")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }
            GridLayout {
                objectName: "photoTextDimensionCornerPinFields"
                Layout.fillWidth: true
                visible: root.dimension.cornerPinEnabled === true
                enabled: root.canEdit
                columns: root.width >= theme.scaled(480) ? 2 : 1
                columnSpacing: theme.space8
                rowSpacing: theme.space4

                Repeater {
                    model: [
                        { key: "topLeft", label: qsTr("Top left") },
                        { key: "topRight", label: qsTr("Top right") },
                        { key: "bottomRight", label: qsTr("Bottom right") },
                        { key: "bottomLeft", label: qsTr("Bottom left") }
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: theme.space2
                        Label {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: root.width >= theme.scaled(480) ? 4
                                : root.width >= theme.scaled(400) ? 2 : 1
                            columnSpacing: theme.space4
                            rowSpacing: theme.space4
                            Label {
                                text: "X"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                Layout.preferredWidth: theme.scaled(14)
                            }
                            Components.ToneTextField {
                                id: pinXField
                                objectName: "photoTextDimensionPin_" + modelData.key + "X"
                                Layout.fillWidth: true
                                Layout.minimumWidth: theme.scaled(60)
                                Layout.maximumWidth: Math.max(theme.scaled(60), root.width - theme.scaled(32))
                                property bool draftActive: false
                                property int capturedIndex: -1
                                property string capturedLayerId: ""
                                property string capturedDocumentToken: ""
                                property string capturedRevision: ""
                                property var capturedController: null
                                accessibleLabel: modelData.label + qsTr(" horizontal normalized position")
                                numericEntry: true
                                text: Number(root.cornerPin(modelData.key).x).toFixed(3)
                                onTextEdited: root.captureEditorTarget(pinXField)
                                onEditingFinished: {
                                    if (draftActive)
                                        root.commitCornerPin(modelData.key, "x", text,
                                            capturedIndex, capturedLayerId,
                                            capturedDocumentToken, capturedRevision,
                                            capturedController)
                                    root.clearEditorTarget(pinXField)
                                    text = Qt.binding(function() {
                                        return Number(root.cornerPin(modelData.key).x).toFixed(3)
                                    })
                                }
                            }
                            Label {
                                text: "Y"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                Layout.preferredWidth: theme.scaled(14)
                            }
                            Components.ToneTextField {
                                id: pinYField
                                objectName: "photoTextDimensionPin_" + modelData.key + "Y"
                                Layout.fillWidth: true
                                Layout.minimumWidth: theme.scaled(60)
                                Layout.maximumWidth: Math.max(theme.scaled(60), root.width - theme.scaled(32))
                                property bool draftActive: false
                                property int capturedIndex: -1
                                property string capturedLayerId: ""
                                property string capturedDocumentToken: ""
                                property string capturedRevision: ""
                                property var capturedController: null
                                accessibleLabel: modelData.label + qsTr(" vertical normalized position")
                                numericEntry: true
                                text: Number(root.cornerPin(modelData.key).y).toFixed(3)
                                onTextEdited: root.captureEditorTarget(pinYField)
                                onEditingFinished: {
                                    if (draftActive)
                                        root.commitCornerPin(modelData.key, "y", text,
                                            capturedIndex, capturedLayerId,
                                            capturedDocumentToken, capturedRevision,
                                            capturedController)
                                    root.clearEditorTarget(pinYField)
                                    text = Qt.binding(function() {
                                        return Number(root.cornerPin(modelData.key).y).toFixed(3)
                                    })
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Components.InspectorSection {
        objectName: "photoTextDimensionSurfaceSection"
        Layout.fillWidth: true
        title: qsTr("Surface and light")
        subtitle: qsTr("Front, side, bevel, and directional finish")
        helpText: qsTr("3D uses these surface colors and lights. Existing flat photo materials return when 3D is off.")
        expandable: true
        expanded: root.surfaceExpanded
        resetAvailable: root.groupChanged(["useTextColor", "frontColor", "sideColor", "bevelColor", "roughness", "metallic", "lightAzimuth", "lightElevation", "lightIntensity", "ambient"])
        resetObjectName: "photoTextDimensionResetSurface"
        resetTooltip: qsTr("Reset surface and lighting")
        enabled: root.canEdit
        onToggled: root.surfaceExpanded = !root.surfaceExpanded
        onResetRequested: root.resetSurface()

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space4

            Components.ToneCheckBox {
                objectName: "photoTextDimensionUseTextColor"
                Layout.fillWidth: true
                text: qsTr("Use text color for the front")
                accessibleLabel: qsTr("Use text color for front surface")
                checked: root.dimension.useTextColor === true
                enabled: root.canEdit
                onToggled: function() { root.writePatch({useTextColor: checked}) }
            }

            Repeater {
                model: [
                    { key: "frontColor", label: qsTr("Front color") },
                    { key: "sideColor", label: qsTr("Side color") },
                    { key: "bevelColor", label: qsTr("Bevel color") }
                ]
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: theme.space8
                    Label {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= theme.scaled(400) ? 2 : 1
                        columnSpacing: theme.space8
                        rowSpacing: theme.space4
                        Components.ColorSwatchButton {
                            objectName: "photoTextDimensionSwatch_" + modelData.key
                            Layout.fillWidth: true
                            swatchColor: root.currentColor(modelData.key)
                            label: modelData.label + ": " + root.currentColor(modelData.key)
                            enabled: root.canEdit && !(modelData.key === "frontColor" && root.dimension.useTextColor === true)
                            onPicked: root.openColorDialog(modelData.key)
                        }
                        Components.ToneTextField {
                            id: colorEntry
                            objectName: "photoTextDimensionColor_" + modelData.key
                            Layout.fillWidth: true
                            Layout.minimumWidth: theme.scaled(72)
                            Layout.maximumWidth: Math.max(theme.scaled(72), root.width - theme.scaled(32))
                            property bool draftActive: false
                            property int capturedIndex: -1
                            property string capturedLayerId: ""
                            property string capturedDocumentToken: ""
                            property string capturedRevision: ""
                            property var capturedController: null
                            accessibleLabel: modelData.label
                            text: root.currentColor(modelData.key)
                            enabled: root.canEdit && !(modelData.key === "frontColor" && root.dimension.useTextColor === true)
                            onTextEdited: root.captureEditorTarget(colorEntry)
                            onEditingFinished: {
                                if (draftActive)
                                    root.commitHexColorForTarget(modelData.key, text,
                                        capturedIndex, capturedLayerId,
                                        capturedDocumentToken, capturedRevision,
                                        capturedController)
                                root.clearEditorTarget(colorEntry)
                                text = Qt.binding(function() { return root.currentColor(modelData.key) })
                            }
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(480) ? 2 : 1
                columnSpacing: theme.space8
                rowSpacing: theme.space4

                Repeater {
                    model: [
                        { key: "roughness", label: qsTr("Roughness"), from: 0, to: 1, step: 0.01, digits: 2 },
                        { key: "metallic", label: qsTr("Metallic"), from: 0, to: 1, step: 0.01, digits: 2 },
                        { key: "lightAzimuth", label: qsTr("Light azimuth"), from: -180, to: 180, step: 1, digits: 0 },
                        { key: "lightElevation", label: qsTr("Light elevation"), from: -90, to: 90, step: 1, digits: 0 },
                        { key: "lightIntensity", label: qsTr("Light intensity"), from: 0, to: 4, step: 0.01, digits: 2 },
                        { key: "ambient", label: qsTr("Ambient"), from: 0, to: 1, step: 0.01, digits: 2 }
                    ]
                    delegate: Components.InspectorSlider {
                        required property var modelData
                        Layout.fillWidth: true
                        stackedHeader: root.width < theme.scaled(300)
                        objectName: "photoTextDimension_" + modelData.key
                        label: modelData.label
                        from: modelData.from
                        to: modelData.to
                        stepSize: modelData.step
                        defaultValue: Number(root.defaults[modelData.key])
                        value: Number(root.dimension[modelData.key])
                        valueText: Number(root.dimension[modelData.key]).toFixed(modelData.digits)
                        enabled: root.canEdit
                        onInteractionStarted: root.beginDimensionEdit()
                        onInteractionFinished: root.endDimensionEdit()
                        onValueInputDirtyChanged: {
                            if (valueInputDirty) root.beginDimensionEdit()
                            else if (!pressed) Qt.callLater(function() {
                                if (root.editSessionOpen && !root.editSessionInvalid) root.endDimensionEdit()
                            })
                        }
                        onValueDragged: function(value) {
                            root.writePatch(root.fieldPatch(modelData.key,
                                Number(value.toFixed(modelData.digits))))
                            if (!pressed) root.endDimensionEdit()
                        }
                    }
                }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        objectName: "photoTextDimensionColorDialog"
        title: qsTr("Choose text surface color")
        options: ColorDialog.ShowAlphaChannel
        onAccepted: {
            if (root.dialogTargetMatches() && root.editingColorKey.length > 0) {
                const patch = ({})
                patch[root.editingColorKey] = String(selectedColor)
                root.writePatch(patch)
            }
            root.closeColorDialog()
        }
        onRejected: root.closeColorDialog()
        onVisibleChanged: if (!visible && root.dialogLayerIndex >= 0) root.closeColorDialog()
    }
}
