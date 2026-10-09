pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    required property var controller
    required property int layerIndex
    required property var definition
    property string layerId: ""
    property bool locked: false
    property string errorText: ""
    readonly property string mode: String(definition.fillMode || (definition.fillColor ? "solid" : "none"))
    readonly property bool gradient: mode === "linear" || mode === "radial"
    readonly property string textureType: String(definition.textureType || "paper")
    property var pendingOptions: ({})
    property string pendingLayerId: ""
    property bool coalescing: false
    property string colorKey: "fillColor"
    Theme { id: theme }
    spacing: theme.space4
    enabled: !locked

    function applyFill(options) {
        const ok = controller.setPhotoLayerShapeFill(layerIndex, options)
        errorText = ok ? "" : qsTr("Could not update this fill. Check the selected shape and values.")
        return ok
    }
    function queueFill(key, value) {
        const next = Object.assign({}, pendingOptions)
        next[key] = value
        pendingLayerId = layerId
        pendingOptions = next
        if (!fillTimer.running) fillTimer.start()
    }
    function flushFill() {
        fillTimer.stop()
        const next = pendingOptions
        pendingOptions = ({})
        if (Object.keys(next).length > 0 && pendingLayerId === layerId && !locked) applyFill(next)
    }
    function beginEdit() {
        if (!coalescing) { controller.beginCoalescedEdit(); coalescing = true }
    }
    function endEdit(commit) {
        if (commit) flushFill()
        else { fillTimer.stop(); pendingOptions = ({}) }
        if (coalescing) { controller.endCoalescedEdit(); coalescing = false }
    }
    function chooseColor(key, fallback) {
        colorKey = key
        colorDialog.selectedColor = String(definition[key] || fallback)
        colorDialog.open()
    }
    onLayerIdChanged: { endEdit(false); errorText = "" }
    onLockedChanged: if (locked) endEdit(false)
    onVisibleChanged: if (!visible) endEdit(true)
    Component.onDestruction: endEdit(false)
    Timer { id: fillTimer; interval: 50; onTriggered: root.flushFill() }

    component FillSlider: ToolValueSlider {
        required property string optionKey
        property real fallback: 0
        Layout.fillWidth: true
        Layout.preferredHeight: theme.controlHeightDense + theme.space16
        showCaption: true
        value: Number(root.definition[optionKey] === undefined ? fallback : root.definition[optionKey])
        valueText: optionKey.indexOf("Angle") >= 0 ? Math.round(value) + "°"
                   : Math.round(value * 100) + "%"
        onMoved: root.queueFill(optionKey, value)
        onPressedChanged: if (pressed) root.beginEdit(); else root.endEdit(true)
    }

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Fill"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ToneComboBox {
            objectName: "photoShapeFillMode"
            Layout.fillWidth: true
            Layout.minimumWidth: 100
            model: [qsTr("No fill"), qsTr("Solid"), qsTr("Linear gradient"), qsTr("Radial gradient"), qsTr("Texture")]
            currentIndex: root.mode === "none" ? 0 : root.mode === "solid" ? 1
                          : root.mode === "linear" ? 2 : root.mode === "radial" ? 3 : 4
            accessibleLabel: qsTr("Shape fill type")
            onActivated: function(index) { root.endEdit(true); root.applyFill({fillMode: ["none", "solid", "linear", "radial", "texture"][index]}) }
        }
        MiniButton {
            objectName: "photoShapeLayerNoFillButton"
            square: true
            iconName: "close"
            role: "icon"
            active: root.mode === "none"
            tooltip: qsTr("Remove the shape fill.")
            onClicked: root.applyFill({fillMode: "none"})
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.mode === "solid" || root.mode === "texture"
        Label { Layout.fillWidth: true; text: root.mode === "texture" ? qsTr("Base color") : qsTr("Color"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ColorSwatchButton {
            objectName: "photoShapeLayerFillSwatch"
            swatchColor: String(root.definition.fillColor || "#4a78ff")
            label: qsTr("Shape fill color")
            onPicked: root.chooseColor("fillColor", "#4a78ff")
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.gradient
        Label { text: qsTr("Start"); color: theme.textMuted; font.pixelSize: theme.typeCaption; font.family: theme.fontUi }
        ColorSwatchButton {
            objectName: "photoShapeGradientStart"
            swatchColor: String(root.definition.gradientStartColor || root.definition.fillColor || "#4a78ff")
            label: qsTr("Gradient start color")
            onPicked: root.chooseColor("gradientStartColor", String(root.definition.fillColor || "#4a78ff"))
        }
        Item { Layout.fillWidth: true }
        MiniButton {
            objectName: "photoShapeGradientSwap"
            square: true
            iconName: "swap"
            role: "icon"
            tooltip: qsTr("Swap gradient colors.")
            onClicked: root.applyFill({gradientStartColor: String(root.definition.gradientEndColor || "#ffffff"),
                gradientEndColor: String(root.definition.gradientStartColor || root.definition.fillColor || "#4a78ff")})
        }
        Label { text: qsTr("End"); color: theme.textMuted; font.pixelSize: theme.typeCaption; font.family: theme.fontUi }
        ColorSwatchButton {
            objectName: "photoShapeGradientEnd"
            swatchColor: String(root.definition.gradientEndColor || "#ffffff")
            label: qsTr("Gradient end color")
            onPicked: root.chooseColor("gradientEndColor", "#ffffff")
        }
    }
    FillSlider {
        objectName: "photoShapeGradientAngle"
        visible: root.mode === "linear"
        optionKey: "gradientAngle"
        label: qsTr("Angle")
        from: -180; to: 180; stepSize: 1
    }
    FillSlider {
        objectName: "photoShapeGradientRadius"
        visible: root.mode === "radial"
        optionKey: "gradientRadius"; fallback: 1
        label: qsTr("Radius")
        from: 0.05; to: 2; stepSize: 0.01
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.mode === "radial"
        FillSlider {
            objectName: "photoShapeGradientCenterX"
            optionKey: "gradientCenterX"; fallback: 0.5
            label: qsTr("Center X")
            from: 0; to: 1; stepSize: 0.01
        }
        FillSlider {
            objectName: "photoShapeGradientCenterY"
            optionKey: "gradientCenterY"; fallback: 0.5
            label: qsTr("Center Y")
            from: 0; to: 1; stepSize: 0.01
        }
    }
    ToneComboBox {
        objectName: "photoShapeTextureType"
        Layout.fillWidth: true
        visible: root.mode === "texture"
        model: [qsTr("Paper"), qsTr("Grain"), qsTr("Hatch"), qsTr("Image texture")]
        currentIndex: root.textureType === "grain" ? 1 : root.textureType === "hatch" ? 2
                      : root.textureType === "image" ? 3 : 0
        accessibleLabel: qsTr("Shape texture")
        onActivated: function(index) {
            if (index === 3 && !root.definition.textureData) textureDialog.open()
            else root.applyFill({textureType: ["paper", "grain", "hatch", "image"][index]})
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.mode === "texture"
        MiniButton {
            objectName: "photoShapeTextureImport"
            text: qsTr("Import image…")
            iconName: "import"
            role: "secondary"
            tooltip: qsTr("Use an image as a tiled texture. It is embedded in the edit at up to 1024 pixels per side.")
            onClicked: textureDialog.open()
        }
        Label {
            Layout.fillWidth: true
            text: String(root.definition.textureName || "")
            visible: String(root.definition.textureType || "") === "image"
            color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    FillSlider {
        objectName: "photoShapeTextureStrength"
        visible: root.mode === "texture"
        optionKey: "textureStrength"; fallback: 0.45
        label: qsTr("Texture strength")
        from: 0; to: 1; stepSize: 0.01
    }
    FillSlider {
        objectName: "photoShapeTextureScale"
        visible: root.mode === "texture"
        optionKey: "textureScale"; fallback: 1
        label: qsTr("Texture scale")
        from: 0.1; to: 8; stepSize: 0.05
    }
    FillSlider {
        objectName: "photoShapeTextureAngle"
        visible: root.mode === "texture"
        optionKey: "textureAngle"
        label: qsTr("Texture angle")
        from: -180; to: 180; stepSize: 1
    }
    Label {
        Layout.fillWidth: true
        visible: root.errorText.length > 0
        text: root.errorText
        color: theme.error; font.family: theme.fontUi; font.pixelSize: theme.typeCaption
        wrapMode: Text.WordWrap
    }
    ColorDialog {
        id: colorDialog
        title: qsTr("Shape color")
        options: ColorDialog.ShowAlphaChannel
        onAccepted: { const next = ({}); next[root.colorKey] = selectedColor.toString(); root.applyFill(next) }
    }
    FileDialog {
        id: textureDialog
        title: qsTr("Choose a shape texture")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.tif *.tiff *.bmp)")]
        onAccepted: {
            root.endEdit(true)
            root.errorText = root.controller.setPhotoLayerShapeTexture(root.layerIndex, String(selectedFile))
                ? "" : qsTr("Could not read this texture. Choose a supported image under 32 MB.")
        }
    }
}
