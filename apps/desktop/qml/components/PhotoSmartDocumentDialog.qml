pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

AppDialog {
    id: root
    required property var controller
    readonly property var editState: root.controller.smartDocumentEditState || ({active: false})
    readonly property var layers: root.editState.layers || []
    property string selectedId: ""
    readonly property var selected: root.layers.find(function(layer) { return String(layer.id) === root.selectedId }) || ({})
    readonly property bool canEdit: root.editState.active && !root.editState.applying && !root.selected.locked && !root.controller.professionalToolsBusy && root.selectedId.length > 0
    property string formError: ""
    property string imageSession: ""
    property string imageChild: ""
    title: qsTr("Edit component contents")
    width: Math.min(popupAvailableWidth, 880)
    height: Math.min(popupAvailableHeight, 680)
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape
    Theme { id: theme }

    function syncInputs() {
        if (!nameField.activeFocus) nameField.text = String(root.selected.name || "")
        if (!textField.activeFocus) textField.text = String(root.selected.text || "")
        if (!colorField.activeFocus) colorField.text = String(root.selected.kind === "fill" ? root.selected.fillColor || "#ffffffff" : root.selected.textColor || "#ffffffff")
    }
    function flushInputs() {
        if (!root.canEdit) return true
        if (nameField.text.length > 256 || (textField.visible && textField.text.length > 1048576) || (colorField.visible && !colorField.acceptableInput)) {
            root.formError = qsTr("Check the layer name, text length and hex color before applying.")
            return false
        }
        root.formError = ""
        const patch = ({name: nameField.text})
        if (root.selected.kind === "text") patch.text = textField.text
        if ((root.selected.kind === "text" || root.selected.kind === "fill") && colorField.acceptableInput)
            patch[root.selected.kind === "fill" ? "fillColor" : "textColor"] = colorField.text
        const changed = Object.keys(patch).some(function(key) {
            let submitted = String(patch[key])
            let current = String(root.selected[key] !== undefined ? root.selected[key] : "")
            if (key === "fillColor" || key === "textColor") {
                submitted = submitted.toLowerCase(); current = current.toLowerCase()
                if (submitted.length === 7) submitted = "#ff" + submitted.slice(1)
                if (current.length === 7) current = "#ff" + current.slice(1)
            }
            return submitted !== current
        })
        if (changed && !root.controller.changePhotoComponentChild(root.selectedId, patch)) {
            root.formError = qsTr("These changes could not be applied. Check the text, color and layer, then try again.")
            return false
        }
        return true
    }
    function chooseLayer(id) {
        if (!root.flushInputs()) return
        nameField.focus = false; textField.focus = false; colorField.focus = false
        root.selectedId = String(id)
        root.syncInputs()
    }
    function addChild(kind) {
        if (root.editState.applying) return false
        if (!root.flushInputs() || !root.controller.addPhotoComponentChild(kind)) return false
        if (root.layers.length) root.chooseLayer(String(root.layers[0].id))
        return true
    }
    onOpened: { root.formError = ""; root.selectedId = root.layers.length ? String(root.layers[0].id) : ""; root.syncInputs() }
    onClosed: if (root.editState.active) root.controller.cancelPhotoComponentEditing()
    onSelectedChanged: if (!root.formError.length) root.syncInputs()
    Connections {
        target: root.controller
        function onControlsChanged() {
            if (root.controller.smartDocumentEditState.active) {
                if (!root.visible) root.open()
            } else if (root.opened) root.close()
        }
        function onAssetChanged() { root.controller.cancelPhotoComponentEditing(); root.close() }
    }
    FileDialog {
        id: imagePicker
        objectName: "photoComponentReplaceImageDialog"
        title: qsTr("Replace component image")
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (root.imageSession.length > 0 && root.editState.active && String(root.editState.sessionId) === root.imageSession &&
                    root.layers.some(function(child) { return String(child.id) === root.imageChild && child.kind === "source" }))
                root.controller.replacePhotoComponentImage(root.imageSession, root.imageChild, selectedFile.toString())
            root.imageSession = ""; root.imageChild = ""
        }
        onRejected: { root.imageSession = ""; root.imageChild = "" }
    }
    contentItem: ColumnLayout {
        spacing: theme.space12
        Label { Layout.fillWidth: true; text: qsTr("The parent stays unchanged until Apply. Shared placements update together."); wrapMode: Text.WordWrap; color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        GridLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            columns: root.width >= 700 ? 2 : 1
            columnSpacing: theme.space12; rowSpacing: theme.space8
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 90
                Layout.preferredWidth: 400; Layout.preferredHeight: root.width >= 700 ? 460 : 170
                color: theme.surfaceBottom; border.color: theme.controlEdge; radius: theme.radius4; clip: true
                Image { objectName: "photoComponentPreview"; anchors.fill: parent; anchors.margins: 8; source: String(root.editState.previewSource || ""); fillMode: Image.PreserveAspectFit; asynchronous: true }
                Label { anchors.centerIn: parent; visible: !root.editState.previewSource; text: qsTr("Preparing preview…"); color: theme.textMuted; font.family: theme.fontUi }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 170; Layout.preferredWidth: 360
                clip: true; contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: theme.space8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Contents"); color: theme.textMain; font.family: theme.fontUi; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        MiniButton { objectName: "photoComponentAddTextButton"; text: qsTr("+ Text"); enabled: !root.controller.professionalToolsBusy && !root.editState.applying; onClicked: root.addChild("text") }
                        MiniButton { objectName: "photoComponentAddFillButton"; text: qsTr("+ Fill"); enabled: !root.controller.professionalToolsBusy && !root.editState.applying; onClicked: root.addChild("fill") }
                    }
                    ListView {
                        id: childList
                        objectName: "photoComponentChildList"
                        Layout.fillWidth: true; Layout.preferredHeight: 135
                        clip: true; model: root.layers
                        delegate: Rectangle {
                            id: childRow
                            required property var modelData
                            width: childList.width; height: 32
                            color: String(modelData.id) === root.selectedId ? theme.alpha(theme.accentCool400, 0.16) : theme.surfaceBottom
                            Label { anchors.fill: parent; anchors.margins: 6; leftPadding: childRow.modelData.parentId ? 10 : 0; text: String(childRow.modelData.name || childRow.modelData.kind); elide: Text.ElideRight; color: theme.textMain; font.family: theme.fontUi }
                            MouseArea { anchors.fill: parent; onClicked: root.chooseLayer(childRow.modelData.id) }
                        }
                    }
                    ToneTextField { id: nameField; objectName: "photoComponentChildName"; Layout.fillWidth: true; enabled: root.canEdit; placeholderText: qsTr("Layer name"); onEditingFinished: root.flushInputs() }
                    ToneSwitch { objectName: "photoComponentChildEnabled"; Layout.fillWidth: true; text: qsTr("Visible"); enabled: root.canEdit; checked: root.selected.enabled !== false; onToggled: root.controller.changePhotoComponentChild(root.selectedId, {enabled: checked}) }
                    InspectorSlider {
                        objectName: "photoComponentChildOpacity"; Layout.fillWidth: true; label: qsTr("Opacity"); from: 0; to: 1; stepSize: 0.01
                        enabled: root.canEdit; value: Number(root.selected.opacity !== undefined ? root.selected.opacity : 1); valueText: Math.round(value * 100) + "%"
                        onValueDragged: function(value) { root.controller.changePhotoComponentChild(root.selectedId, {opacity: value}) }
                    }
                    ToneTextArea {
                        id: textField; objectName: "photoComponentChildText"
                        Layout.fillWidth: true; Layout.preferredHeight: 90; visible: root.selected.kind === "text"; enabled: root.canEdit
                        accessibleLabel: qsTr("Layer text")
                        onActiveFocusChanged: if (!activeFocus) root.flushInputs()
                    }
                    ToneTextField {
                        id: colorField; objectName: "photoComponentChildColor"; Layout.fillWidth: true
                        visible: root.selected.kind === "fill" || root.selected.kind === "text"; enabled: root.canEdit
                        placeholderText: qsTr("Hex color"); validator: RegularExpressionValidator { regularExpression: /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/ }
                        onEditingFinished: root.flushInputs()
                    }
                    MiniButton {
                        objectName: "photoComponentReplaceImageButton"; visible: root.selected.kind === "source"; enabled: root.canEdit; text: qsTr("Replace image…")
                        onClicked: { root.imageSession = String(root.editState.sessionId); root.imageChild = root.selectedId; imagePicker.open() }
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; text: root.formError || String(root.editState.error || ""); visible: text.length > 0; color: theme.warning; wrapMode: Text.WordWrap; font.family: theme.fontUi }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: (root.controller.professionalToolsBusy || root.editState.applying) ? qsTr("Preparing contents…") : ""; color: theme.textMuted; font.family: theme.fontUi }
            MiniButton { objectName: "photoComponentCancelButton"; text: qsTr("Cancel"); onClicked: { root.controller.cancelPhotoComponentEditing(); root.close() } }
            ActionButton { objectName: "photoComponentApplyButton"; text: qsTr("Apply"); tone: "primary"; enabled: !root.controller.professionalToolsBusy && !root.editState.applying && root.editState.previewReady !== false; onClicked: { if (root.flushInputs()) root.controller.applyPhotoComponentEditing() } }
        }
    }
}
