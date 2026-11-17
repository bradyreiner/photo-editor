pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    Theme { id: theme }

    required property var overlay
    property alias fillColor: fillField.text
    property alias strokeColor: strokeField.text
    property real strokeWidth: 2
    property string outputMode: "layer"
    property bool maskTarget: false

    signal appearanceChanged(string fillColor, string strokeColor, real strokeWidth)

    implicitWidth: Math.max(theme.scaled(300), content.implicitWidth + theme.space12)
    implicitHeight: content.implicitHeight + theme.space12

    function publishAppearance() {
        if (fillField.acceptableInput && strokeField.acceptableInput && widthField.acceptableInput)
            appearanceChanged(fillColor.trim(), strokeColor.trim(), strokeWidth)
    }

    function setOutputMode(mode) {
        if (mode !== "layer" && mode !== "selection")
            return
        if (outputMode === mode)
            return
        outputMode = mode
    }

    function updateFinishPolicy() {
        if (root.overlay) {
            root.overlay.requireClosedPathToFinish = root.maskTarget || root.outputMode === "selection"
            root.overlay.requireAllPathsClosedToFinish = root.maskTarget
        }
    }

    function coordinateText(axis) {
        if (!root.overlay || !root.overlay.hasSelectedAnchor)
            return ""
        const value = root.overlay.selectedAnchorPosition(axis)
        return Number.isFinite(value) ? value.toFixed(2) : ""
    }

    function syncAnchorPosition() {
        if (!xPositionField.activeFocus)
            xPositionField.text = coordinateText("x")
        if (!yPositionField.activeFocus)
            yPositionField.text = coordinateText("y")
    }

    function applyAnchorPosition(axis, field) {
        if (!root.overlay || !root.overlay.hasSelectedAnchor || !field.acceptableInput)
            return
        if (root.overlay.setSelectedAnchorPosition(axis, Number(field.text)))
            syncAnchorPosition()
    }

    function setAnchorKind(kind) {
        if (root.overlay)
            root.overlay.setSelectedAnchorKind(kind)
    }

    function toggleHandleLink() {
        if (root.overlay)
            root.overlay.setSelectedHandlesLinked(root.overlay.selectedHandleState() !== "linked")
    }

    onOverlayChanged: updateFinishPolicy()
    onOutputModeChanged: updateFinishPolicy()
    onMaskTargetChanged: updateFinishPolicy()
    Component.onCompleted: {
        updateFinishPolicy()
        syncAnchorPosition()
    }

    Connections {
        target: root.overlay
        function onSelectedAnchorIdChanged() { root.syncAnchorPosition() }
        function onSelectedSubpathIdChanged() { root.syncAnchorPosition() }
        function onEditDocumentChanged() { root.syncAnchorPosition() }
        function onCommitFailed() { root.syncAnchorPosition() }
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: theme.space6
        spacing: theme.gapDense

        Item {
            Layout.fillWidth: true
            implicitHeight: commandFlow.implicitHeight

            Flow {
                id: commandFlow
                objectName: "photoVectorPathCommandFlow"
                anchors.fill: parent
                spacing: theme.gapDense

                Components.MiniButton {
                    objectName: "photoVectorPathCloseButton"
                    text: qsTr("Close path")
                    iconName: "shape"
                    showLeadingIcon: true
                    role: "secondary"
                    enabled: root.overlay && root.overlay.canCloseCurrentPath && !root.overlay.commitPending
                    tooltip: root.maskTarget
                             ? qsTr("Close the next eligible open subpath. Repeat until every path is closed.")
                             : qsTr("Close an open subpath after it has at least three anchors.")
                    onClicked: root.overlay.closeOpenPath()
                }
                Components.ActionButton {
                    objectName: "photoVectorPathFinishButton"
                    text: qsTr("Finish")
                    iconName: "check"
                    tone: "primary"
                    compact: true
                    enabled: root.overlay && root.overlay.canFinishPath
                    tooltipText: root.maskTarget && root.overlay && !root.overlay.hasOnlyClosedPaths
                                 ? qsTr("Close every path to create a vector mask.")
                                 : root.outputMode === "selection" && root.overlay && !root.overlay.hasClosedPath
                                   ? qsTr("Close a path to create a selection.")
                                 : qsTr("Commit this path edit.")
                    onClicked: root.overlay.finishPath()
                }
                Components.MiniButton {
                    objectName: "photoVectorPathCancelButton"
                    text: qsTr("Cancel")
                    iconName: "close"
                    showLeadingIcon: true
                    role: "ghost"
                    enabled: root.overlay && root.overlay.canCancelPath
                    tooltip: qsTr("Discard the current path edit.")
                    onClicked: root.overlay.cancelPath()
                }
                Components.MiniButton {
                    objectName: "photoVectorPathDeleteAnchorButton"
                    text: qsTr("Delete anchor")
                    iconName: "trash"
                    showLeadingIcon: true
                    role: "secondary"
                    enabled: root.overlay && root.overlay.hasSelectedAnchor && !root.overlay.commitPending
                    tooltip: qsTr("Remove the selected anchor from this path.")
                    onClicked: root.overlay.deleteSelectedAnchor()
                }
            }
        }

        Label {
            objectName: "photoVectorPathSelectionGuidance"
            Layout.fillWidth: true
            visible: root.overlay &&
                     (root.maskTarget ? !root.overlay.hasOnlyClosedPaths
                                      : root.outputMode === "selection" && !root.overlay.hasClosedPath)
            text: root.maskTarget ? qsTr("Close every subpath with at least three anchors to create a vector mask.")
                                  : qsTr("Close a path to create a selection.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: targetFlow.implicitHeight
            visible: !root.maskTarget

            Flow {
                id: targetFlow
                objectName: "photoVectorPathTargetFlow"
                anchors.fill: parent
                spacing: theme.gapDense

                Label {
                    text: qsTr("Destination")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                Components.MiniButton {
                    objectName: "photoVectorPathLayerTargetButton"
                    text: qsTr("Shape layer")
                    iconName: "layers"
                    showLeadingIcon: true
                    role: "segmented"
                    active: root.outputMode === "layer"
                    enabled: !root.overlay || !root.overlay.commitPending
                    onClicked: root.setOutputMode("layer")
                }
                Components.MiniButton {
                    objectName: "photoVectorPathSelectionTargetButton"
                    text: qsTr("Selection")
                    iconName: "selection-new"
                    showLeadingIcon: true
                    role: "segmented"
                    active: root.outputMode === "selection"
                    enabled: !root.overlay || !root.overlay.commitPending
                    onClicked: root.setOutputMode("selection")
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: visible ? appearanceFlow.implicitHeight : 0
            visible: !root.maskTarget

            Flow {
                id: appearanceFlow
                objectName: "photoVectorPathAppearanceFlow"
                anchors.fill: parent
                spacing: theme.gapDense

                Row {
                    spacing: theme.gapDense
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Fill")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    Components.ToneTextField {
                        id: fillField
                        objectName: "photoVectorPathFillColorField"
                        width: theme.scaled(92)
                        height: theme.controlHeightDense
                        text: "#4a78ff"
                        placeholderText: "#RRGGBB"
                        accessibleLabel: qsTr("Vector fill color")
                        enabled: !root.overlay || !root.overlay.commitPending
                        validator: RegularExpressionValidator {
                            regularExpression: /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/
                        }
                        onEditingFinished: root.publishAppearance()
                    }
                }
                Row {
                    spacing: theme.gapDense
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Stroke")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    Components.ToneTextField {
                        id: strokeField
                        objectName: "photoVectorPathStrokeColorField"
                        width: theme.scaled(92)
                        height: theme.controlHeightDense
                        text: "#ffffff"
                        placeholderText: "#RRGGBB"
                        accessibleLabel: qsTr("Vector stroke color")
                        enabled: !root.overlay || !root.overlay.commitPending
                        validator: RegularExpressionValidator {
                            regularExpression: /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/
                        }
                        onEditingFinished: root.publishAppearance()
                    }
                }
                Row {
                    spacing: theme.gapDense
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Width")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    Components.ToneTextField {
                        id: widthField
                        objectName: "photoVectorPathStrokeWidthField"
                        width: theme.scaled(64)
                        height: theme.controlHeightDense
                        text: String(root.strokeWidth)
                        numericEntry: true
                        enabled: !root.overlay || !root.overlay.commitPending
                        placeholderText: "px"
                        accessibleLabel: qsTr("Vector stroke width in pixels")
                        validator: DoubleValidator {
                            bottom: 0
                            top: 4096
                            decimals: 2
                            notation: DoubleValidator.StandardNotation
                        }
                        onEditingFinished: {
                            if (acceptableInput) {
                                root.strokeWidth = Number(text)
                                root.publishAppearance()
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: anchorFlow.implicitHeight

            Flow {
                id: anchorFlow
                objectName: "photoVectorPathAnchorFlow"
                anchors.fill: parent
                spacing: theme.gapDense

                Label {
                    text: qsTr("Anchor")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                Components.MiniButton {
                    objectName: "photoVectorPathCornerButton"
                    text: qsTr("Corner")
                    role: "segmented"
                    active: root.overlay && root.overlay.hasSelectedAnchor && root.overlay.selectedHandleState() !== "linked"
                    enabled: root.overlay && root.overlay.hasSelectedAnchor && !root.overlay.commitPending
                    tooltip: qsTr("Keep incoming and outgoing handles independent.")
                    onClicked: root.setAnchorKind("corner")
                }
                Components.MiniButton {
                    objectName: "photoVectorPathSmoothButton"
                    text: qsTr("Smooth")
                    role: "segmented"
                    active: root.overlay && root.overlay.hasSelectedAnchor && root.overlay.selectedHandleState() === "linked"
                    enabled: root.overlay && root.overlay.hasSelectedAnchor && !root.overlay.commitPending
                    tooltip: qsTr("Align the handles to create a smooth curve.")
                    onClicked: root.setAnchorKind("smooth")
                }
                Components.MiniButton {
                    objectName: "photoVectorPathHandleLinkButton"
                    text: root.overlay && root.overlay.selectedHandleState() === "linked"
                          ? qsTr("Break handles") : qsTr("Link handles")
                    iconName: root.overlay && root.overlay.selectedHandleState() === "linked" ? "link" : "unlink"
                    showLeadingIcon: true
                    role: "secondary"
                    enabled: root.overlay && root.overlay.hasSelectedAnchor && !root.overlay.commitPending
                    tooltip: qsTr("Link handles to keep a smooth tangent, or break them to edit independently.")
                    onClicked: root.toggleHandleLink()
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: positionFlow.implicitHeight

            Flow {
                id: positionFlow
                objectName: "photoVectorPathPositionFlow"
                anchors.fill: parent
                spacing: theme.gapDense

                Label {
                    text: qsTr("Position")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                Label {
                    text: root.overlay && root.overlay.editDocument.coordinateSpace === "document"
                          ? qsTr("X px") : qsTr("X %")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                Components.ToneTextField {
                    id: xPositionField
                    objectName: "photoVectorPathAnchorXField"
                    width: theme.scaled(72)
                    height: theme.controlHeightDense
                    text: ""
                    numericEntry: true
                    enabled: root.overlay && root.overlay.hasSelectedAnchor && !root.overlay.commitPending
                    placeholderText: root.overlay && root.overlay.editDocument.coordinateSpace === "document" ? qsTr("px") : qsTr("%")
                    accessibleLabel: qsTr("Selected anchor horizontal position")
                    validator: DoubleValidator { bottom: -10000000; top: 10000000; decimals: 4; notation: DoubleValidator.StandardNotation }
                    onEditingFinished: root.applyAnchorPosition("x", xPositionField)
                }
                Label {
                    text: root.overlay && root.overlay.editDocument.coordinateSpace === "document"
                          ? qsTr("Y px") : qsTr("Y %")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                Components.ToneTextField {
                    id: yPositionField
                    objectName: "photoVectorPathAnchorYField"
                    width: theme.scaled(72)
                    height: theme.controlHeightDense
                    text: ""
                    numericEntry: true
                    enabled: root.overlay && root.overlay.hasSelectedAnchor && !root.overlay.commitPending
                    placeholderText: root.overlay && root.overlay.editDocument.coordinateSpace === "document" ? qsTr("px") : qsTr("%")
                    accessibleLabel: qsTr("Selected anchor vertical position")
                    validator: DoubleValidator { bottom: -10000000; top: 10000000; decimals: 4; notation: DoubleValidator.StandardNotation }
                    onEditingFinished: root.applyAnchorPosition("y", yPositionField)
                }
            }
        }

        Label {
            objectName: "photoVectorPathCommitError"
            Layout.fillWidth: true
            visible: root.overlay && root.overlay.commitError.length > 0
            text: root.overlay ? root.overlay.commitError : ""
            color: theme.error
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }

        Label {
            objectName: "photoVectorPathMaskTargetLabel"
            Layout.fillWidth: true
            visible: root.maskTarget
            text: qsTr("Editing vector mask")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }

        Label {
            objectName: "photoVectorPathInstructions"
            Layout.fillWidth: true
            text: qsTr("Click to place anchors, or click a curve to add one. Drag an anchor or handle to edit. Delete removes the selected anchor. Alt-drag breaks handles. Enter applies; Escape cancels.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }
    }
}
