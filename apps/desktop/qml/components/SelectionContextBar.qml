pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    required property var controller
    required property var workspace

    Theme { id: theme }

    readonly property int layerIndex: workspace.selectedEditTargetLayerIndex()
    readonly property int targetLayerIndex: {
        if (!controller.editingLayerMask)
            return layerIndex
        const maskOwnerId = String(controller.maskEditLayerId || "")
        if (maskOwnerId.length > 0 && typeof workspace.photoLayerIndexForId === "function") {
            const ownerIndex = Number(workspace.photoLayerIndexForId(maskOwnerId))
            return ownerIndex >= 0 ? ownerIndex : -1
        }
        return layerIndex
    }
    readonly property var layerData: targetLayerIndex >= 0
                                      ? workspace.photoLayerRows[targetLayerIndex] || ({}) : ({})
    readonly property bool targetLayerLocked: isLayerLocked(layerData)
    readonly property bool inheritedLayerLock: targetLayerLocked && layerData.locked !== true
    readonly property bool canMaskLayer: targetLayerIndex >= 0 && controller.hasWorkingSelection &&
                                         !controller.selectionWorkBusy && !targetLayerLocked
    readonly property string targetName: String(layerData.displayName || layerData.name || "Base image")
    readonly property string cutoutDisabledReason: typeof workspace.selectionCutoutDisabledReason === "function"
        ? workspace.selectionCutoutDisabledReason() : qsTr("Cutout is unavailable in this workspace.")

    implicitHeight: content.implicitHeight
    visible: controller.hasWorkingSelection || controller.editingLayerMask

    function isLayerLocked(layer) {
        if (!layer || !layer.id)
            return false
        if (typeof workspace.professionalLayerLocked === "function")
            return workspace.professionalLayerLocked(layer) === true

        const seen = ({})
        let current = layer
        for (let depth = 0; current && depth < 64; ++depth) {
            const id = String(current.id || "")
            if (current.locked === true || seen[id])
                return true
            seen[id] = true
            const parentId = String(current.parentId || "")
            if (parentId.length === 0)
                return false
            current = (workspace.photoLayerRows || []).find(item => String(item.id || "") === parentId)
        }
        return current !== null && current !== undefined
    }

    function maskDisabledReason() {
        if (!controller.hasWorkingSelection)
            return qsTr("Create or load a working selection first.")
        if (controller.selectionWorkBusy)
            return qsTr("Wait for selection processing to finish.")
        if (targetLayerIndex < 0)
            return qsTr("Select a photo layer before adding a mask.")
        if (layerData.locked === true)
            return qsTr("Unlock this layer before adding a mask.")
        if (inheritedLayerLock)
            return qsTr("Unlock the containing group before adding a mask.")
        return ""
    }

    function returnToLayerArtwork() {
        if (!controller.editingLayerMask || targetLayerIndex < 0 ||
                typeof workspace.editPhotoLayerInspectorTarget !== "function")
            return false
        // Let the workspace own the transition. A failed switch leaves the
        // current mask target and the independent working selection intact.
        return workspace.editPhotoLayerInspectorTarget(targetLayerIndex) === true
    }

    function menuItems() {
        const selected = controller.hasWorkingSelection
        const items = [
            {key: "reveal-selection", label: qsTr("Mask layer: reveal selected area"), icon: "masks", enabled: canMaskLayer, disabledReason: maskDisabledReason()},
            {key: "hide-selection", label: qsTr("Mask layer: hide selected area"), icon: "masks", enabled: canMaskLayer, disabledReason: maskDisabledReason()},
            {key: "linked-cutout", label: qsTr("Create linked cutout"), icon: "source", enabled: root.cutoutDisabledReason.length === 0, disabledReason: root.cutoutDisabledReason, separatorBefore: true},
            {key: "export-cutout", label: qsTr("Export cutout PNG…"), icon: "export", enabled: root.cutoutDisabledReason.length === 0 && !controller.photoExportBusy && typeof workspace.chooseCutoutExport === "function"},
            {key: "refine", label: qsTr("Refine selection"), icon: "brush-tool", enabled: selected},
            {key: "invert", label: qsTr("Invert selection"), icon: "compare", shortcut: "Ctrl/Cmd+Shift+I", enabled: selected},
            {key: "reselect", label: qsTr("Reselect"), icon: "select", shortcut: "Ctrl/Cmd+Shift+D", enabled: controller.canReselect},
            {key: "transform", label: qsTr("Transform selection…"), icon: "geometry", enabled: selected && !controller.selectionWorkBusy},
            {key: "save", label: qsTr("Save selection…"), icon: "save", enabled: selected}
        ]
        for (const selection of controller.savedSelections || [])
            items.push({key: "load:" + String(selection.id), label: qsTr("Load ") + String(selection.name), icon: "masks"})
        return items
    }

    function trigger(key) {
        if (key === "invert" || key === "reselect") workspace.queueSelectionAction(key)
        else if (key === "reveal-selection" || key === "hide-selection") workspace.applySelectionToCurrentLayer(key === "hide-selection")
        else if (key === "refine") { controller.activateWorkingSelection(); workspace.activateEditTool("masks") }
        else if (key === "linked-cutout") workspace.createCutoutFromWorkingSelection(false)
        else if (key === "export-cutout") workspace.chooseCutoutExport()
        else if (key === "save") { selectionName.text = qsTr("Selection ") + ((controller.savedSelections || []).length + 1); savePopup.open() }
        else if (key === "transform") transformPopup.open()
        else if (key.indexOf("load:") === 0) controller.loadMaskAsSelection(key.slice(5))
    }

    function layerMenuItems() {
        const items = []
        if (!workspace.componentWorkspaceActive)
            items.push({key: "base-image", label: qsTr("Base image"), icon: "media",
                        checked: targetLayerIndex < 0 || layerData.baseImageEdit === true})
        for (const layer of workspace.photoLayerRows || []) {
            if (layer.baseImageEdit === true) continue
            items.push({key: "layer:" + String(layer.id), label: String(layer.displayName || layer.name || qsTr("Layer")),
                        icon: layer.kind === "adjustment" ? "adjust" : "layers", checked: layer.id === layerData.id})
        }
        return items
    }

    GridLayout {
        id: content
        anchors.fill: parent
        columns: root.width >= theme.scaled(560) ? 2 : 1
        columnSpacing: theme.space8
        rowSpacing: theme.space4

        MenuButton {
            objectName: "selectionLayerTargetMenu"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: theme.scaled(260)
            text: (root.targetName.length > 25 ? root.targetName.slice(0, 24) + "…" : root.targetName) +
                  (root.controller.editingLayerMask ? qsTr(" · Mask") : "")
            accessibleName: qsTr("Target layer: %1").arg(root.targetName)
            compact: true
            primaryOpensMenu: true
            tooltipText: qsTr("Switch layer: %1").arg(root.targetName)
            menuItems: root.layerMenuItems()
            onMenuItemTriggered: function(key) { root.workspace.switchSelectionLayerTarget(key) }
        }

        Flow {
            Layout.fillWidth: true
            spacing: theme.space4
            MiniButton {
                objectName: "selectionEditArtworkButton"
                visible: root.controller.editingLayerMask
                text: qsTr("Edit layer")
                enabled: root.targetLayerIndex >= 0
                tooltip: qsTr("Return to the layer while keeping the selection.")
                onClicked: root.returnToLayerArtwork()
            }
            MiniButton {
                objectName: "cutoutWorkingSelectionButton"
                visible: root.controller.hasWorkingSelection
                text: qsTr("Cutout")
                role: "primary"
                enabled: root.cutoutDisabledReason.length === 0
                disabledReason: root.cutoutDisabledReason
                tooltip: qsTr("Copy selected pixels to a new transparent layer. Ctrl/Cmd+J.")
                onClicked: root.workspace.createCutoutFromWorkingSelection(true)
            }
            MiniButton {
                objectName: "adjustWorkingSelectionButton"
                visible: root.controller.hasWorkingSelection && !root.controller.editingLayerMask
                text: qsTr("Adjust")
                enabled: !root.controller.selectionWorkBusy && typeof root.workspace.createAdjustmentFromWorkingSelection === "function"
                disabledReason: qsTr("Wait for selection processing to finish.")
                tooltip: qsTr("Adjust the selected area on a new masked layer.")
                onClicked: root.workspace.createAdjustmentFromWorkingSelection()
            }
            MenuButton {
                objectName: "workingSelectionMenu"
                text: qsTr("More")
                compact: true
                showMenuIndicator: false
                primaryOpensMenu: true
                menuItems: root.menuItems()
                onMenuItemTriggered: function(key) { root.trigger(key) }
            }
            MiniButton {
                objectName: "workingSelectionDeselectButton"
                visible: root.controller.hasWorkingSelection
                square: true
                iconName: "close"
                a11yName: qsTr("Deselect")
                tooltip: qsTr("Deselect. Ctrl/Cmd+D.")
                onClicked: root.workspace.queueSelectionAction("deselect")
            }
        }
    }

    AppPopup {
        id: savePopup
        objectName: "selectionSavePopup"
        focus: true
        onOpened: selectionName.forceActiveFocus(Qt.TabFocusReason)
        width: Math.min(theme.scaled(340), popupAvailableWidth)
        implicitHeight: saveFields.implicitHeight + theme.space16
        ColumnLayout {
            id: saveFields
            anchors.fill: parent
            spacing: theme.space8
            Label { text: qsTr("Save Selection"); color: theme.textMain; font.family: theme.fontUi }
            ToneTextField { id: selectionName; objectName: "savedSelectionName"; Layout.fillWidth: true; selectByMouse: true; accessibleLabel: qsTr("Selection name") }
            ActionButton {
                text: qsTr("Save")
                Layout.fillWidth: true
                enabled: selectionName.text.trim().length > 0
                onClicked: { if (root.controller.saveWorkingSelection(selectionName.text)) savePopup.close() }
            }
        }
    }

    AppPopup {
        id: transformPopup
        objectName: "selectionTransformPopup"
        focus: true
        onOpened: moveX.forceActiveFocus(Qt.TabFocusReason)
        width: Math.min(theme.scaled(340), popupAvailableWidth)
        implicitHeight: transformFields.implicitHeight + theme.space16
        ColumnLayout {
            id: transformFields
            anchors.fill: parent
            spacing: theme.space8
            Label { text: qsTr("Transform Selection"); color: theme.textMain; font.family: theme.fontUi }
            Label { text: qsTr("Move, scale or rotate the outline. Layer pixels stay in place."); Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textMuted; font.family: theme.fontUi }
            GridLayout {
                Layout.fillWidth: true
                columns: width >= theme.scaled(300) ? 2 : 1
                columnSpacing: theme.space6
                rowSpacing: theme.space4
                Label { text: qsTr("Move X (%)"); color: theme.textMain }
                ToneTextField {
                    id: moveX
                    objectName: "selectionTransformMoveX"
                    text: "0"
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(72)
                    Layout.preferredWidth: theme.scaled(104)
                    numericEntry: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    accessibleLabel: qsTr("Move X percent")
                    validator: DoubleValidator { locale: "C" }
                }
                Label { text: qsTr("Move Y (%)"); color: theme.textMain }
                ToneTextField {
                    id: moveY
                    objectName: "selectionTransformMoveY"
                    text: "0"
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(72)
                    Layout.preferredWidth: theme.scaled(104)
                    numericEntry: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    accessibleLabel: qsTr("Move Y percent")
                    validator: DoubleValidator { locale: "C" }
                }
                Label { text: qsTr("Scale (%)"); color: theme.textMain }
                ToneTextField {
                    id: selectionScaleInput
                    objectName: "selectionTransformScale"
                    text: "100"
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(72)
                    Layout.preferredWidth: theme.scaled(104)
                    numericEntry: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    accessibleLabel: qsTr("Selection scale percent")
                    validator: DoubleValidator { bottom: 1; top: 10000; locale: "C" }
                }
                Label { text: qsTr("Rotation (°)"); color: theme.textMain }
                ToneTextField {
                    id: selectionRotationInput
                    objectName: "selectionTransformRotation"
                    text: "0"
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(72)
                    Layout.preferredWidth: theme.scaled(104)
                    numericEntry: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    accessibleLabel: qsTr("Selection rotation degrees")
                    validator: DoubleValidator { locale: "C" }
                }
            }
            ActionButton {
                text: qsTr("Apply")
                Layout.fillWidth: true
                enabled: !root.controller.selectionWorkBusy && moveX.acceptableInput && moveY.acceptableInput && selectionScaleInput.acceptableInput && selectionRotationInput.acceptableInput
                onClicked: {
                    if (root.controller.transformWorkingSelection({x: Number(moveX.text) / 100, y: Number(moveY.text) / 100,
                        scaleX: Number(selectionScaleInput.text) / 100, scaleY: Number(selectionScaleInput.text) / 100, rotation: Number(selectionRotationInput.text)})) transformPopup.close()
                }
            }
        }
    }
}
