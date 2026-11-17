import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../apps/desktop/qml/components" as Components

Window {
    id: window

    visible: true
    width: 340
    height: 820
    color: "#171717"
    property int selectionStart: 2
    property int selectionEnd: 7
    property string selectionContent: "A quiet portrait"
    property var targetIndices: []
    property var selectedLayerIds: []
    property bool pathHandlesActive: false

    QtObject {
        id: controller
        objectName: "typographyController"
        property var style: ({
            family: "Georgia", face: "", size: 48, weight: 400, italic: false,
            underline: false, color: "#ffffffff", alignment: "left", tracking: 0,
            lineHeight: 1.2, paragraphSpacing: 0, baselineOffset: 0,
            strokeWidth: 0, strokeColor: "#ff000000", shadowX: 0, shadowY: 2,
            shadowColor: "#a0000000", material: "none", axes: ({}), pathPoints: [],
            runs: [{start: 2, length: 5, strokeWidth: 2, strokeColor: "#ff336699"}]
        })
        property var lastChanges: ({})
        property int singleStyleCalls: 0
        property int multiStyleCalls: 0
        property int rangeStyleCalls: 0
        property int rangeStart: -1
        property int rangeEnd: -1
        property int pathCalls: 0
        property string rangeSnapshot: ""
        property int coalescedBegins: 0
        property int coalescedEnds: 0
        property int coalescedCancels: 0
        property var transactionStyle: ({})
        property var layerIds: ["text-layer-1", "text-layer-2"]

        function photoLayerTypography(index) { return style }
        function photoLayerSnapshot() {
            return [{id: String(layerIds[0] || "")}, {id: String(layerIds[1] || "")}]
        }
        function photoLayerTextLayoutInfo(index) { return ({ resolvedFamily: "Georgia" }) }
        function photoTextFontWarning(family) { return "" }
        function availablePhotoTextFonts() { return ["Arial", "Georgia", "Impact"] }
        function availablePhotoTextFaces(family) { return ["Regular", "Bold"] }
        function availablePhotoTextAxes(family, face) { return [] }
        function setPhotoLayerTypography(index, changes) {
            singleStyleCalls++
            lastChanges = changes
            style = Object.assign({}, style, changes)
            return true
        }
        function setPhotoLayersTypography(indices, changes) {
            multiStyleCalls++
            lastChanges = changes
            style = Object.assign({}, style, changes)
            return true
        }
        function setPhotoLayerTextStyle(index, family, size, color, weight, alignment) {
            return setPhotoLayerTypography(index, {family: family, size: size, color: color,
                weight: weight === "bold" ? 700 : 400, alignment: alignment})
        }
        function setPhotoLayerTextRangeStyle(index, start, end, changes, textSnapshot) {
            if (changes.color !== undefined && String(style.color || "") === String(changes.color))
                return false
            rangeStyleCalls++
            rangeStart = start
            rangeEnd = end
            rangeSnapshot = textSnapshot
            lastChanges = changes
            style = Object.assign({}, style, changes)
            return true
        }
        function setPhotoLayerTextPathFromActiveSelection(index, offset) {
            pathCalls++
            style = Object.assign({}, style, {
                pathPoints: [{x: 0.12, y: 0.66}, {x: 0.48, y: 0.48}, {x: 0.88, y: 0.61}],
                pathClosed: false, pathStart: 0.04, pathEnd: 0.96,
                pathOffset: 4, pathSpacing: 1.5, pathSide: "left"
            })
            return true
        }
        function clearPhotoLayerTextPath(index) {
            style = Object.assign({}, style, {pathPoints: [], pathClosed: false})
            return true
        }
        function beginCoalescedEdit() { coalescedBegins++; transactionStyle = style; return true }
        function endCoalescedEdit() { coalescedEnds++; return true }
        function cancelCoalescedEdit() {
            coalescedCancels++
            style = transactionStyle
            return true
        }
        function setPhotoLayerPerspective(index, horizontal, vertical) { return true }
    }

    QtObject {
        id: canonicalTextEditor
        objectName: "canonicalTextEditor"
        property bool editing: false
        function flushPendingText() { return true }
    }
    property var canonicalEditor: canonicalTextEditor

    Components.ModularToolPanel {
        id: toolPanel
        objectName: "typographyToolPanelFixture"
        anchors.fill: parent
        active: true
        title: qsTr("Typography")
        targetText: qsTr("Text layer · A quiet portrait")
        contextKey: "typography-ui-proof"

        ColumnLayout {
            parent: toolPanel.contentItem
            Layout.fillWidth: true
            spacing: theme.space12
            Components.PhotoTypographyPanel {
                id: typography
                objectName: "photoTypographyPanelFixture"
                Layout.fillWidth: true
                showHeader: false
                controller: controller
                layerIndex: 0
                layerData: ({id: "text-layer-1", textTypography: ({})})
                textSelectionStart: window.selectionStart
                textSelectionEnd: window.selectionEnd
                textSelectionContent: window.selectionContent
                selectedLayerIndices: window.targetIndices
                selectedLayerIds: window.selectedLayerIds
                onBaselineEditingRequested: function(active) {
                    window.pathHandlesActive = active
                }
            }
        }
    }

    Components.Theme { id: theme }
}
