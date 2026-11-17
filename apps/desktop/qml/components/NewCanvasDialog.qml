pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "CanvasPresets.js" as CanvasPresets

AppPopup {
    id: root
    objectName: "newCanvasDialog"
    property bool busy: false
    property bool advancedExpanded: false
    property string errorText: ""
    property var projectNames: []
    property string projectName: ""
    property bool resizing: false
    function setCanvasSize(settings) {
        nameField.text = settings.name || "Untitled Canvas";
        widthField.text = String(settings.width);
        heightField.text = String(settings.height);
        presetField.currentIndex = root.presets.length - 1;
        resolutionField.currentIndex = 3;
        backgroundField.currentIndex = Math.max(0, ["transparent", "white", "color"].indexOf(settings.background || "transparent"));
        colorField.text = settings.color || "#20252b";
    }
    property Item focusReturnItem: null
    signal createRequested(var settings)
    function applyResolution() {
        const edge = resolutionField.currentIndex === 1 ? 1920 : resolutionField.currentIndex === 2 ? 3840 : 0;
        if (!edge) return;
        const scale = edge / Math.max(root.canvasWidth, root.canvasHeight);
        const nextWidth = Math.max(1, Math.round(root.canvasWidth * scale));
        const nextHeight = Math.max(1, Math.round(root.canvasHeight * scale));
        widthField.text = String(nextWidth);
        heightField.text = String(nextHeight);
    }
    function presetLabel(preset: var): string {
        if (preset.label === "9:16") return "Portrait · 9:16"
        if (preset.width === preset.height) return "Square · " + preset.label
        if (preset.height > preset.width) return "Portrait · " + preset.label
        return "Landscape · " + preset.label
    }
    readonly property var presets: CanvasPresets.formats.map(preset => Object.assign({}, preset, {
        label: root.presetLabel(preset)
    })).concat([
        {
            label: "Landscape · 16:9",
            width: 1920,
            height: 1080
        },
        {
            label: "Landscape · 3:2",
            width: 1800,
            height: 1200
        },
        {
            label: "Portrait · 2:3",
            width: 1200,
            height: 1800
        },
        {
            label: "Custom",
            width: 0,
            height: 0
        }
    ])
    readonly property real canvasWidth: Number(widthField.text)
    readonly property real canvasHeight: Number(heightField.text)
    readonly property bool dimensionsValid: widthField.acceptableInput && heightField.acceptableInput && canvasWidth * canvasHeight <= 32 * 1024 * 1024
    readonly property bool backgroundColorValid: root.resizing || backgroundField.currentIndex !== 2 || /^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.test(colorField.text.trim())
    readonly property bool creationValid: dimensionsValid && backgroundColorValid
    modal: true
    focus: true
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(460), parent ? parent.width - theme.scaled(32) : theme.scaled(460))))
    height: Math.min(popupAvailableHeight, Math.max(1, Math.min(contentItem.implicitHeight + padding * 2, parent ? parent.height - theme.scaled(32) : theme.scaled(620))))
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: standardPopupPadding
    onAboutToShow: {
        root.errorText = "";
        projectField.currentIndex = Math.max(0, projectField.model.indexOf(root.projectName));
        const popupWindow = root.parent ? root.parent.Window.window : null;
        root.focusReturnItem = popupWindow ? popupWindow.activeFocusItem : null;
    }
    onOpened: {
        nameField.forceActiveFocus();
        nameField.selectAll();
    }
    onClosed: Qt.callLater(function () {
        if (root.focusReturnItem && root.focusReturnItem.visible && root.focusReturnItem.enabled)
            root.focusReturnItem.forceActiveFocus(Qt.TabFocusReason);
        root.focusReturnItem = null;
    })
    Theme {
        id: theme
    }
    background: PopupSurface {
        Accessible.name: "New Canvas"
        Accessible.role: Accessible.Dialog
    }
    contentItem: ColumnLayout {
        spacing: theme.gapControl
        ScrollView {
            id: canvasOptionsScroll
            objectName: "newCanvasOptionsScroll"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(canvasForm.implicitHeight, Math.max(1, root.popupAvailableHeight - root.topPadding - root.bottomPadding - canvasFooter.implicitHeight - theme.gapControl))
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: canvasForm
                width: canvasOptionsScroll.availableWidth
                spacing: theme.gapControl
                Label {
                    text: root.resizing ? "Canvas size" : "New canvas"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeDialogTitle
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: root.resizing ? "Resize the canvas. Image layers keep their original source pixels." : "Start a new document in any size."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: "Name"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                ToneTextField {
                    id: nameField
                    objectName: "newCanvasNameField"
                    Layout.fillWidth: true
                    text: "Untitled Canvas"
                    accessibleLabel: "Canvas name"
                    placeholderText: "Canvas name"
                    enabled: !root.busy && !root.resizing
                    maximumLength: 120
                    onTextEdited: root.errorText = ""
                }
                Label {
                    text: "Project"
                    visible: !root.resizing
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                ToneComboBox {
                    id: projectField
                    objectName: "newCanvasProjectField"
                    Layout.fillWidth: true
                    visible: !root.resizing
                    model: ["No project"].concat(root.projectNames)
                    accessibleLabel: "Canvas project"
                    enabled: !root.busy && !root.resizing
                }
                Label {
                    Layout.fillWidth: true
                    text: "Format"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
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
                        root.errorText = ""
                        const preset = root.presets[currentIndex];
                        if (preset.width > 0) {
                            widthField.text = String(preset.width);
                            heightField.text = String(preset.height);
                            root.applyResolution();
                        } else {
                            widthField.forceActiveFocus();
                            widthField.selectAll();
                        }
                    }
                }
                Label {
                    text: "Resolution"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                ToneComboBox {
                    id: resolutionField
                    objectName: "newCanvasResolutionField"
                    Layout.fillWidth: true
                    model: ["Preset size", "HD · 1920 px long edge", "4K · 3840 px long edge", "Custom pixels"]
                    accessibleLabel: "Canvas resolution"
                    enabled: !root.busy
                    onActivated: root.applyResolution()
                }
                GridLayout {
                    objectName: "newCanvasDimensionsGroup"
                    columns: 2
                    columnSpacing: theme.space12
                    Layout.fillWidth: true
                    Label {
                        Layout.columnSpan: 2
                        text: "Dimensions"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    Label {
                        text: "Width · px"
                        color: theme.textMuted
                        font.family: theme.fontUi
                    }
                    Label {
                        text: "Height · px"
                        color: theme.textMuted
                        font.family: theme.fontUi
                    }
                    ToneTextField {
                        id: widthField
                        numericEntry: true
                        objectName: "newCanvasWidthField"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: "1080"
                        accessibleLabel: "Canvas width in pixels"
                        validator: IntValidator {
                            bottom: 1
                            top: 8192
                        }
                        enabled: !root.busy
                        onTextEdited: {
                            root.errorText = ""
                            presetField.currentIndex = root.presets.length - 1
                            resolutionField.currentIndex = 3
                        }
                    }
                    ToneTextField {
                        id: heightField
                        numericEntry: true
                        objectName: "newCanvasHeightField"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: "1350"
                        accessibleLabel: "Canvas height in pixels"
                        validator: IntValidator {
                            bottom: 1
                            top: 8192
                        }
                        enabled: !root.busy
                        onTextEdited: {
                            root.errorText = ""
                            presetField.currentIndex = root.presets.length - 1
                            resolutionField.currentIndex = 3
                        }
                    }
                }
                ActionButton {
                    objectName: "newCanvasSwapDimensionsButton"
                    text: "Swap width & height"
                    compact: true
                    enabled: !root.busy && root.dimensionsValid
                    onClicked: {
                        const previousWidth = widthField.text;
                        widthField.text = heightField.text;
                        heightField.text = previousWidth;
                        const presetIndex = root.presets.findIndex(preset =>
                            preset.width === root.canvasWidth && preset.height === root.canvasHeight);
                        presetField.currentIndex = presetIndex >= 0 ? presetIndex : root.presets.length - 1;
                        root.errorText = "";
                    }
                }
                ActionButton {
                    objectName: "newCanvasAdvancedButton"
                    visible: !root.resizing
                    text: root.advancedExpanded ? "Hide background options" : "Background options…"
                    compact: true
                    active: root.advancedExpanded
                    enabled: !root.busy
                    onClicked: root.advancedExpanded = !root.advancedExpanded
                }
                ColumnLayout {
                    objectName: "newCanvasBackgroundGroup"
                    Layout.fillWidth: true
                    visible: !root.resizing && root.advancedExpanded
                    Label {
                        text: "Background"
                        color: theme.textMuted
                        font.family: theme.fontUi
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ToneComboBox {
                            id: backgroundField
                            objectName: "newCanvasBackgroundField"
                            Layout.fillWidth: true
                            model: ["Transparent", "White", "Color"]
                            accessibleLabel: "Canvas background"
                            enabled: !root.busy
                            onActivated: root.errorText = ""
                        }
                        ToneTextField {
                            id: colorField
                            objectName: "newCanvasColorField"
                            Layout.preferredWidth: theme.scaled(112)
                            text: "#20252b"
                            accessibleLabel: "Canvas background color"
                            Accessible.description: "Enter a 3 or 6 digit hex color, for example #20252b."
                            visible: backgroundField.currentIndex === 2
                            enabled: !root.busy
                            placeholderText: "#RRGGBB"
                            onTextEdited: root.errorText = ""
                        }
                    }
                }
                Label {
                    objectName: "newCanvasFormatSummary"
                    Layout.fillWidth: true
                    text: widthField.text + " × " + heightField.text + " px · " + backgroundField.currentText
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.advancedExpanded
                    text: "Up to 8192 px per side and 32 megapixels."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }
                Label {
                    objectName: "newCanvasErrorLabel"
                    Layout.fillWidth: true
                    visible: root.errorText.length > 0 || !root.dimensionsValid || !root.backgroundColorValid
                    text: root.errorText || (!root.dimensionsValid
                          ? "Enter valid dimensions, up to 32 megapixels."
                          : "Enter a valid hex color for the canvas background.")
                    color: theme.error
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.Wrap
                }
            }
        }
        RowLayout {
            id: canvasFooter
            objectName: "newCanvasFooter"
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            ActionButton {
                text: "Cancel"
                enabled: !root.busy
                onClicked: root.close()
            }
            ActionButton {
                objectName: "newCanvasCreateButton"
                text: root.busy ? "Working…" : root.resizing ? "Apply size" : "Create canvas"
                tone: "primary"
                enabled: !root.busy && root.creationValid
                disabledReason: !root.dimensionsValid
                                ? "Enter valid dimensions, up to 32 megapixels."
                                : !root.backgroundColorValid
                                  ? "Enter a valid hex canvas background color."
                                  : ""
                onClicked: root.createRequested({
                    name: nameField.text,
                    projectName: projectField.currentIndex > 0 ? projectField.currentText : "",
                    width: root.canvasWidth,
                    height: root.canvasHeight,
                    background: ["transparent", "white", "color"][backgroundField.currentIndex],
                    color: colorField.text
                })
            }
        }
    }
}
