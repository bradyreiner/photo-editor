pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

ColumnLayout {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    // The layer map is the usual source. An explicit binding is also accepted
    // for workspaces that expose the selected mask separately.
    property var vectorMask: undefined
    property bool densityEditOpen: false
    property bool densityGestureAborted: false
    property string densityEditLayerId: ""

    readonly property var resolvedVectorMask: root.vectorMask !== undefined
        ? root.vectorMask
        : (root.layerData && root.layerData.vectorMask ? root.layerData.vectorMask : null)
    readonly property bool attached: root.resolvedVectorMask !== null
    readonly property bool sourceDocumentBasis: root.attached &&
        String(root.resolvedVectorMask.basis || "local-layer") === "source-document"
    readonly property bool layerLocked: root.locked ||
        (root.layerData && root.layerData.locked === true)
    readonly property bool canAttach: !root.layerLocked && root.layerIndex >= 0 &&
        !root.attached
    readonly property bool canEdit: !root.layerLocked && root.layerIndex >= 0 &&
        root.attached && root.controller &&
        typeof root.controller.setPhotoLayerVectorMaskEnabled === "function" &&
        typeof root.controller.setPhotoLayerVectorMaskInverted === "function" &&
        typeof root.controller.setPhotoLayerVectorMaskDensity === "function" &&
        typeof root.controller.removePhotoLayerVectorMask === "function" &&
        typeof root.controller.beginCoalescedEdit === "function" &&
        typeof root.controller.endCoalescedEdit === "function"
    readonly property bool canChangeBasis: root.canEdit &&
        typeof root.controller.setPhotoLayerVectorMaskBasis === "function"
    readonly property real densityValue: {
        const value = root.resolvedVectorMask ? Number(root.resolvedVectorMask.density) : 1.0
        return Number.isFinite(value) ? Math.max(0, Math.min(1, value)) : 1.0
    }

    signal attachRequested()
    signal editRequested()

    function beginDensityEdit() {
        if (!root.canEdit || root.densityEditOpen)
            return
        root.densityEditLayerId = String(root.layerData && root.layerData.id || "")
        root.controller.beginCoalescedEdit()
        root.densityEditOpen = true
    }

    function endDensityEdit() {
        if (!root.densityEditOpen)
            return
        if (densitySlider.pressed)
            root.densityGestureAborted = true
        root.densityEditOpen = false
        root.densityEditLayerId = ""
        root.controller.endCoalescedEdit()
    }

    function setDensity(value) {
        if (!root.canEdit || root.densityGestureAborted)
            return
        const ownsEdit = !root.densityEditOpen
        if (ownsEdit)
            root.beginDensityEdit()
        try {
            root.controller.setPhotoLayerVectorMaskDensity(root.layerIndex, value)
        } finally {
            if (ownsEdit)
                root.endDensityEdit()
        }
    }

    onLayerIndexChanged: root.endDensityEdit()
    onLockedChanged: if (root.layerLocked) root.endDensityEdit()
    onCanEditChanged: if (!root.canEdit) root.endDensityEdit()
    onLayerDataChanged: {
        const currentLayerId = String(root.layerData && root.layerData.id || "")
        if (root.densityEditOpen && currentLayerId !== root.densityEditLayerId)
            root.endDensityEdit()
    }
    Component.onDestruction: root.endDensityEdit()

    Theme { id: theme }
    spacing: theme.space6
    implicitWidth: theme.scaled(260)

    ColumnLayout {
        id: content
        Layout.fillWidth: true
        spacing: theme.gapDense

        Label {
            objectName: "photoVectorMaskTitle"
            text: qsTr("Vector mask")
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeSection
            font.weight: Font.DemiBold
        }

        Label {
            objectName: "photoVectorMaskSummary"
            Layout.fillWidth: true
            text: root.attached
                ? qsTr("Editable cubic path · soft coverage at any scale")
                : qsTr("Keep a closed vector path editable as this layer’s mask.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }

        Label {
            objectName: "photoVectorMaskBasisGuidance"
            Layout.fillWidth: true
            visible: root.attached
            text: root.sourceDocumentBasis
                ? qsTr("Fixed to document · stays anchored to source pixels when this layer moves.")
                : qsTr("Follow layer · moves with this layer’s position, scale, and rotation.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }

        Label {
            objectName: "photoVectorMaskBasisLabel"
            Layout.fillWidth: true
            visible: root.attached
            text: qsTr("Mask coordinates")
            color: theme.textMain
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: Font.DemiBold
        }

        Flow {
            objectName: "photoVectorMaskBasisChoices"
            Layout.fillWidth: true
            visible: root.attached
            spacing: theme.gapDense

            Components.MiniButton {
                objectName: "photoVectorMaskFollowLayerButton"
                text: qsTr("Follow layer")
                role: "segmented"
                active: !root.sourceDocumentBasis
                enabled: root.canChangeBasis
                tooltip: qsTr("Keep the mask in layer coordinates so it follows this layer’s position, scale, and rotation.")
                a11yName: qsTr("Follow layer coordinates")
                onClicked: {
                    if (root.sourceDocumentBasis)
                        root.controller.setPhotoLayerVectorMaskBasis(root.layerIndex, "local-layer")
                }
            }

            Components.MiniButton {
                objectName: "photoVectorMaskFixedDocumentButton"
                text: qsTr("Fixed to document")
                role: "segmented"
                active: root.sourceDocumentBasis
                enabled: root.canChangeBasis
                tooltip: qsTr("Keep the mask anchored to source-document pixels when this layer moves, scales, or rotates.")
                a11yName: qsTr("Fixed to document coordinates")
                onClicked: {
                    if (!root.sourceDocumentBasis)
                        root.controller.setPhotoLayerVectorMaskBasis(root.layerIndex, "source-document")
                }
            }
        }

        Components.ActionButton {
            objectName: "photoVectorMaskAttachButton"
            Layout.fillWidth: true
            visible: !root.attached
            text: qsTr("Create vector mask")
            iconName: "masks"
            tone: "primary"
            compact: true
            enabled: root.canAttach
            tooltipText: qsTr("Draw and close a cubic path to attach it as this layer’s editable mask.")
            onClicked: root.attachRequested()
        }

        Flow {
            objectName: "photoVectorMaskPathActions"
            Layout.fillWidth: true
            visible: root.attached
            spacing: theme.gapDense

            Components.MiniButton {
                objectName: "photoVectorMaskEditPathButton"
                text: qsTr("Edit path")
                iconName: "edit"
                showLeadingIcon: true
                role: "secondary"
                enabled: root.canEdit
                tooltip: qsTr("Revise the editable cubic mask path.")
                onClicked: root.editRequested()
            }

            Components.MiniButton {
                objectName: "photoVectorMaskRemoveButton"
                text: qsTr("Remove")
                iconName: "close"
                showLeadingIcon: true
                role: "ghost"
                enabled: root.canEdit
                tooltip: qsTr("Remove this vector mask from the layer.")
                onClicked: root.controller.removePhotoLayerVectorMask(root.layerIndex)
            }
        }

        Components.ToneSwitch {
            objectName: "photoVectorMaskEnabledToggle"
            Layout.fillWidth: true
            visible: root.attached
            text: qsTr("Mask enabled")
            accessibleLabel: qsTr("Enable vector mask")
            checked: root.resolvedVectorMask ? root.resolvedVectorMask.enabled !== false : true
            enabled: root.canEdit
            onToggled: root.controller.setPhotoLayerVectorMaskEnabled(
                           root.layerIndex, checked)
        }

        Components.ToneSwitch {
            objectName: "photoVectorMaskInvertToggle"
            Layout.fillWidth: true
            visible: root.attached
            text: qsTr("Invert coverage")
            accessibleLabel: qsTr("Invert vector mask coverage")
            checked: root.resolvedVectorMask ? Boolean(root.resolvedVectorMask.invert) : false
            enabled: root.canEdit
            onToggled: root.controller.setPhotoLayerVectorMaskInverted(
                           root.layerIndex, checked)
        }

        ColumnLayout {
            objectName: "photoVectorMaskDensityControls"
            Layout.fillWidth: true
            visible: root.attached
            enabled: root.canEdit
            spacing: theme.gapDense

            Components.InspectorSlider {
                id: densitySlider
                objectName: "photoVectorMaskDensitySlider"
                Layout.fillWidth: true
                label: qsTr("Density")
                from: 0.0
                to: 1.0
                stepSize: 0.01
                value: root.densityValue
                valueText: Math.round(value * 100) + "%"
                defaultValue: 1.0
                valueInputEnabled: false
                showResetButton: false
                enabled: root.canEdit
                onInteractionStarted: {
                    root.densityGestureAborted = false
                    root.beginDensityEdit()
                }
                onInteractionFinished: {
                    root.endDensityEdit()
                    root.densityGestureAborted = false
                }
                onValueDragged: function(nextValue) { root.setDensity(nextValue) }
            }
        }
    }
}
