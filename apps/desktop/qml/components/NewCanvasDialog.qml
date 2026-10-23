pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "CanvasPresets.js" as CanvasPresets

AppPopup {
    id: root
    objectName: "newCanvasDialog"
    property bool busy: false
    property string errorText: ""
    property Item focusReturnItem: null
    signal createRequested(var settings)
    readonly property var presets: CanvasPresets.formats.concat([
        { label: "Landscape", width: 1920, height: 1080 },
        { label: "Custom", width: 0, height: 0 }
    ])
    readonly property real canvasWidth: Number(widthField.text)
    readonly property real canvasHeight: Number(heightField.text)
    readonly property bool dimensionsValid: widthField.acceptableInput && heightField.acceptableInput &&
                                            canvasWidth * canvasHeight <= 32 * 1024 * 1024
    modal: true
    focus: true
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(popupAvailableWidth, Math.max(1, Math.min(460, parent ? parent.width - 32 : 460)))
    height: Math.min(popupAvailableHeight, Math.max(1, Math.min(contentItem.implicitHeight + padding * 2, parent ? parent.height - 32 : 620)))
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: standardPopupPadding
    onAboutToShow: {
        root.errorText = ""
        const popupWindow = root.parent ? root.parent.Window.window : null
        root.focusReturnItem = popupWindow ? popupWindow.activeFocusItem : null
    }
    onOpened: { nameField.forceActiveFocus(); nameField.selectAll() }
    onClosed: Qt.callLater(function() {
        if (root.focusReturnItem && root.focusReturnItem.visible && root.focusReturnItem.enabled)
            root.focusReturnItem.forceActiveFocus(Qt.TabFocusReason)
        root.focusReturnItem = null
    })
    Theme { id: theme }
    background: PopupSurface {
        Accessible.name: "New Canvas"
        Accessible.role: Accessible.Dialog
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: "New Canvas"; color: theme.textStrong; font.pixelSize: theme.typeDialogTitle; font.weight: Font.DemiBold }
        Label { Layout.fillWidth: true; text: "Start a composition with shapes, text, photos and paint."; color: theme.textMuted; wrapMode: Text.WordWrap }
        ToneTextField {
            id: nameField
            objectName: "newCanvasNameField"
            Layout.fillWidth: true
            text: "Untitled Canvas"
            accessibleLabel: "Canvas name"
            enabled: !root.busy
            maximumLength: 120
        }
        ToneComboBox {
            id: presetField
            objectName: "newCanvasPresetField"
            Layout.fillWidth: true
            model: root.presets
            textRole: "label"
            accessibleLabel: "Canvas preset"
            enabled: !root.busy
            onActivated: {
                const preset = root.presets[currentIndex]
                if (preset.width > 0) { widthField.text = String(preset.width); heightField.text = String(preset.height) }
            }
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            Layout.fillWidth: true
            Label { text: "Width · px"; color: theme.textMuted }
            Label { text: "Height · px"; color: theme.textMuted }
            ToneTextField {
                id: widthField
                objectName: "newCanvasWidthField"
                Layout.fillWidth: true
                text: "1080"
                accessibleLabel: "Canvas width in pixels"
                validator: IntValidator { bottom: 1; top: 8192 }
                enabled: !root.busy
                onTextEdited: presetField.currentIndex = root.presets.length - 1
            }
            ToneTextField {
                id: heightField
                objectName: "newCanvasHeightField"
                Layout.fillWidth: true
                text: "1350"
                accessibleLabel: "Canvas height in pixels"
                validator: IntValidator { bottom: 1; top: 8192 }
                enabled: !root.busy
                onTextEdited: presetField.currentIndex = root.presets.length - 1
            }
        }
        Label { text: "Background"; color: theme.textMuted }
        RowLayout {
            ToneComboBox {
                id: backgroundField
                objectName: "newCanvasBackgroundField"
                Layout.fillWidth: true
                model: ["Transparent", "White", "Color"]
                accessibleLabel: "Canvas background"
                enabled: !root.busy
            }
            ToneTextField {
                id: colorField
                objectName: "newCanvasColorField"
                Layout.preferredWidth: 112
                text: "#20252b"
                accessibleLabel: "Canvas background color"
                visible: backgroundField.currentIndex === 2
                enabled: !root.busy
            }
        }
        Label { Layout.fillWidth: true; text: "Saved in your library. Up to 8192 px per side and 32 megapixels."; color: theme.textMuted; wrapMode: Text.WordWrap }
        Label {
            objectName: "newCanvasErrorLabel"
            Layout.fillWidth: true
            visible: root.errorText.length > 0 || !root.dimensionsValid
            text: root.errorText || "Enter valid dimensions, up to 32 megapixels."
            color: theme.error
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { text: "Cancel"; enabled: !root.busy; onClicked: root.close() }
            ActionButton {
                objectName: "newCanvasCreateButton"
                text: root.busy ? "Creating…" : "Create Canvas"
                enabled: !root.busy && root.dimensionsValid
                onClicked: root.createRequested({ name: nameField.text, width: root.canvasWidth, height: root.canvasHeight,
                                                 background: ["transparent", "white", "color"][backgroundField.currentIndex], color: colorField.text })
            }
        }
    }
}
