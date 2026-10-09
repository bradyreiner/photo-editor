pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

StudioCollapsibleGroup {
    id: root
    objectName: "libraryMetadataPanel"
    property var catalogModel: null
    property var draft: ({})
    property var editedFields: ({})
    property string draftAssetId: ""
    property string localMessage: ""
    readonly property bool dirty: Object.keys(editedFields).length > 0
    title: "Metadata"
    expanded: false
    onToggled: expanded = !expanded
    Theme { id: theme }

    function loadDraft() {
        draftAssetId = catalogModel ? catalogModel.selectedAssetId : ""
        draft = catalogModel ? Object.assign({}, catalogModel.selectedMetadata) : ({})
        if (catalogModel && !draft.captureTime) {
            const asset = catalogModel.assetById(draftAssetId)
            if (asset.captureTimestamp) draft.captureTime = asset.captureTimestamp
        }
        editedFields = ({})
        localMessage = ""
    }
    function editField(key, value) {
        let next = Object.assign({}, draft)
        next[key] = value
        draft = next
        let fields = Object.assign({}, editedFields)
        fields[key] = true
        editedFields = fields
    }
    function applyDraft() {
        const patch = ({})
        for (const key of Object.keys(editedFields))
            patch[key] = key === "keywords" ? String(draft[key] || "").split(",") : String(draft[key] || "")
        if (catalogModel && catalogModel.saveMetadataToSelected(patch)) loadDraft()
        else localMessage = "Metadata could not be applied. Check the status below."
    }
    Component.onCompleted: loadDraft()
    Connections {
        target: root.catalogModel
        function onSelectedAssetChanged() {
            if (root.draftAssetId !== root.catalogModel.selectedAssetId || !root.dirty) root.loadDraft()
        }
    }
    Label {
        Layout.fillWidth: true
        text: root.catalogModel && root.catalogModel.selectedAssetCount > 1
              ? "Apply changed fields to " + root.catalogModel.selectedAssetCount + " selected photos."
              : "Keywords and details stay with this photo in the catalog."
        color: theme.textMuted
        font.pixelSize: theme.typeCaption
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: [
            { key: "title", label: "Title", hint: "Photo title" },
            { key: "keywords", label: "Keywords", hint: "Nepal, hiking, family" },
            { key: "caption", label: "Caption", hint: "Describe the photo" },
            { key: "creator", label: "Creator", hint: "Photographer" },
            { key: "copyright", label: "Copyright", hint: "Copyright notice" },
            { key: "location", label: "Location", hint: "Place or venue" },
            { key: "captureTime", label: "Capture time", hint: "2026-10-07T15:30:00-06:00" }
        ]
        delegate: ColumnLayout {
            id: metadataFieldRow
            required property var modelData
            Layout.fillWidth: true
            spacing: 3
            Label { text: metadataFieldRow.modelData.label; color: theme.textMuted; font.pixelSize: theme.typeCaption }
            ToneTextField {
                objectName: "libraryMetadata" + metadataFieldRow.modelData.key + "Field"
                Layout.fillWidth: true
                text: metadataFieldRow.modelData.key === "keywords" && root.draft[metadataFieldRow.modelData.key] && typeof root.draft[metadataFieldRow.modelData.key].join === "function"
                      ? root.draft[metadataFieldRow.modelData.key].join(", ") : String(root.draft[metadataFieldRow.modelData.key] || "")
                placeholderText: metadataFieldRow.modelData.hint
                enabled: root.catalogModel && !root.catalogModel.metadataFileBusy
                onTextEdited: root.editField(metadataFieldRow.modelData.key, text)
                Accessible.name: metadataFieldRow.modelData.label
            }
        }
    }
    ActionButton {
        objectName: "libraryMetadataApplyButton"
        Layout.fillWidth: true
        text: root.catalogModel && root.catalogModel.selectedAssetCount > 1 ? "Apply to selected" : "Save metadata"
        iconName: "check"
        compact: true
        enabled: root.dirty && root.catalogModel && !root.catalogModel.metadataFileBusy
        onClicked: root.applyDraft()
    }
    RowLayout {
        Layout.fillWidth: true
        MiniButton {
            objectName: "libraryMetadataReadXmpButton"
            Layout.fillWidth: true
            text: "Read XMP"
            iconName: "import"
            enabled: !root.dirty && root.catalogModel && !root.catalogModel.metadataFileBusy
            tooltip: "Read standard metadata from sidecars beside the selected originals."
            onClicked: root.catalogModel.readSelectedMetadataSidecars()
        }
        MiniButton {
            objectName: "libraryMetadataWriteXmpButton"
            Layout.fillWidth: true
            text: "Write XMP"
            iconName: "export"
            enabled: !root.dirty && root.catalogModel && !root.catalogModel.metadataFileBusy
            tooltip: "Write keywords and descriptive metadata to XMP; preserve other sidecar fields."
            onClicked: root.catalogModel.writeSelectedMetadataSidecars()
        }
    }
    Label {
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.localMessage.length > 0 ? root.localMessage : root.catalogModel ? root.catalogModel.metadataStatus : ""
        color: theme.textMuted
        font.pixelSize: theme.typeCaption
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    MiniButton {
        objectName: "libraryMetadataRetryButton"
        Layout.fillWidth: true
        visible: root.catalogModel && root.catalogModel.metadataPendingCount > 0
        text: "Retry catalog sync"
        iconName: "retry"
        onClicked: root.catalogModel.retryMetadataPersistence()
    }
}
