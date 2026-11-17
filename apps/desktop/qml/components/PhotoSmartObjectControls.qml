pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    property bool linkedContentAllowed: true
    property string replacementLayerId: ""
    property string replacementAssetId: ""
    readonly property var content: layerData.smartObject || ({})
    readonly property var linkStatus: {
        const statuses = root.controller.smartObjectLinkStatuses || ({})
        return statuses[String(root.layerData.id)] || ({status: "unchecked"})
    }
    readonly property string linkDescriptorKey: [
        String(root.controller.assetId || ""),
        String(root.layerData.id || ""), String(root.content.contentId || ""),
        String(root.content.sourcePath || ""), String(root.content.sourceSha256 || "")
    ].join("|")
    readonly property bool isSmartObject: Object.keys(content).length > 0
    readonly property bool isLinked: isSmartObject && String(content.policy) === "linked"
    readonly property bool canConvert: !isSmartObject && layerData.baseImageEdit !== true &&
        (String(layerData.kind) === "source" || (String(layerData.kind) === "pixel" && layerData.pixelData !== undefined)) &&
        layerData.paintTiles === undefined && layerData.vectorPath === undefined && layerData.shapeDefinition === undefined
    readonly property bool canEdit: layerIndex >= 0 && !locked && !controller.professionalToolsBusy
    readonly property bool canCheckLink: layerIndex >= 0 && !controller.professionalToolsBusy
    property string lastAutoCheckKey: ""
    property string pendingLinkCheckKey: ""
    Theme { id: theme }
    spacing: theme.space6
    visible: isSmartObject || canConvert

    function checkLinkedStatusIfNeeded() {
        if (!root.isLinked || !root.canCheckLink) return
        const key = root.linkDescriptorKey
        const state = root.linkStatus.status || "unchecked"
        if (state === "unchecked" && root.lastAutoCheckKey !== key) {
            root.startLinkCheck()
        }
    }

    function startLinkCheck() {
        if (!root.isLinked || !root.canCheckLink) return false
        if (!root.controller.checkPhotoSmartObjectLink(root.layerIndex)) return false
        root.lastAutoCheckKey = root.linkDescriptorKey
        root.pendingLinkCheckKey = root.linkDescriptorKey
        return true
    }

    function layerIndexForId(layerId) {
        const layers = root.controller.photoLayers || []
        for (let index = 0; index < layers.length; ++index) {
            if (String(layers[index].id) === String(layerId)) return index
        }
        return -1
    }

    function openSourcePicker(dialog) {
        root.replacementLayerId = String(root.layerData.id)
        root.replacementAssetId = String(root.controller.assetId || "")
        dialog.open()
    }

    function sourceFileName() {
        let name = String(root.content.fileName || root.content.sourcePath ||
                          root.layerData.sourcePath || qsTr("Source file"))
        const separator = Math.max(name.lastIndexOf("/"), name.lastIndexOf("\\"))
        if (separator >= 0) name = name.slice(separator + 1)
        return name
    }

    onCanEditChanged: Qt.callLater(root.checkLinkedStatusIfNeeded)
    onCanCheckLinkChanged: Qt.callLater(root.checkLinkedStatusIfNeeded)
    onLinkDescriptorKeyChanged: {
        root.lastAutoCheckKey = ""
        root.pendingLinkCheckKey = ""
        Qt.callLater(root.checkLinkedStatusIfNeeded)
    }
    onLinkStatusChanged: {
        if (root.linkStatus.status && root.linkStatus.status !== "unchecked") {
            root.pendingLinkCheckKey = ""
        }
    }
    Component.onCompleted: Qt.callLater(root.checkLinkedStatusIfNeeded)

    Label {
        text: root.isSmartObject ? qsTr("Smart Object") : qsTr("Reusable source")
        color: theme.textMain
        font.family: theme.fontUi
        font.weight: Font.DemiBold
    }
    Label {
        Layout.fillWidth: true
        text: root.controller.professionalToolsBusy ? qsTr("Preparing content…")
            : root.isSmartObject ? qsTr("%1 · Replace once to update shared instances.").arg(root.content.policy === "linked" ? qsTr("Linked file") : qsTr("Embedded source"))
            : qsTr("Preserve this image as reusable source content. Each placement keeps its masks, effects and transform.")
        wrapMode: Text.WordWrap
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: theme.space2
        visible: root.isSmartObject
        Label {
            Layout.fillWidth: true
            objectName: "photoSmartObjectSourceSummary"
            text: root.isLinked
                ? qsTr("%1 · Linked").arg(root.sourceFileName())
                : qsTr("%1 · Embedded").arg(String(root.content.fileName || qsTr("Source content")))
            wrapMode: Text.WrapAnywhere
            color: theme.textMain
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }
        Label {
            Layout.fillWidth: true
            objectName: "photoSmartObjectLinkStatus"
            visible: root.isLinked
            text: {
                const reported = String(root.linkStatus.status || "unchecked")
                const state = reported === "unchecked" &&
                    root.pendingLinkCheckKey === root.linkDescriptorKey ? "checking" : reported
                if (state === "current") return qsTr("Link is current")
                if (state === "missing") return qsTr("Source file is missing")
                if (state === "changed") return qsTr("Source changed on disk")
                if (state === "unreadable") return qsTr("Source file could not be read")
                if (state === "checking") return qsTr("Checking linked source…")
                return qsTr("Link status not checked")
            }
            wrapMode: Text.WordWrap
            color: root.linkStatus.status === "current" ? theme.textMuted : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }
    }
    MenuButton {
        Layout.fillWidth: true
        objectName: "photoSmartObjectCreateButton"
        visible: root.canConvert
        enabled: root.canEdit
        text: qsTr("Create Smart Object")
        primaryOpensMenu: true
        menuItems: [
            {key: "embedded", label: qsTr("Embed source"), icon: "source"},
            {key: "linked", label: qsTr("Keep file linked"), icon: "link",
                enabled: root.linkedContentAllowed && String(root.layerData.kind) === "source",
                disabledReason: !root.linkedContentAllowed
                    ? qsTr("Component images are embedded on Apply. Keep linked sources in the parent document.")
                    : qsTr("Choose an image source layer to keep its file linked.")}
        ]
        onMenuItemTriggered: function(key) { root.controller.convertPhotoLayerToSmartObject(root.layerIndex, key === "linked") }
    }
    Flow {
        Layout.fillWidth: true
        spacing: theme.space6
        visible: root.isSmartObject
        MiniButton {
            objectName: "photoSmartObjectDuplicateButton"
            text: qsTr("Duplicate shared")
            enabled: root.canEdit
            onClicked: root.controller.duplicatePhotoSmartObject(root.layerIndex)
        }
        MiniButton {
            objectName: "photoSmartObjectIndependentButton"
            text: qsTr("Make independent")
            enabled: root.canEdit
            onClicked: root.controller.makePhotoSmartObjectIndependent(root.layerIndex)
        }
        MiniButton {
            objectName: "photoSmartObjectReplaceButton"
            text: qsTr("Replace contents…")
            enabled: root.canEdit && (root.linkedContentAllowed || !root.isLinked)
            disabledReason: qsTr("Embed linked content before replacing it inside a component.")
            onClicked: root.openSourcePicker(replacement)
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: theme.space6
        visible: root.isLinked
        MiniButton {
            objectName: "photoSmartObjectRelinkButton"
            text: qsTr("Relink…")
            enabled: root.canEdit && root.linkedContentAllowed
            disabledReason: qsTr("Component images are embedded. Keep linked sources in the parent document.")
            onClicked: root.openSourcePicker(relink)
        }
        MiniButton {
            objectName: "photoSmartObjectRefreshButton"
            text: qsTr("Update")
            enabled: root.canEdit && root.linkedContentAllowed
            disabledReason: qsTr("Embed linked content before applying it inside a component.")
            onClicked: root.controller.refreshPhotoSmartObjectContents(root.layerIndex)
        }
        MiniButton {
            objectName: "photoSmartObjectEmbedButton"
            text: qsTr("Embed")
            enabled: root.canEdit
            onClicked: root.controller.embedPhotoSmartObjectContents(root.layerIndex)
        }
        MiniButton {
            objectName: "photoSmartObjectCheckLinkButton"
            text: qsTr("Check link")
            enabled: root.canCheckLink
            onClicked: root.startLinkCheck()
        }
    }
    FileDialog {
        id: replacement
        objectName: "photoSmartObjectReplaceDialog"
        title: qsTr("Replace Smart Object contents")
        fileMode: FileDialog.OpenFile
        onAccepted: {
            const layerId = root.replacementLayerId
            const capturedAssetId = root.replacementAssetId
            root.replacementLayerId = ""
            root.replacementAssetId = ""
            if (capturedAssetId !== String(root.controller.assetId || "")) return
            const index = root.layerIndexForId(layerId)
            if (index >= 0) root.controller.replacePhotoSmartObjectContents(index, selectedFile.toString())
        }
    }
    FileDialog {
        id: relink
        objectName: "photoSmartObjectRelinkDialog"
        title: qsTr("Relink Smart Object source")
        fileMode: FileDialog.OpenFile
        onAccepted: {
            const layerId = root.replacementLayerId
            const capturedAssetId = root.replacementAssetId
            root.replacementLayerId = ""
            root.replacementAssetId = ""
            if (capturedAssetId !== String(root.controller.assetId || "")) return
            const index = root.layerIndexForId(layerId)
            if (index >= 0) root.controller.relinkPhotoSmartObjectContents(index, selectedFile.toString())
        }
    }
}
