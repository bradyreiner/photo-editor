import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Column {
    id: root

    required property var editRoot
    spacing: 10

    InspectorSection {
        width: parent.width
        title: "Tools"
        subtitle: "Start with Subject or Object, paint the mask directly, and keep retouch separate."

        Rectangle {
            width: parent.width
            radius: 10
            color: "#161a1e"
            border.width: 1
            border.color: editorPreview.hasActiveMask
                          ? Qt.alpha(editRoot.maskAccent(
                                         editorPreview.maskStack[editorPreview.activeMaskIndex]),
                                     0.45)
                          : "#2b3138"
            implicitHeight: statusColumn.implicitHeight + 20

            Column {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4

                Label {
                    width: parent.width
                    text: editorPreview.hasActiveMask
                          ? (editorPreview.activeMaskToolRole === "retouch"
                             ? editRoot.activeToolLabel()
                             : "Editing " + editorPreview.activeMaskName)
                          : "No active mask"
                    color: "#f2f4f6"
                    font.family: "Avenir Next"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    width: parent.width
                    text: editRoot.maskFallbackMode === "polygon"
                          ? "AI missed the target. Continue the polygon on the canvas and double-click to finish."
                          : editRoot.maskFallbackMode === "brush"
                            ? "AI missed the target. Remove Brush is active so you can keep painting."
                              : editRoot.cloneSourcePickArmed
                              ? "Click the source area for the Clone Stamp."
                              : editRoot.objectPickArmed && editRoot.objectPickMode === "remove"
                                ? (editRoot.objectPickCreateNew
                                   ? "Click or drag across a removable object to start a new retouch stroke."
                                   : "Click or drag across removable objects to expand the current staged removal.")
                                : editRoot.objectPickArmed && editRoot.objectPickMode === "selection-subtract"
                                  ? "Click an object to subtract it from the current selection."
                                : editRoot.objectPickArmed
                                  ? (editRoot.objectPickCreateNew
                                     ? "Click an object to start a new selection."
                                     : "Click an object to add it to the current selection.")
                                  : editRoot.polygonDrawArmed
                                    ? "Click to place polygon points. Double-click to finish."
                                    : editRoot.lassoDrawArmed
                                      ? "Drag directly on the canvas to draw a freehand selection."
                                      : "Start with Subject or Object Select, then use Brush Add or Brush Erase to paint the mask directly."
                    wrapMode: Text.WordWrap
                    color: "#8f98a2"
                    font.family: "Avenir Next"
                    font.pixelSize: 11
                }
            }
        }

        Label {
            width: parent.width
            text: "PREVIEW MODE"
            color: "#6b7580"
            font.family: "Menlo"
            font.pixelSize: 8
            font.weight: Font.DemiBold
            font.letterSpacing: 0.8
        }

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Overlay"
                compact: true
                active: editorPreview.maskPreviewMode === "overlay"
                enabled: editorPreview.hasAsset
                onClicked: editorPreview.maskPreviewMode = "overlay"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Gray"
                compact: true
                active: editorPreview.maskPreviewMode === "grayscale"
                enabled: editorPreview.hasAsset
                tooltipText: "Show a neutral grayscale matte."
                onClicked: editorPreview.maskPreviewMode = "grayscale"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Black / White"
                compact: true
                active: editorPreview.maskPreviewMode === "black-on-white"
                enabled: editorPreview.hasAsset
                tooltipText: "Show the mask as black on a white field."
                onClicked: editorPreview.maskPreviewMode = "black-on-white"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "White / Black"
                compact: true
                active: editorPreview.maskPreviewMode === "white-on-black"
                enabled: editorPreview.hasAsset
                tooltipText: "Show the mask as white on a dark field."
                onClicked: editorPreview.maskPreviewMode = "white-on-black"
            }
        }

        Label {
            width: parent.width
            text: "SELECTION SOURCES"
            color: "#6b7580"
            font.family: "Menlo"
            font.pixelSize: 8
            font.weight: Font.DemiBold
            font.letterSpacing: 0.8
        }

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.hasActiveMask ? "Add Object" : "Object Select"
                compact: true
                active: editRoot.objectPickArmed &&
                        editRoot.objectPickMode === "selection" &&
                        !editRoot.objectPickCreateNew
                enabled: editorPreview.hasAsset
                onClicked: editRoot.selectTool(
                               editRoot.objectPickArmed &&
                               editRoot.objectPickMode === "selection" &&
                               !editRoot.objectPickCreateNew ? "" : "smart")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "New Selection"
                compact: true
                visible: editorPreview.hasActiveMask
                active: editRoot.objectPickArmed &&
                        editRoot.objectPickMode === "selection" &&
                        editRoot.objectPickCreateNew
                enabled: editorPreview.hasAsset
                onClicked: editRoot.selectTool(
                               editRoot.objectPickArmed &&
                               editRoot.objectPickMode === "selection" &&
                               editRoot.objectPickCreateNew ? "" : "smart-new")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Subtract Object"
                compact: true
                active: editRoot.objectPickArmed &&
                        editRoot.objectPickMode === "selection-subtract"
                enabled: editorPreview.hasAsset &&
                         editorPreview.hasActiveMask
                onClicked: editRoot.selectTool(
                               editRoot.objectPickArmed &&
                               editRoot.objectPickMode === "selection-subtract"
                               ? ""
                               : "smart-subtract")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Lasso"
                compact: true
                active: editRoot.lassoDrawArmed
                onClicked: editRoot.selectTool(editRoot.lassoDrawArmed ? "" : "lasso")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Polygon"
                compact: true
                active: editRoot.polygonDrawArmed
                onClicked: {
                    if (!editRoot.polygonDrawArmed && editorPreview.activeMaskKind !== "polygon")
                        editorPreview.startPolygonSelection()
                    editRoot.selectTool(editRoot.polygonDrawArmed ? "" : "polygon")
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "AI Subject"
                compact: true
                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.addSubjectMask()
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Linear"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.addMask("linear")
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Radial"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.addMask("radial")
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Luma Range"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.addMask("luminance")
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Color Range"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.addMask("color")
                }
            }
        }

        Label {
            width: parent.width
            text: "PAINT THE SELECTION"
            color: "#6b7580"
            font.family: "Menlo"
            font.pixelSize: 8
            font.weight: Font.DemiBold
            font.letterSpacing: 0.8
            topPadding: 4
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Brush Add"
                compact: true
                active: editorPreview.activeMaskKind === "brush" &&
                        editorPreview.activeMaskOperation === "add" &&
                        editorPreview.activeMaskRetouchMode.length === 0
                enabled: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.startSelectionBrush(false)
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Brush Erase"
                compact: true
                active: editorPreview.activeMaskKind === "brush" &&
                        editorPreview.activeMaskOperation === "subtract" &&
                        editorPreview.activeMaskRetouchMode.length === 0
                enabled: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.startSelectionBrush(true)
                }
            }
        }

        Label {
            width: parent.width
            text: "RETOUCH"
            color: "#6b7580"
            font.family: "Menlo"
            font.pixelSize: 8
            font.weight: Font.DemiBold
            font.letterSpacing: 0.8
            topPadding: 4
        }

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Remove Object"
                compact: true
                active: editRoot.objectPickArmed &&
                        editRoot.objectPickMode === "remove" &&
                        !editRoot.objectPickCreateNew
                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                onClicked: editRoot.selectTool(
                               editRoot.objectPickArmed &&
                               editRoot.objectPickMode === "remove" &&
                               !editRoot.objectPickCreateNew ? "" : "remove")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "New Remove"
                compact: true
                active: editRoot.objectPickArmed &&
                        editRoot.objectPickMode === "remove" &&
                        editRoot.objectPickCreateNew
                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                onClicked: editRoot.selectTool(
                               editRoot.objectPickArmed &&
                               editRoot.objectPickMode === "remove" &&
                               editRoot.objectPickCreateNew ? "" : "remove-new")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Remove Brush"
                compact: true
                active: editorPreview.activeMaskKind === "brush" &&
                        editorPreview.activeMaskRetouchMode === "remove"
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.startRemoveBrush()
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Heal"
                compact: true
                active: editorPreview.activeMaskKind === "brush" &&
                        editorPreview.activeMaskRetouchMode === "heal"
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.startHealBrush()
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Clone"
                compact: true
                active: editorPreview.activeMaskKind === "brush" &&
                        editorPreview.activeMaskRetouchMode === "clone"
                onClicked: {
                    editRoot.clearTransientTools()
                    if (editorPreview.activeMaskKind === "brush" &&
                            editorPreview.activeMaskRetouchMode === "clone") {
                        editRoot.selectTool("clone-source")
                    } else {
                        editorPreview.startCloneBrush()
                        editRoot.selectTool("clone-source")
                    }
                }
            }
        }

        RowLayout {
            visible: editorPreview.hasActiveMask &&
                     editorPreview.activeMaskToolRole === "retouch" &&
                     !editorPreview.activeMaskCommitted
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskRetouchMode === "remove"
                      ? "Remove"
                      : (editorPreview.activeMaskRetouchMode === "heal" ? "Apply Heal" : "Apply")
                compact: true
                onClicked: editorPreview.applyActiveRetouch()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Cancel"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.cancelActiveRetouch()
                }
            }
        }

        Label {
            visible: !editorPreview.subjectMaskAvailable
            width: parent.width
            text: "AI Subject, Object Select, and Remove Object require macOS 14 or later."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: "Avenir Next"
            font.pixelSize: 11
        }
    }

    InspectorSection {
        width: parent.width
        title: "Mask Sets"
        subtitle: "Selection sources compose one editable mask, and adjustments live on the set."

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
                                    font.family: "Avenir Next"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.sourceCount.toString() + " sources"
                                    color: "#8f98a2"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }

                            ActionButton {
                                text: modelData.enabled ? "On" : "Off"
                                compact: true
                                active: modelData.enabled
                                accent: setColor
                                enabled: firstSource !== null
                                onClicked: {
                                    if (!firstSource)
                                        return
                                    editorPreview.activeMaskIndex = firstSource.flatIndex
                                    editorPreview.activeMaskEnabled = !modelData.enabled
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
                                    onClicked: editorPreview.activeMaskIndex =
                                                   (editorPreview.activeMaskIndex === modelData.flatIndex
                                                    ? -1
                                                    : modelData.flatIndex)
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
                                            font.family: "Avenir Next"
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.kind + " · " + modelData.operation
                                            color: "#8f98a2"
                                            font.family: "Avenir Next"
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
                        font.family: "Avenir Next"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }

                    Label {
                        width: parent.width
                        text: "Start with AI Subject, Pick Object, a shape mask, or a manual selection."
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: "#6b7580"
                        font.family: "Avenir Next"
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

            ActionButton {
                Layout.fillWidth: true
                text: "Delete"
                compact: true
                onClicked: editorPreview.removeActiveMask()
            }
        }
    }

    InspectorSection {
        width: parent.width
        title: "Retouch"
        subtitle: "Heal, clone, and remove strokes are staged separately from selection masks."

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
                        onClicked: editorPreview.activeMaskIndex =
                                       (editorPreview.activeMaskIndex === modelData.flatIndex
                                        ? -1
                                        : modelData.flatIndex)
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
                                font.family: "Avenir Next"
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
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                elide: Text.ElideRight
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
                font.family: "Avenir Next"
                font.pixelSize: 12
            }
        }
    }

    InspectorSection {
        width: parent.width
        title: editorPreview.activeMaskToolRole === "retouch" ? "Active Retouch" : "Active Source"
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
                font.family: "Avenir Next"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }

    InspectorSection {
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
            font.family: "Avenir Next"
            font.pixelSize: 11
        }
    }

    InspectorSection {
        width: parent.width
        title: "Edge"
        subtitle: "Trim or recover boundaries with a smaller direct paint brush."
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
            text: "Edge brushes are smaller than the normal brush and paint directly onto the mask."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: "Avenir Next"
            font.pixelSize: 11
        }

        InspectorSlider {
            width: parent.width
            label: "Mask Size"
            valueText: Math.round(editorPreview.activeMaskSelectionExpand).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSelectionExpand
            onValueDragged: editorPreview.activeMaskSelectionExpand = value
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
            onValueDragged: editorPreview.activeMaskFeather = value
            onResetRequested: editorPreview.activeMaskFeather = editRoot.activeMaskResetFeather()
        }
    }

    InspectorSection {
        width: parent.width
        title: "Adjustment"
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
            onValueDragged: editorPreview.activeMaskExposure = value
            onResetRequested: editorPreview.activeMaskExposure = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Contrast"
            valueText: Math.round(editorPreview.activeMaskContrast).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskContrast
            onValueDragged: editorPreview.activeMaskContrast = value
            onResetRequested: editorPreview.activeMaskContrast = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Highlights"
            valueText: Math.round(editorPreview.activeMaskHighlights).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskHighlights
            onValueDragged: editorPreview.activeMaskHighlights = value
            onResetRequested: editorPreview.activeMaskHighlights = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Shadows"
            valueText: Math.round(editorPreview.activeMaskShadows).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskShadows
            onValueDragged: editorPreview.activeMaskShadows = value
            onResetRequested: editorPreview.activeMaskShadows = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Whites"
            valueText: Math.round(editorPreview.activeMaskWhites).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskWhites
            onValueDragged: editorPreview.activeMaskWhites = value
            onResetRequested: editorPreview.activeMaskWhites = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Blacks"
            valueText: Math.round(editorPreview.activeMaskBlacks).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskBlacks
            onValueDragged: editorPreview.activeMaskBlacks = value
            onResetRequested: editorPreview.activeMaskBlacks = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Temperature"
            valueText: Math.round(editorPreview.activeMaskTemperature).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskTemperature
            onValueDragged: editorPreview.activeMaskTemperature = value
            onResetRequested: editorPreview.activeMaskTemperature = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Tint"
            valueText: Math.round(editorPreview.activeMaskTint).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskTint
            onValueDragged: editorPreview.activeMaskTint = value
            onResetRequested: editorPreview.activeMaskTint = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Vibrance"
            valueText: Math.round(editorPreview.activeMaskVibrance).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskVibrance
            onValueDragged: editorPreview.activeMaskVibrance = value
            onResetRequested: editorPreview.activeMaskVibrance = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Overall Saturation"
            valueText: Math.round(editorPreview.activeMaskSaturation).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSaturation
            onValueDragged: editorPreview.activeMaskSaturation = value
            onResetRequested: editorPreview.activeMaskSaturation = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Shift"
            valueText: Math.round(editorPreview.activeMaskHueShift * 1.8).toString() + "°"
            from: -100
            to: 100
            value: editorPreview.activeMaskHueShift
            onValueDragged: editorPreview.activeMaskHueShift = value
            onResetRequested: editorPreview.activeMaskHueShift = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Chroma Shift"
            valueText: Math.round(editorPreview.activeMaskSaturationShift).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSaturationShift
            onValueDragged: editorPreview.activeMaskSaturationShift = value
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
            onValueDragged: editorPreview.activeMaskColorizeHue = value
            onResetRequested: editorPreview.activeMaskColorizeHue = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Tint Amount"
            valueText: Math.round(editorPreview.activeMaskColorizeStrength).toString() + "%"
            from: 0
            to: 100
            value: editorPreview.activeMaskColorizeStrength
            onValueDragged: editorPreview.activeMaskColorizeStrength = value
            onResetRequested: editorPreview.activeMaskColorizeStrength = 0
        }
    }

    InspectorSection {
        width: parent.width
        title: "Shape"
        visible: editorPreview.hasActiveMask &&
                 editorPreview.activeMaskToolRole !== "retouch" &&
                 (editorPreview.activeMaskKind === "linear" || editorPreview.activeMaskKind === "radial")

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "linear"
            label: "Position"
            valueText: editorPreview.activeMaskPosition.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskPosition
            onValueDragged: editorPreview.activeMaskPosition = value
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
            onValueDragged: editorPreview.activeMaskRotation = value
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
            onValueDragged: editorPreview.activeMaskCenterX = value
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
            onValueDragged: editorPreview.activeMaskCenterY = value
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
            onValueDragged: editorPreview.activeMaskRadiusX = value
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
            onValueDragged: editorPreview.activeMaskRadiusY = value
            onResetRequested: editorPreview.activeMaskRadiusY = 0.25
        }
    }

    InspectorSection {
        width: parent.width
        title: "Brush"
        visible: editorPreview.activeMaskKind === "brush"

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
            font.family: "Avenir Next"
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
                font.family: "Avenir Next"
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
            onValueDragged: editorPreview.activeMaskBrushSize = value
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
            onValueDragged: editorPreview.activeMaskBrushFlow = value
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
            onValueDragged: editorPreview.activeMaskCloneScale = value
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
            onValueDragged: editorPreview.activeMaskCloneRotation = value
            onResetRequested: editorPreview.activeMaskCloneRotation = 0
        }

        RowLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskBrushPointCount.toString() + " brush points"
                color: "#8f98a2"
                font.family: "Avenir Next"
                font.pixelSize: 11
            }

            ActionButton {
                text: "Clear"
                compact: true
                onClicked: editorPreview.clearActiveBrushPoints()
            }
        }
    }

    InspectorSection {
        width: parent.width
        title: "Polygon"
        visible: editorPreview.activeMaskKind === "polygon"

        RowLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskPolygonPointCount.toString() + " points"
                color: "#8f98a2"
                font.family: "Avenir Next"
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

    InspectorSection {
        width: parent.width
        title: "AI Mask"
        visible: editorPreview.activeMaskKind === "subject" || editorPreview.activeMaskKind === "object"

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskFeather.toFixed(2)
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskFeather
            onValueDragged: editorPreview.activeMaskFeather = value
            onResetRequested: editorPreview.activeMaskFeather = 0.5
        }

        Label {
            width: parent.width
            text: editorPreview.activeMaskKind === "object"
                  ? "Object picks come from the live AI preview on the canvas. If AI cannot isolate the click, the workflow falls back to manual refinement."
                  : "Subject masks stay editable inside the current mask set."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: "Avenir Next"
            font.pixelSize: 11
        }
    }

    InspectorSection {
        width: parent.width
        title: "Luminance Range"
        visible: editorPreview.activeMaskKind === "luminance"

        InspectorSlider {
            width: parent.width
            label: "Minimum"
            valueText: editorPreview.activeMaskLuminanceMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskLuminanceMin
            onValueDragged: editorPreview.activeMaskLuminanceMin = value
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
            onValueDragged: editorPreview.activeMaskLuminanceMax = value
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
            onValueDragged: editorPreview.activeMaskRangeFeather = value
            onResetRequested: editorPreview.activeMaskRangeFeather = 0.1
        }
    }

    InspectorSection {
        width: parent.width
        title: "Color Range"
        visible: editorPreview.activeMaskKind === "color"

        InspectorSlider {
            width: parent.width
            label: "Hue Center"
            valueText: Math.round(editorPreview.activeMaskHueCenter * 360).toString() + "°"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskHueCenter
            onValueDragged: editorPreview.activeMaskHueCenter = value
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
            onValueDragged: editorPreview.activeMaskHueRange = value
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
            onValueDragged: editorPreview.activeMaskSaturationMin = value
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
            onValueDragged: editorPreview.activeMaskSaturationMax = value
            onResetRequested: editorPreview.activeMaskSaturationMax = 1.0
        }
    }
}
