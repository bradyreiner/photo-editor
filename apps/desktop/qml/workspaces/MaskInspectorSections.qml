import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Column {
    id: root

    required property var editRoot
    Theme { id: theme }

    component MaskSection: InspectorSection {
        showAccentMarker: false
        uppercaseTitle: false
        titleFontFamily: theme.fontDisplay
        titlePixelSize: 16
        titleWeight: Font.Bold
        titleLetterSpacing: 0
    }

    readonly property var activeMaskData: editorPreview.hasActiveMask &&
                                          editorPreview.activeMaskIndex >= 0 &&
                                          editorPreview.activeMaskIndex < editorPreview.maskStack.length
                                          ? editorPreview.maskStack[editorPreview.activeMaskIndex]
                                          : null
    readonly property color activeMaskAccent: activeMaskData ? editRoot.maskAccent(activeMaskData) : theme.accentCool400

    function retouchAccent(mode) {
        if (mode === "clone")
            return "#7cb5ff"
        if (mode === "heal")
            return "#72cbb2"
        return "#d98962"
    }

    spacing: 10

    MaskSection {
        width: parent.width
        title: "Mask"

        Rectangle {
            Layout.fillWidth: true
            radius: 10
            color: "#161a1e"
            border.width: 0
            border.color: "transparent"
            implicitHeight: statusColumn.implicitHeight + 20

            Column {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                RowLayout {
                    width: parent.width
                    spacing: 6

                    Rectangle {
                        radius: 5
                        color: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole === "retouch"
                               ? Qt.alpha(root.retouchAccent(editorPreview.activeMaskRetouchMode), 0.16)
                               : Qt.alpha(root.activeMaskAccent, 0.16)
                        border.width: 1
                        border.color: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole === "retouch"
                                      ? Qt.alpha(root.retouchAccent(editorPreview.activeMaskRetouchMode), 0.34)
                                      : Qt.alpha(root.activeMaskAccent, 0.34)
                        implicitWidth: modeChip.implicitWidth + 12
                        implicitHeight: 18

                        Label {
                            id: modeChip
                            anchors.centerIn: parent
                            text: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole === "retouch" ? "Retouch" : "Select"
                            color: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole === "retouch"
                                   ? root.retouchAccent(editorPreview.activeMaskRetouchMode)
                                   : root.activeMaskAccent
                            font.family: theme.fontDisplay
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.2
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                Label {
                    width: parent.width
                    text: editorPreview.hasActiveMask
                          ? (editorPreview.activeMaskToolRole === "retouch"
                             ? editRoot.activeToolLabel()
                             : "Editing " + editorPreview.activeMaskName)
                          : "No active mask"
                    color: "#f2f4f6"
                    font.family: theme.fontDisplay
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    width: parent.width
                    text: editRoot.cloneSourcePickArmed
                              ? "Click a clone source."
                              : editRoot.objectPickArmed && editRoot.objectPickMode === "remove"
                                ? "Click or drag a target."
                                : editRoot.objectPickArmed && editRoot.objectPickMode === "selection-subtract"
                                  ? "Click an object to subtract."
                                : editRoot.objectPickArmed
                                  ? "Click an object to select."
                                  : editRoot.polygonDrawArmed
                                    ? "Click points. Double-click to finish."
                                    : editRoot.lassoDrawArmed
                                      ? "Drag on the canvas."
                                      : "Choose a tool to begin."
                    wrapMode: Text.WordWrap
                    color: "#8f98a2"
                    font.family: theme.fontUi
                    font.pixelSize: 10
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            width: parent.width
            spacing: 6

            ActionButton {
                text: "Color"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "overlay"
                enabled: editorPreview.hasAsset
                tooltipText: "Show a colored mask overlay on the image."
                onClicked: editorPreview.maskPreviewMode = "overlay"
            }

            ActionButton {
                text: "Matte"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "grayscale"
                enabled: editorPreview.hasAsset
                tooltipText: "Show a neutral matte without the tinted overlay."
                onClicked: editorPreview.maskPreviewMode = "grayscale"
            }

            ActionButton {
                text: "On Light"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "black-on-white"
                enabled: editorPreview.hasAsset
                tooltipText: "Show the mask against a light matte."
                onClicked: editorPreview.maskPreviewMode = "black-on-white"
            }

            ActionButton {
                text: "On Dark"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "white-on-black"
                enabled: editorPreview.hasAsset
                tooltipText: "Show the mask against a dark matte."
                onClicked: editorPreview.maskPreviewMode = "white-on-black"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.hasActiveMask ? "Deselect" : "Exit Mask"
                compact: true
                enabled: editorPreview.hasActiveMask ||
                         editRoot.objectPickArmed ||
                         editRoot.polygonDrawArmed ||
                         editRoot.lassoDrawArmed ||
                         editRoot.cloneSourcePickArmed
                onClicked: editRoot.deselectMaskEditing()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Delete Active"
                compact: true
                accent: "#d36a5e"
                enabled: editorPreview.hasActiveMask
                onClicked: editorPreview.removeActiveMask()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: editorPreview.hasActiveMask &&
                     editorPreview.activeMaskToolRole === "retouch" &&
                     !editorPreview.activeMaskCommitted
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskRetouchMode === "remove"
                      ? "Apply Remove"
                      : (editorPreview.activeMaskRetouchMode === "heal" ? "Apply Heal" : "Apply Clone")
                compact: true
                accent: root.retouchAccent(editorPreview.activeMaskRetouchMode)
                tone: "primary"
                onClicked: editorPreview.applyActiveRetouch()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Discard"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.cancelActiveRetouch()
                }
            }
        }

        Label {
            visible: !editorPreview.subjectMaskAvailable
            Layout.fillWidth: true
            text: "AI Subject, Object Select, and Remove Object require macOS 14 or later."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        Label {
            Layout.fillWidth: true
            text: editorPreview.aiModelStatus
            wrapMode: Text.WordWrap
            color: editorPreview.aiSmartSelectionAvailable || editorPreview.aiInpaintAvailable
                   ? "#72cbb2"
                   : "#d98962"
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        width: parent.width
        title: "Mask Sets"
        subtitle: "Selection sources build one editable mask set."
        expandable: true
        expanded: true

        Column {
            width: parent.width
            spacing: 8

            Repeater {
                model: editorPreview.maskSets

                delegate: Rectangle {
                    required property var modelData

                    readonly property bool isActiveSet: {
                        for (let sourceIndex = 0; sourceIndex < modelData.sources.length; ++sourceIndex) {
                            if (modelData.sources[sourceIndex].flatIndex === editorPreview.activeMaskIndex)
                                return true
                        }
                        return false
                    }
                    readonly property var firstSource: modelData.sources.length > 0 ? modelData.sources[0] : null
                    readonly property color setColor: firstSource ? editRoot.maskAccent(firstSource) : "#8ab8d8"

                    width: parent.width
                    radius: 10
                    color: isActiveSet ? Qt.alpha(setColor, 0.12) : "#171b1f"
                    border.width: 1
                    border.color: isActiveSet ? Qt.alpha(setColor, 0.45) : "#2d3339"
                    implicitHeight: setColumn.implicitHeight + 20
                    opacity: modelData.enabled ? 1.0 : 0.55

                    Column {
                        id: setColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        RowLayout {
                            id: setHeader
                            width: parent.width
                            spacing: 8

                            TapHandler {
                                onTapped: {
                                    if (firstSource)
                                        editorPreview.activeMaskIndex = firstSource.flatIndex
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 10
                                Layout.preferredHeight: 10
                                radius: 5
                                color: setColor
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: "#f2f4f6"
                                    font.family: theme.fontUi
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.sourceCount === 1
                                          ? "1 source"
                                          : modelData.sourceCount.toString() + " sources"
                                    color: "#8f98a2"
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }

                            ActionButton {
                                iconName: modelData.enabled ? "visible" : "hidden"
                                showText: false
                                compact: true
                                active: modelData.enabled
                                accent: setColor
                                tooltipText: modelData.enabled ? "Disable mask set" : "Enable mask set"
                                enabled: firstSource !== null
                                onClicked: {
                                    const newEnabled = !modelData.enabled
                                    for (let si = 0; si < modelData.sources.length; ++si) {
                                        editorPreview.activeMaskIndex = modelData.sources[si].flatIndex
                                        editorPreview.activeMaskEnabled = newEnabled
                                    }
                                    if (firstSource)
                                        editorPreview.activeMaskIndex = firstSource.flatIndex
                                }
                            }
                        }

                        Repeater {
                            model: modelData.sources

                            delegate: Rectangle {
                                required property var modelData

                                width: parent.width
                                radius: 8
                                color: modelData.flatIndex === editorPreview.activeMaskIndex
                                       ? "#21272d"
                                       : "#14181c"
                                border.width: 1
                                border.color: modelData.flatIndex === editorPreview.activeMaskIndex
                                              ? editRoot.maskAccent(modelData)
                                              : "#242b31"
                                implicitHeight: 44
                                opacity: modelData.enabled ? 1.0 : 0.6

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: editorPreview.activeMaskIndex = modelData.flatIndex
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8

                                    Rectangle {
                                        Layout.preferredWidth: 8
                                        Layout.preferredHeight: 8
                                        radius: 4
                                        color: editRoot.maskAccent(modelData)
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            color: "#f2f4f6"
                                            font.family: theme.fontUi
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.kind + " · " + modelData.operation
                                            color: "#8f98a2"
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: editorPreview.maskSetCount === 0
                width: parent.width
                implicitHeight: emptyMaskSets.implicitHeight + 24
                radius: 8
                color: "#15191e"
                border.width: 1
                border.color: "#232830"

                Column {
                    id: emptyMaskSets
                    anchors.centerIn: parent
                    width: parent.width - 24
                    spacing: 6

                    Label {
                        width: parent.width
                        text: "No mask sets yet"
                        horizontalAlignment: Text.AlignHCenter
                        color: "#8f98a2"
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }

                    Label {
                        width: parent.width
                        text: "Start with AI Subject, Pick Object, a shape mask, or a manual selection."
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: "#6b7580"
                        font.family: theme.fontUi
                        font.pixelSize: 11
                    }
                }
            }
        }

        RowLayout {
            width: parent.width
            visible: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Deselect"
                compact: true
                onClicked: editRoot.deselectMaskEditing()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Move Up"
                compact: true
                onClicked: editorPreview.moveActiveMask(-1)
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Move Down"
                compact: true
                onClicked: editorPreview.moveActiveMask(1)
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Duplicate"
                compact: true
                onClicked: editorPreview.duplicateActiveMask()
            }

            ConfirmActionButton {
                Layout.fillWidth: true
                text: "Delete"
                compact: true
                helperText: "Delete the active mask"
                onConfirmed: editorPreview.removeActiveMask()
            }
        }
    }

    MaskSection {
        width: parent.width
        title: "Retouch"
        subtitle: "Heal, clone, and remove strokes stay separate from selection masks."
        expandable: true
        expanded: editorPreview.activeMaskToolRole === "retouch"

        Column {
            width: parent.width
            spacing: 8

            Repeater {
                model: editorPreview.retouchStack

                delegate: Rectangle {
                    required property var modelData

                    width: parent.width
                    radius: 10
                    color: modelData.flatIndex === editorPreview.activeMaskIndex ? "#21272d" : "#171b1f"
                    border.width: 1
                    border.color: modelData.flatIndex === editorPreview.activeMaskIndex
                                  ? editRoot.maskAccent(modelData)
                                  : "#2d3339"
                    implicitHeight: 50
                    opacity: modelData.enabled ? 1.0 : 0.55

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: editorPreview.activeMaskIndex = modelData.flatIndex
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            color: editRoot.maskAccent(modelData)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: "#f2f4f6"
                                font.family: theme.fontUi
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.retouchMode.length > 0
                                      ? modelData.retouchMode + " · " +
                                        (modelData.committed ? "applied" : "staged")
                                      : "retouch"
                                color: "#8f98a2"
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }

                        ActionButton {
                            text: "Delete"
                            compact: true
                            accent: "#d36a5e"
                            onClicked: {
                                editorPreview.activeMaskIndex = modelData.flatIndex
                                editorPreview.removeActiveMask()
                            }
                        }
                    }
                }
            }

            Label {
                visible: editorPreview.retouchCount === 0
                width: parent.width
                text: "No retouch strokes yet."
                wrapMode: Text.WordWrap
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 12
            }
        }
    }

    MaskSection {
        width: parent.width
        title: editorPreview.activeMaskToolRole === "retouch" ? "Retouch Properties" : "Mask Properties"
        visible: editorPreview.hasActiveMask

        ToneTextField {
            width: parent.width
            text: editorPreview.activeMaskName
            placeholderText: editorPreview.activeMaskToolRole === "retouch" ? "Retouch name" : "Mask set name"
            onEditingFinished: editorPreview.activeMaskName = text
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole !== "retouch" &&
                     editorPreview.activeMaskKind !== "brush"

            ActionButton {
                Layout.fillWidth: true
                text: "Add"
                active: editorPreview.activeMaskOperation === "add"
                onClicked: editorPreview.activeMaskOperation = "add"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Subtract"
                active: editorPreview.activeMaskOperation === "subtract"
                onClicked: editorPreview.activeMaskOperation = "subtract"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Intersect"
                active: editorPreview.activeMaskOperation === "intersect"
                onClicked: editorPreview.activeMaskOperation = "intersect"
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskEnabled ? "Enabled" : "Disabled"
                active: editorPreview.activeMaskEnabled
                onClicked: editorPreview.activeMaskEnabled = !editorPreview.activeMaskEnabled
            }

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskRetouchMode.length > 0
                      ? editorPreview.activeMaskRetouchMode + " retouch"
                      : (editorPreview.activeMaskKind.length > 0 ? editorPreview.activeMaskKind + " source" : "")
                horizontalAlignment: Text.AlignRight
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        Label {
            width: parent.width
            visible: editorPreview.activeMaskToolRole !== "retouch"
            text: "Selections inside the same mask set share the same adjustments. Pick any source in the set to grade the whole mask."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        width: parent.width
        title: "Looks"
        subtitle: "Fast local adjustments before the full control stack."
        visible: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        expandable: true
        expanded: false

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Warm"
                accent: "#d98962"
                active: editRoot.activeGradePreset === "warm"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("warm")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Cool"
                accent: "#7cb5ff"
                active: editRoot.activeGradePreset === "cool"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("cool")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Pop"
                accent: "#c99a52"
                active: editRoot.activeGradePreset === "pop"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("pop")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Desat"
                accent: "#d8d2ca"
                active: editRoot.activeGradePreset === "desat"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("desat")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Warm Tint"
                accent: "#d98962"
                active: editRoot.activeGradePreset === "tint-warm"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("tint-warm")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Cool Tint"
                accent: "#5e95d3"
                active: editRoot.activeGradePreset === "tint-cool"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("tint-cool")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Lift"
                accent: "#f0c987"
                active: editRoot.activeGradePreset === "lift"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("lift")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Burn"
                accent: "#8f6751"
                active: editRoot.activeGradePreset === "burn"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("burn")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Soften"
                accent: "#bca89d"
                active: editRoot.activeGradePreset === "soften"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("soften")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Teal Pop"
                accent: "#4fa6aa"
                active: editRoot.activeGradePreset === "teal-pop"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("teal-pop")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Reset"
                onClicked: editRoot.applyActiveMaskGradePreset("reset")
            }
        }
    }

    MaskSection {
        width: parent.width
        title: "Selection"
        subtitle: "Only the essentials. Use Edge below when you need more control."
        visible: editRoot.activeMaskSupportsSelectionRefine() &&
                 editorPreview.activeMaskToolRole !== "retouch"

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Invert"
                active: editorPreview.activeMaskSelectionInverted
                onClicked: editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Reset Edge"
                onClicked: {
                    editorPreview.activeMaskSelectionInverted = false
                    editorPreview.activeMaskSelectionExpand = 0
                    editorPreview.activeMaskSelectionEdgeContrast = 0
                    editorPreview.activeMaskSelectionDecontaminate = 0
                    editorPreview.activeMaskFeather = editRoot.activeMaskResetFeather()
                }
            }

        }

        Label {
            width: parent.width
            text: editorPreview.activeMaskSelectionInverted
                  ? "The mask is inverted. Adjustments now target everything outside the visible boundary."
                  : "Brush Add and Brush Erase paint the selection directly. Use Edge below only when the painted mask needs cleanup."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        width: parent.width
        title: "Edge"
        subtitle: "Trim or recover boundaries, then tighten soft edges or clean color spill."
        visible: editRoot.activeMaskSupportsSelectionRefine() &&
                 editorPreview.activeMaskToolRole !== "retouch"
        expandable: true
        expanded: editorPreview.activeMaskKind === "subject" || editorPreview.activeMaskKind === "object"

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Edge Add"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.startSelectionEdgeBrush(false)
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Edge Erase"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.startSelectionEdgeBrush(true)
                }
            }
        }

        Label {
            width: parent.width
            text: "Edge brushes are smaller than the normal brush and paint directly onto the mask. Edge Contrast tightens soft boundaries. Decontaminate mutes colored fringing along semi-transparent edges."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        InspectorSlider {
            width: parent.width
            label: "Mask Size"
            valueText: Math.round(editorPreview.activeMaskSelectionExpand).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSelectionExpand
            onValueDragged: function(value) { editorPreview.activeMaskSelectionExpand = value }
            onResetRequested: editorPreview.activeMaskSelectionExpand = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskFeather.toFixed(2)
            from: editorPreview.activeMaskKind === "polygon" ? 0.0 : 0.05
            to: editorPreview.activeMaskKind === "polygon" ? 0.2 : 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskFeather
            onValueDragged: function(value) { editorPreview.activeMaskFeather = value }
            onResetRequested: editorPreview.activeMaskFeather = editRoot.activeMaskResetFeather()
        }

        InspectorSlider {
            width: parent.width
            label: "Edge Contrast"
            valueText: Math.round(editorPreview.activeMaskSelectionEdgeContrast).toString()
            from: 0
            to: 100
            value: editorPreview.activeMaskSelectionEdgeContrast
            onValueDragged: function(value) { editorPreview.activeMaskSelectionEdgeContrast = value }
            onResetRequested: editorPreview.activeMaskSelectionEdgeContrast = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Decontaminate"
            valueText: Math.round(editorPreview.activeMaskSelectionDecontaminate).toString()
            from: 0
            to: 100
            value: editorPreview.activeMaskSelectionDecontaminate
            onValueDragged: function(value) { editorPreview.activeMaskSelectionDecontaminate = value }
            onResetRequested: editorPreview.activeMaskSelectionDecontaminate = 0
        }
    }

    MaskSection {
        width: parent.width
        title: "Adjust"
        subtitle: editorPreview.activeMaskToolRole === "retouch"
                  ? "Retouch uses its own stroke settings. Image adjustments apply only to selection mask sets."
                  : (editRoot.activeMaskHasAdjustments()
                     ? "The current mask set is modifying tone or color."
                     : "Collapsed until the current mask set needs image adjustments.")
        expandable: true
        expanded: editRoot.activeMaskHasAdjustments()
        visible: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"

        InspectorSlider {
            width: parent.width
            label: "Exposure"
            valueText: editorPreview.activeMaskExposure.toFixed(2) + " EV"
            from: -2
            to: 2
            value: editorPreview.activeMaskExposure
            onValueDragged: function(value) { editorPreview.activeMaskExposure = value }
            onResetRequested: editorPreview.activeMaskExposure = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Contrast"
            valueText: Math.round(editorPreview.activeMaskContrast).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskContrast
            onValueDragged: function(value) { editorPreview.activeMaskContrast = value }
            onResetRequested: editorPreview.activeMaskContrast = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Highlights"
            valueText: Math.round(editorPreview.activeMaskHighlights).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskHighlights
            onValueDragged: function(value) { editorPreview.activeMaskHighlights = value }
            onResetRequested: editorPreview.activeMaskHighlights = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Shadows"
            valueText: Math.round(editorPreview.activeMaskShadows).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskShadows
            onValueDragged: function(value) { editorPreview.activeMaskShadows = value }
            onResetRequested: editorPreview.activeMaskShadows = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Whites"
            valueText: Math.round(editorPreview.activeMaskWhites).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskWhites
            onValueDragged: function(value) { editorPreview.activeMaskWhites = value }
            onResetRequested: editorPreview.activeMaskWhites = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Blacks"
            valueText: Math.round(editorPreview.activeMaskBlacks).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskBlacks
            onValueDragged: function(value) { editorPreview.activeMaskBlacks = value }
            onResetRequested: editorPreview.activeMaskBlacks = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Temperature"
            valueText: Math.round(editorPreview.activeMaskTemperature).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskTemperature
            onValueDragged: function(value) { editorPreview.activeMaskTemperature = value }
            onResetRequested: editorPreview.activeMaskTemperature = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Tint"
            valueText: Math.round(editorPreview.activeMaskTint).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskTint
            onValueDragged: function(value) { editorPreview.activeMaskTint = value }
            onResetRequested: editorPreview.activeMaskTint = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Vibrance"
            valueText: Math.round(editorPreview.activeMaskVibrance).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskVibrance
            onValueDragged: function(value) { editorPreview.activeMaskVibrance = value }
            onResetRequested: editorPreview.activeMaskVibrance = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Overall Saturation"
            valueText: Math.round(editorPreview.activeMaskSaturation).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSaturation
            onValueDragged: function(value) { editorPreview.activeMaskSaturation = value }
            onResetRequested: editorPreview.activeMaskSaturation = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Shift"
            valueText: Math.round(editorPreview.activeMaskHueShift * 1.8).toString() + "°"
            from: -100
            to: 100
            value: editorPreview.activeMaskHueShift
            onValueDragged: function(value) { editorPreview.activeMaskHueShift = value }
            onResetRequested: editorPreview.activeMaskHueShift = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Chroma Shift"
            valueText: Math.round(editorPreview.activeMaskSaturationShift).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSaturationShift
            onValueDragged: function(value) { editorPreview.activeMaskSaturationShift = value }
            onResetRequested: editorPreview.activeMaskSaturationShift = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Tint Hue"
            valueText: Math.round(editorPreview.activeMaskColorizeHue * 360).toString() + "°"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskColorizeHue
            onValueDragged: function(value) { editorPreview.activeMaskColorizeHue = value }
            onResetRequested: editorPreview.activeMaskColorizeHue = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Tint Amount"
            valueText: Math.round(editorPreview.activeMaskColorizeStrength).toString() + "%"
            from: 0
            to: 100
            value: editorPreview.activeMaskColorizeStrength
            onValueDragged: function(value) { editorPreview.activeMaskColorizeStrength = value }
            onResetRequested: editorPreview.activeMaskColorizeStrength = 0
        }
    }

    MaskSection {
        width: parent.width
        title: "Shape"
        visible: editorPreview.hasActiveMask &&
                 editorPreview.activeMaskToolRole !== "retouch" &&
                 (editorPreview.activeMaskKind === "linear" || editorPreview.activeMaskKind === "radial")
        expandable: true
        expanded: true

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "linear"
            label: "Position"
            valueText: editorPreview.activeMaskPosition.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskPosition
            onValueDragged: function(value) { editorPreview.activeMaskPosition = value }
            onResetRequested: editorPreview.activeMaskPosition = 0.5
        }

        InspectorSlider {
            width: parent.width
            label: "Rotation"
            valueText: Math.round(editorPreview.activeMaskRotation).toString() + "°"
            from: -180
            to: 180
            stepSize: 1
            value: editorPreview.activeMaskRotation
            onValueDragged: function(value) { editorPreview.activeMaskRotation = value }
            onResetRequested: editorPreview.activeMaskRotation = 0
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Center X"
            valueText: editorPreview.activeMaskCenterX.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskCenterX
            onValueDragged: function(value) { editorPreview.activeMaskCenterX = value }
            onResetRequested: editorPreview.activeMaskCenterX = 0.5
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Center Y"
            valueText: editorPreview.activeMaskCenterY.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskCenterY
            onValueDragged: function(value) { editorPreview.activeMaskCenterY = value }
            onResetRequested: editorPreview.activeMaskCenterY = 0.5
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Radius X"
            valueText: editorPreview.activeMaskRadiusX.toFixed(2)
            from: 0.05
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRadiusX
            onValueDragged: function(value) { editorPreview.activeMaskRadiusX = value }
            onResetRequested: editorPreview.activeMaskRadiusX = 0.25
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Radius Y"
            valueText: editorPreview.activeMaskRadiusY.toFixed(2)
            from: 0.05
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRadiusY
            onValueDragged: function(value) { editorPreview.activeMaskRadiusY = value }
            onResetRequested: editorPreview.activeMaskRadiusY = 0.25
        }
    }

    MaskSection {
        width: parent.width
        title: "Brush"
        visible: editorPreview.activeMaskKind === "brush"
        expandable: true
        expanded: true

        Label {
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "remove" ||
                     editorPreview.activeMaskRetouchMode === "heal" ||
                     editorPreview.activeMaskRetouchMode === "clone"
            text: editorPreview.activeMaskRetouchMode === "remove"
                  ? "Paint over unwanted content to remove it from the image."
                  : editorPreview.activeMaskRetouchMode === "heal"
                    ? "Heal uses a softer fill pass for blemishes and seams."
                    : (editorPreview.activeMaskCloneSourceSet
                       ? "Clone Stamp is ready. Paint to stamp from the marked source point."
                       : "Clone Stamp needs a source point before you paint.")
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskRetouchMode === "clone"

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskCloneSourceSet ? "Repick Source" : "Pick Source"
                compact: true
                active: editRoot.cloneSourcePickArmed
                onClicked: editRoot.selectTool(editRoot.cloneSourcePickArmed ? "" : "clone-source")
            }

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskCloneSourceSet
                      ? ("Source: " + Math.round(editorPreview.activeMaskCloneSourceX * 100).toString() + ", " +
                         Math.round(editorPreview.activeMaskCloneSourceY * 100).toString())
                      : "Source not set"
                horizontalAlignment: Text.AlignRight
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
            }
        }

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            visible: editorPreview.activeMaskRetouchMode === "clone"

            ActionButton {
                Layout.fillWidth: true
                text: "Rotate -15°"
                compact: true
                onClicked: editorPreview.activeMaskCloneRotation = editorPreview.activeMaskCloneRotation - 15
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Rotate +15°"
                compact: true
                onClicked: editorPreview.activeMaskCloneRotation = editorPreview.activeMaskCloneRotation + 15
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Scale Down"
                compact: true
                onClicked: editorPreview.activeMaskCloneScale = editorPreview.activeMaskCloneScale - 0.1
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Scale Up"
                compact: true
                onClicked: editorPreview.activeMaskCloneScale = editorPreview.activeMaskCloneScale + 0.1
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Brush Size"
            valueText: Math.round(editorPreview.activeMaskBrushSize * 100).toString()
            from: 0.01
            to: 0.35
            stepSize: 0.01
            value: editorPreview.activeMaskBrushSize
            onValueDragged: function(value) { editorPreview.activeMaskBrushSize = value }
            onResetRequested: editorPreview.activeMaskBrushSize = 0.08
        }

        InspectorSlider {
            width: parent.width
            label: "Brush Flow"
            valueText: Math.round(editorPreview.activeMaskBrushFlow * 100).toString() + "%"
            from: 0.05
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushFlow
            onValueDragged: function(value) { editorPreview.activeMaskBrushFlow = value }
            onResetRequested: editorPreview.activeMaskBrushFlow = 1.0
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "clone"
            label: "Source Scale"
            valueText: editorPreview.activeMaskCloneScale.toFixed(2) + "x"
            from: 0.25
            to: 4.0
            stepSize: 0.01
            value: editorPreview.activeMaskCloneScale
            onValueDragged: function(value) { editorPreview.activeMaskCloneScale = value }
            onResetRequested: editorPreview.activeMaskCloneScale = 1.0
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "clone"
            label: "Source Rotation"
            valueText: Math.round(editorPreview.activeMaskCloneRotation).toString() + "°"
            from: -180
            to: 180
            stepSize: 1
            value: editorPreview.activeMaskCloneRotation
            onValueDragged: function(value) { editorPreview.activeMaskCloneRotation = value }
            onResetRequested: editorPreview.activeMaskCloneRotation = 0
        }

        RowLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskBrushPointCount.toString() + " brush points"
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
            }

            ActionButton {
                text: "Clear"
                compact: true
                onClicked: editorPreview.clearActiveBrushPoints()
            }
        }
    }

    MaskSection {
        width: parent.width
        title: "Polygon"
        visible: editorPreview.activeMaskKind === "polygon"
        expandable: true
        expanded: true

        RowLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskPolygonPointCount.toString() + " points"
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
            }

            ActionButton {
                text: "Undo Point"
                compact: true
                onClicked: editorPreview.removeLastActivePolygonPoint()
            }

            ActionButton {
                text: "Clear"
                compact: true
                onClicked: editorPreview.clearActivePolygonPoints()
            }
        }
    }

    MaskSection {
        width: parent.width
        title: "AI Refine"
        visible: editorPreview.activeMaskKind === "subject" || editorPreview.activeMaskKind === "object"
        expandable: true
        expanded: true

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskFeather.toFixed(2)
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskFeather
            onValueDragged: function(value) { editorPreview.activeMaskFeather = value }
            onResetRequested: editorPreview.activeMaskFeather = 0.5
        }

        Label {
            width: parent.width
            text: editorPreview.activeMaskKind === "object"
                  ? "Object picks come from the live AI preview on the canvas. If AI cannot isolate the click, the workflow falls back to manual refinement."
                  : "Subject masks stay editable inside the current mask set."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        width: parent.width
        title: "Luminance Range"
        visible: editorPreview.activeMaskKind === "luminance"
        expandable: true
        expanded: true

        InspectorSlider {
            width: parent.width
            label: "Minimum"
            valueText: editorPreview.activeMaskLuminanceMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskLuminanceMin
            onValueDragged: function(value) { editorPreview.activeMaskLuminanceMin = value }
            onResetRequested: editorPreview.activeMaskLuminanceMin = 0.2
        }

        InspectorSlider {
            width: parent.width
            label: "Maximum"
            valueText: editorPreview.activeMaskLuminanceMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskLuminanceMax
            onValueDragged: function(value) { editorPreview.activeMaskLuminanceMax = value }
            onResetRequested: editorPreview.activeMaskLuminanceMax = 0.8
        }

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskRangeFeather.toFixed(2)
            from: 0
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskRangeFeather
            onValueDragged: function(value) { editorPreview.activeMaskRangeFeather = value }
            onResetRequested: editorPreview.activeMaskRangeFeather = 0.1
        }
    }

    MaskSection {
        width: parent.width
        title: "Color Range"
        visible: editorPreview.activeMaskKind === "color"
        expandable: true
        expanded: true

        InspectorSlider {
            width: parent.width
            label: "Hue Center"
            valueText: Math.round(editorPreview.activeMaskHueCenter * 360).toString() + "°"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskHueCenter
            onValueDragged: function(value) { editorPreview.activeMaskHueCenter = value }
            onResetRequested: editorPreview.activeMaskHueCenter = 0.08
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Width"
            valueText: Math.round(editorPreview.activeMaskHueRange * 360).toString() + "°"
            from: 0.02
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskHueRange
            onValueDragged: function(value) { editorPreview.activeMaskHueRange = value }
            onResetRequested: editorPreview.activeMaskHueRange = 0.12
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Min"
            valueText: editorPreview.activeMaskSaturationMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskSaturationMin
            onValueDragged: function(value) { editorPreview.activeMaskSaturationMin = value }
            onResetRequested: editorPreview.activeMaskSaturationMin = 0.1
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Max"
            valueText: editorPreview.activeMaskSaturationMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskSaturationMax
            onValueDragged: function(value) { editorPreview.activeMaskSaturationMax = value }
            onResetRequested: editorPreview.activeMaskSaturationMax = 1.0
        }
    }
}
