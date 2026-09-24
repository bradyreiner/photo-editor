pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    property bool expanded: true
    readonly property bool upgraded: !!root.layerData.textTypography
    readonly property var style: {
        const revision = root.layerData
        return revision ? root.controller.photoLayerTypography(root.layerIndex) : ({})
    }
    readonly property var layoutInfo: {
        const revision = root.layerData
        return root.expanded && root.upgraded && revision
            ? root.controller.photoLayerTextLayoutInfo(root.layerIndex) : ({})
    }
    spacing: 6
    onLayerIndexChanged: expanded = true

    Button {
        Layout.fillWidth: true
        text: qsTr("Typography")
        checkable: true
        checked: root.expanded
        onClicked: root.expanded = checked
        Accessible.description: qsTr("Show font decoration and spacing controls")
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.expanded
        spacing: 6
        Label {
            Layout.fillWidth: true
            visible: !root.upgraded
            wrapMode: Text.WordWrap
            text: qsTr("Enable full typography to edit font faces, character styling, and paragraph spacing. Existing text may reflow; you can undo the change.")
        }
        Button {
            Layout.fillWidth: true
            visible: !root.upgraded
            enabled: !root.locked
            text: qsTr("Enable full typography")
            onClicked: root.controller.setPhotoLayerTypography(root.layerIndex, { size: Number(root.style.size) })
        }
        Label {
            Layout.fillWidth: true
            visible: root.upgraded && (!!root.layoutInfo.error || !!root.layoutInfo.overflow)
            wrapMode: Text.WordWrap
            text: root.layoutInfo.error ? String(root.layoutInfo.error)
                : qsTr("Text extends outside its box. Increase the box size or reduce the text size or spacing.")
            Accessible.name: text
        }
        Label {
            objectName: "photoMissingFontWarning"
            Layout.fillWidth: true
            visible: root.upgraded && (!!root.layoutInfo.missingFont || !!root.layoutInfo.missingFace)
            wrapMode: Text.WordWrap
            text: root.layoutInfo.missingFont
                ? qsTr("%1 is unavailable. Displaying %2; the saved font choice is preserved.").arg(root.style.family).arg(root.layoutInfo.resolvedFamily)
                : qsTr("The saved font face is unavailable. Choose an installed face to replace it.")
        }
        ComboBox {
            objectName: "photoTextModeControl"
            Layout.fillWidth: true
            visible: root.upgraded
            enabled: !root.locked
            model: [qsTr("Paragraph box"), qsTr("Point text · auto size")]
            currentIndex: root.style.mode === "point" ? 1 : 0
            Accessible.name: qsTr("Text layout mode")
            onActivated: function(index) {
                root.controller.setPhotoLayerTypography(root.layerIndex, {mode: index === 1 ? "point" : "paragraph"})
            }
        }
        ComboBox {
            Layout.fillWidth: true
            visible: root.upgraded
            enabled: !root.locked
            readonly property var faces: root.controller.availablePhotoTextFaces(String(root.style.family || ""))
            model: [qsTr("Automatic face")].concat(faces)
            currentIndex: Math.max(0, faces.indexOf(String(root.style.face || "")) + 1)
            Accessible.name: qsTr("Font face")
            onActivated: function(index) {
                root.controller.setPhotoLayerTypography(root.layerIndex, { face: index === 0 ? "" : faces[index - 1] })
            }
        }
        RowLayout {
            visible: root.upgraded
            Layout.fillWidth: true
            Repeater {
                model: [{ key: "italic", label: qsTr("Italic") }, { key: "underline", label: qsTr("Underline") }]
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    checkable: true
                    checked: !!root.style[modelData.key]
                    enabled: !root.locked
                    onClicked: {
                        const change = ({})
                        change[modelData.key] = !root.style[modelData.key]
                        root.controller.setPhotoLayerTypography(root.layerIndex, change)
                    }
                }
            }
        }
        Repeater {
            model: [
                { key: "size", label: qsTr("Size (px)"), minimum: 0.25, maximum: 8192 },
                { key: "tracking", label: qsTr("Letter spacing (px)"), minimum: -8192, maximum: 8192 },
                { key: "lineHeight", label: qsTr("Line height (× size)"), minimum: 0.1, maximum: 20 },
                { key: "paragraphSpacing", label: qsTr("Paragraph spacing (px)"), minimum: 0, maximum: 8192 },
                { key: "baselineOffset", label: qsTr("Baseline shift (px)"), minimum: -8192, maximum: 8192 }
            ]
            delegate: RowLayout {
                id: metricRow
                required property var modelData
                visible: root.upgraded
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: metricRow.modelData.label
                    wrapMode: Text.WordWrap
                }
                TextField {
                    id: field
                    objectName: "photoTypography_" + metricRow.modelData.key
                    Layout.preferredWidth: 88
                    enabled: !root.locked
                    text: String(root.style[metricRow.modelData.key] || 0)
                    Accessible.name: metricRow.modelData.label
                    selectByMouse: true
                    validator: DoubleValidator {
                        bottom: metricRow.modelData.minimum
                        top: metricRow.modelData.maximum
                        decimals: 4
                        notation: DoubleValidator.StandardNotation
                        locale: "C"
                    }
                    function restore() {
                        text = Qt.binding(function() { return String(root.style[metricRow.modelData.key] || 0) })
                    }
                    onEditingFinished: {
                        if (acceptableInput) {
                            const change = ({})
                            change[metricRow.modelData.key] = Number(text)
                            root.controller.setPhotoLayerTypography(root.layerIndex, change)
                        }
                        restore()
                    }
                    Keys.onEscapePressed: function(event) {
                        restore()
                        focus = false
                        event.accepted = true
                    }
                }
            }
        }
    }
}
