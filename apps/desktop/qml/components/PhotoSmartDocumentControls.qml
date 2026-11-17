pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    readonly property bool component: !!root.layerData.smartObjectDocument
    readonly property bool canConvert: !root.component && root.layerData.baseImageEdit !== true && !root.layerData.parentId &&
        ["group", "text", "fill", "pixel", "source"].indexOf(String(root.layerData.kind)) >= 0
    readonly property bool editable: root.layerIndex >= 0 && !root.locked && !root.controller.professionalToolsBusy
    Theme { id: theme }
    visible: root.component || root.canConvert
    spacing: theme.space6
    Label { text: qsTr("Editable component"); color: theme.textMain; font.family: theme.fontUi; font.weight: Font.DemiBold }
    Label {
        Layout.fillWidth: true
        text: root.component ? qsTr("Edit the layers inside. Apply once to update shared placements.")
                             : qsTr("Keep this layer or group as editable, reusable contents.")
        wrapMode: Text.WordWrap; color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption
    }
    ActionButton {
        objectName: "photoComponentCreateButton"
        visible: root.canConvert; enabled: root.editable
        text: qsTr("Create component"); compact: true
        onClicked: root.controller.convertPhotoLayerToComponent(root.layerIndex)
    }
    Flow {
        Layout.fillWidth: true; spacing: theme.space6; visible: root.component
        MiniButton { objectName: "photoComponentEditButton"; text: qsTr("Edit contents…"); enabled: root.editable; onClicked: root.controller.beginPhotoComponentEditing(root.layerIndex) }
        MiniButton { objectName: "photoComponentDuplicateButton"; text: qsTr("Duplicate shared"); enabled: root.editable; onClicked: root.controller.duplicatePhotoComponent(root.layerIndex) }
        MiniButton { objectName: "photoComponentIndependentButton"; text: qsTr("Make independent"); enabled: root.editable; onClicked: root.controller.makePhotoComponentIndependent(root.layerIndex) }
    }
}
