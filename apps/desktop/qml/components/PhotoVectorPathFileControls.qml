pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "." as Components

ColumnLayout {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    property string importAssetId: ""
    property string importLayerId: ""
    property string exportAssetId: ""
    property string exportLayerId: ""
    property string pickerStatus: ""

    readonly property string assetId: String(root.controller && root.controller.assetId || "")
    readonly property string layerId: String(root.layerData && root.layerData.id || "")
    readonly property bool hasPath: !!(root.layerData && root.layerData.vectorPath)
    readonly property bool canImport: root.assetId.length > 0 && root.controller &&
        typeof root.controller.importPhotoVectorSvg === "function" &&
        !root.controller.professionalToolsBusy
    readonly property bool canExport: root.layerIndex >= 0 && root.assetId.length > 0 &&
        root.layerId.length > 0 && root.hasPath && root.controller &&
        typeof root.controller.exportPhotoVectorSvg === "function" &&
        !root.controller.professionalToolsBusy

    Theme { id: theme }
    spacing: theme.gapDense
    implicitWidth: theme.scaled(250)

    function layerIndexForId(id) {
        const layers = root.controller && root.controller.photoLayers
            ? root.controller.photoLayers : []
        for (let index = 0; index < layers.length; ++index) {
            if (String(layers[index].id || "") === String(id || ""))
                return index
        }
        return -1
    }

    function localPath(url) {
        if (url && typeof url.toLocalFile === "function")
            return String(url.toLocalFile())
        const text = String(url || "")
        if (text.indexOf("file://") === 0) {
            try {
                return decodeURIComponent(text.slice(7))
            } catch (error) {
                return ""
            }
        }
        return text
    }

    function openImportPicker() {
        if (!root.canImport)
            return false
        root.pickerStatus = ""
        root.importAssetId = root.assetId
        root.importLayerId = root.layerId
        importDialog.open()
        return true
    }

    function acceptImport(fileUrl) {
        const capturedAssetId = root.importAssetId
        const capturedLayerId = root.importLayerId
        root.importAssetId = ""
        root.importLayerId = ""
        const path = root.localPath(fileUrl)
        if (!capturedAssetId || !path.length ||
            capturedAssetId !== String(root.controller.assetId || "") ||
            (capturedLayerId.length > 0 && root.layerIndexForId(capturedLayerId) < 0)) {
            root.pickerStatus = qsTr("The selected asset or layer changed. Open the picker again.")
            return false
        }
        if (!root.controller.importPhotoVectorSvg(capturedAssetId, path)) {
            root.pickerStatus = qsTr("SVG import could not start. The document may have changed or another operation may be running.")
            return false
        }
        return true
    }

    function openExportPicker() {
        if (!root.canExport)
            return false
        root.pickerStatus = ""
        root.exportAssetId = root.assetId
        root.exportLayerId = root.layerId
        exportDialog.open()
        return true
    }

    function acceptExport(fileUrl) {
        const capturedAssetId = root.exportAssetId
        const capturedLayerId = root.exportLayerId
        root.exportAssetId = ""
        root.exportLayerId = ""
        const path = root.localPath(fileUrl)
        const index = root.layerIndexForId(capturedLayerId)
        if (!capturedAssetId || !capturedLayerId || !path.length || index < 0 ||
            capturedAssetId !== String(root.controller.assetId || "") ||
            !root.controller.photoLayers[index].vectorPath) {
            root.pickerStatus = qsTr("The selected asset or path changed. Open the picker again.")
            return false
        }
        if (!root.controller.exportPhotoVectorSvg(capturedAssetId, capturedLayerId, path)) {
            root.pickerStatus = qsTr("SVG export could not start. The selected path may have changed or another operation may be running.")
            return false
        }
        return true
    }

    function clearImportAnchor() {
        root.importAssetId = ""
        root.importLayerId = ""
    }

    function clearExportAnchor() {
        root.exportAssetId = ""
        root.exportLayerId = ""
    }

    FileDialog {
        id: importDialog
        objectName: "photoVectorPathImportDialog"
        title: qsTr("Import editable SVG paths")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("SVG vector files (*.svg)"), qsTr("All files (*)")]
        onAccepted: root.acceptImport(selectedFile)
        onRejected: root.clearImportAnchor()
    }

    FileDialog {
        id: exportDialog
        objectName: "photoVectorPathExportDialog"
        title: qsTr("Export editable SVG path")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "svg"
        nameFilters: [qsTr("SVG vector files (*.svg)")]
        onAccepted: root.acceptExport(selectedFile)
        onRejected: root.clearExportAnchor()
    }

    ColumnLayout {
        id: content
        Layout.fillWidth: true
        spacing: theme.gapDense

        Label {
            objectName: "photoVectorPathFileTitle"
            text: qsTr("SVG path interchange")
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeSection
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Keep path contours editable with solid fill and stroke styling.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }

        Flow {
            objectName: "photoVectorPathFileActionFlow"
            Layout.fillWidth: true
            spacing: theme.gapDense

            Components.ActionButton {
                objectName: "photoVectorPathImportButton"
                text: qsTr("Import SVG")
                iconName: "import"
                tone: "primary"
                compact: true
                enabled: root.canImport
                tooltipText: qsTr("Import supported SVG paths as editable vector layers.")
                onClicked: root.openImportPicker()
            }

            Components.MiniButton {
                objectName: "photoVectorPathExportButton"
                text: qsTr("Export SVG")
                iconName: "export"
                showLeadingIcon: true
                role: "secondary"
                enabled: root.canExport
                tooltip: qsTr("Export path geometry and solid fill/stroke only. Masks, effects and layer placement are not included.")
                onClicked: root.openExportPicker()
            }
        }

        Label {
            objectName: "photoVectorPathFilePickerStatus"
            Layout.fillWidth: true
            visible: root.pickerStatus.length > 0
            text: root.pickerStatus
            color: theme.warning
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }
    }
}
