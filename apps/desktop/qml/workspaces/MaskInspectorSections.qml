import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Column {
    id: root

    required property var editRoot
    Theme { id: theme }
    property string maskSearchQuery: ""

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
        if (mode === "dust")
            return "#c2b36a"
        if (mode === "red-eye")
            return "#d36a5e"
        if (mode === "skin-cleanup")
            return "#d7a08c"
        if (mode === "frequency-smoothing")
            return "#c994d8"
        return "#d98962"
    }

    function maskTreeSources(sources) {
        const input = Array.isArray(sources) ? sources : []
        const byParent = ({})
        const byId = ({})
        for (let i = 0; i < input.length; ++i) {
            const source = input[i]
            if (source.id && source.id.length > 0)
                byId[source.id] = source
            const parentId = source.parentId || ""
            if (!byParent[parentId])
                byParent[parentId] = []
            byParent[parentId].push(source)
        }

        const result = []
        const visited = ({})
        function appendSource(source, depth) {
            if (!source)
                return
            const key = source.id && source.id.length > 0 ? source.id : "flat-" + source.flatIndex
            if (visited[key])
                return
            visited[key] = true
            const node = ({})
            for (const propertyName in source)
                node[propertyName] = source[propertyName]
            node.depth = depth
            result.push(node)
            const children = byParent[source.id] || []
            for (let childIndex = 0; childIndex < children.length; ++childIndex)
                appendSource(children[childIndex], depth + 1)
        }

        const roots = []
        for (let sourceIndex = 0; sourceIndex < input.length; ++sourceIndex) {
            const source = input[sourceIndex]
            const parentId = source.parentId || ""
            if (parentId.length === 0 || !byId[parentId])
                roots.push(source)
        }
        for (let rootIndex = 0; rootIndex < roots.length; ++rootIndex)
            appendSource(roots[rootIndex], 0)
        for (let fallbackIndex = 0; fallbackIndex < input.length; ++fallbackIndex)
            appendSource(input[fallbackIndex], 0)
        return result
    }

    function maskMatchesSearch(source) {
        const query = root.maskSearchQuery.trim().toLowerCase()
        if (query.length === 0)
            return true
        const searchable = ((source.name || "") + " " +
                            (source.kind || "") + " " +
                            (source.operation || "") + " " +
                            (source.maskSetName || "")).toLowerCase()
        return searchable.indexOf(query) >= 0
    }

    function retouchModeLabel(modeName) {
        const mode = String(modeName || "").trim().toLowerCase()
        if (mode === "remove")
            return "Remove"
        if (mode === "heal")
            return "Heal"
        if (mode === "patch")
            return "Patch"
        if (mode === "clone")
            return "Clone"
        if (mode === "dust")
            return "Dust"
        if (mode === "red-eye")
            return "Red Eye"
        if (mode === "skin-cleanup")
            return "Skin Cleanup"
        if (mode === "frequency-smoothing")
            return "Frequency Smoothing"
        return "Retouch"
    }

    function retouchAuditText(retouch) {
        const auditLabel = String(retouch.retouchAuditLabel || "")
        if (auditLabel.length > 0)
            return auditLabel
        const status = retouch.committed ? "applied" : "staged"
        const enabled = retouch.enabled ? "" : "off"
        const pointCount = retouch.brushPoints ? retouch.brushPoints.length : 0
        const pointText = pointCount > 0 ? pointCount.toString() + " pts" : "no strokes"
        const extras = []
        extras.push(status)
        if (enabled.length > 0)
            extras.push(enabled)
        extras.push(pointText)
        if ((retouch.inpaintCandidateCount || 0) > 0)
            extras.push((retouch.inpaintCandidateCount || 0).toString() + " candidates")
        if (retouch.inpaintFallback || false)
            extras.push("fallback")
        return root.retouchModeLabel(retouch.retouchMode) + " · " + extras.join(" · ")
    }

    function maskKindLabel(kindName) {
        const kind = String(kindName || "").trim().toLowerCase()
        if (kind === "linear")
            return "Linear source"
        if (kind === "radial")
            return "Radial source"
        if (kind === "brush")
            return "Brush source"
        if (kind === "object")
            return "Object source"
        if (kind === "subject")
            return "Subject source"
        if (kind === "sky")
            return "Sky source"
        if (kind === "background")
            return "Background source"
        if (kind === "people")
            return "People source"
        if (kind === "polygon")
            return "Polygon source"
        if (kind === "lasso")
            return "Lasso source"
        if (kind === "luminance")
            return "Luminance range"
        if (kind === "color")
            return "Color range"
        if (kind === "depth")
            return "Depth range"
        if (kind.length === 0)
            return "Mask source"
        return kind.charAt(0).toUpperCase() + kind.slice(1) + " source"
    }

    function maskOperationLabel(operationName) {
        const operation = String(operationName || "").trim().toLowerCase()
        if (operation === "subtract")
            return "Subtract"
        if (operation === "intersect")
            return "Intersect"
        if (operation === "exclude")
            return "Exclude"
        if (operation === "replace")
            return "Replace"
        if (operation === "union")
            return "Union"
        return "Add"
    }

    function maskStateSummary(mask) {
        if (!mask)
            return "No active mask"
        const parts = []
        parts.push(root.maskKindLabel(mask.kind))
        parts.push(root.maskOperationLabel(mask.operation))
        parts.push((mask.invert || mask.selectionInvert) ? "Inverted" : "Normal")
        if (!(mask.enabled !== false))
            parts.push("Off")
        if (mask.solo)
            parts.push("Solo")
        if (mask.locked)
            parts.push("Locked")
        return parts.join(" · ")
    }

    function activeMaskStatusText() {
        if (!editorPreview.hasActiveMask)
            return "Choose a tool to begin."
        if (editorPreview.activeMaskToolRole === "retouch")
            return root.retouchAuditText(root.activeMaskData || ({}))
        return root.maskStateSummary(root.activeMaskData)
    }

    function refineStrengths() {
        const featherDefault = editRoot.activeMaskResetFeather()
        return [
            {
                "key": "expand",
                "label": editorPreview.activeMaskSelectionExpand < 0 ? "Contract" : "Expand",
                "value": Math.min(1, Math.abs(editorPreview.activeMaskSelectionExpand) / 100),
                "accent": editorPreview.activeMaskSelectionExpand < 0 ? "#d98962" : root.activeMaskAccent
            },
            {
                "key": "feather",
                "label": "Feather",
                "value": Math.min(1, Math.abs(editorPreview.activeMaskFeather - featherDefault) / 0.95),
                "accent": "#8fb7d8"
            },
            {
                "key": "smooth",
                "label": "Smooth",
                "value": Math.min(1, editorPreview.activeMaskSelectionSmooth / 100),
                "accent": "#72cbb2"
            },
            {
                "key": "fine",
                "label": "Fine",
                "value": Math.min(1, editorPreview.activeMaskSelectionFineEdge / 100),
                "accent": "#c994d8"
            },
            {
                "key": "edge",
                "label": "Edge",
                "value": Math.min(1, editorPreview.activeMaskSelectionEdgeContrast / 100),
                "accent": "#f2c56b"
            },
            {
                "key": "spill",
                "label": "Spill",
                "value": Math.min(1, editorPreview.activeMaskSelectionDecontaminate / 100),
                "accent": "#d98962"
            }
        ]
    }

    function activeRefineEffectCount() {
        const strengths = root.refineStrengths()
        let count = editorPreview.activeMaskSelectionInverted ? 1 : 0
        for (let index = 0; index < strengths.length; ++index) {
            if (strengths[index].value > 0.01)
                ++count
        }
        return count
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
                            objectName: "activeMaskModeChip"
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
                    objectName: "activeMaskTitleLabel"
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
                    objectName: "activeMaskStatusLabel"
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
                                       : root.activeMaskStatusText()
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
                objectName: "maskPreviewOverlayButton"
                text: "Overlay"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "overlay"
                enabled: editorPreview.hasAsset
                tooltipText: "Show a colored mask overlay on the image."
                onClicked: editorPreview.maskPreviewMode = "overlay"
            }

            ActionButton {
                objectName: "maskPreviewMatteButton"
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
                objectName: "maskPreviewLightButton"
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
                objectName: "maskPreviewDarkButton"
                text: "On Dark"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "white-on-black"
                enabled: editorPreview.hasAsset
                tooltipText: "Show the mask against a dark matte."
                onClicked: editorPreview.maskPreviewMode = "white-on-black"
            }

            ActionButton {
                objectName: "maskPreviewOutlineButton"
                text: "Outline"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "marching-ants"
                enabled: editorPreview.hasAsset
                tooltipText: "Show the animated selection boundary only."
                onClicked: editorPreview.maskPreviewMode = "marching-ants"
            }

            ActionButton {
                objectName: "maskPreviewEdgeButton"
                text: "Edge"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "edge"
                enabled: editorPreview.hasAsset
                tooltipText: "Show only semi-transparent mask edges."
                onClicked: editorPreview.maskPreviewMode = "edge"
            }

            ActionButton {
                objectName: "maskPreviewAffectedButton"
                text: "Affected"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "affected"
                enabled: editorPreview.hasAsset
                tooltipText: "Highlight pixels affected by the active mask."
                onClicked: editorPreview.maskPreviewMode = "affected"
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
                      : (editorPreview.activeMaskRetouchMode === "heal"
                         ? "Apply Heal"
                         : (editorPreview.activeMaskRetouchMode === "patch"
                            ? "Apply Patch"
                            : (editorPreview.activeMaskRetouchMode === "dust"
                               ? "Apply Dust"
                               : (editorPreview.activeMaskRetouchMode === "red-eye"
                                  ? "Apply Red Eye"
                                  : "Apply Clone"))))
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

            TextField {
                width: parent.width
                text: root.maskSearchQuery
                placeholderText: "Search masks"
                selectByMouse: true
                font.family: theme.fontUi
                font.pixelSize: 12
                color: "#f2f4f6"
                placeholderTextColor: "#6f7882"
                onTextChanged: root.maskSearchQuery = text
                background: Rectangle {
                    radius: 8
                    color: "#11161a"
                    border.width: 1
                    border.color: root.maskSearchQuery.trim().length > 0 ? Qt.alpha(root.activeMaskAccent, 0.50) : "#2a3138"
                }
            }

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
                                objectName: "maskSetThumbnail"
                                Layout.preferredWidth: 44
                                Layout.preferredHeight: 30
                                radius: 5
                                color: "#090c0f"
                                border.width: 1
                                border.color: Qt.alpha(setColor, 0.44)
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    source: modelData.thumbnailSource || ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: false
                                    visible: source.toString().length > 0
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    anchors.leftMargin: 4
                                    anchors.bottomMargin: 4
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: setColor
                                    border.width: 1
                                    border.color: "#0c1014"
                                }
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
                            model: root.maskTreeSources(modelData.sources).filter(root.maskMatchesSearch)

                            delegate: Rectangle {
                                required property var modelData

                                x: Math.min(40, modelData.depth * 14)
                                width: parent.width - x
                                radius: 8
                                color: modelData.flatIndex === editorPreview.activeMaskIndex
                                       ? "#21272d"
                                       : "#14181c"
                                border.width: 1
                                border.color: modelData.flatIndex === editorPreview.activeMaskIndex
                                              ? editRoot.maskAccent(modelData)
                                              : "#242b31"
                                implicitHeight: 52
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
                                        objectName: "maskSourceThumbnail"
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 24
                                        radius: 5
                                        color: "#090c0f"
                                        border.width: 1
                                        border.color: Qt.alpha(editRoot.maskAccent(modelData), 0.42)
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            source: modelData.thumbnailSource || ""
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: false
                                            visible: source.toString().length > 0
                                        }

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.bottom: parent.bottom
                                            anchors.leftMargin: 3
                                            anchors.bottomMargin: 3
                                            width: 7
                                            height: 7
                                            radius: 4
                                            color: editRoot.maskAccent(modelData)
                                            border.width: 1
                                            border.color: "#0c1014"
                                        }
                                    }

                                    Rectangle {
                                        visible: modelData.depth > 0
                                        Layout.preferredWidth: 12
                                        Layout.preferredHeight: 1
                                        color: "#46505a"
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
                                            objectName: "maskSourceSummaryLabel"
                                            Layout.fillWidth: true
                                            text: root.maskStateSummary(modelData)
                                            color: "#8f98a2"
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "maskSourceDependencyLabel"
                                            Layout.fillWidth: true
                                            visible: String(modelData.dependencyLabel || "").length > 0
                                            text: String(modelData.dependencyLabel || "")
                                            color: editRoot.maskAccent(modelData)
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                    }

                                    ActionButton {
                                        text: modelData.enabled ? "On" : "Off"
                                        compact: true
                                        active: modelData.enabled
                                        accent: modelData.enabled ? editRoot.maskAccent(modelData) : "#d98962"
                                        enabled: !modelData.locked
                                        tooltipText: modelData.enabled ? "Disable this source" : "Enable this source"
                                        onClicked: {
                                            editorPreview.activeMaskIndex = modelData.flatIndex
                                            editorPreview.activeMaskEnabled = !modelData.enabled
                                        }
                                    }

                                    ActionButton {
                                        text: "Solo"
                                        compact: true
                                        active: modelData.solo
                                        enabled: !modelData.locked
                                        tooltipText: modelData.solo ? "Clear solo" : "Solo this mask"
                                        onClicked: {
                                            editorPreview.activeMaskIndex = modelData.flatIndex
                                            editorPreview.activeMaskSolo = !modelData.solo
                                        }
                                    }

                                    ActionButton {
                                        text: modelData.locked ? "Locked" : "Lock"
                                        compact: true
                                        active: modelData.locked
                                        accent: modelData.locked ? "#d98962" : editRoot.maskAccent(modelData)
                                        tooltipText: modelData.locked ? "Unlock this mask" : "Lock this mask"
                                        onClicked: {
                                            editorPreview.activeMaskIndex = modelData.flatIndex
                                            editorPreview.activeMaskLocked = !modelData.locked
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 6
                visible: editorPreview.reusableMaskOutputs().length > 0

                Label {
                    width: parent.width
                    text: "Reusable Outputs"
                    color: "#8f98a2"
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }

                Flow {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: editorPreview.reusableMaskOutputs()

                        delegate: FilterChip {
                            required property var modelData

                            text: modelData.name + " -> " + modelData.id
                            active: true
                            enabled: false
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
        subtitle: "Heal, clone, skin, red-eye, dust, and remove edits stay separate from selection masks."
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
                    implicitHeight: retouchRowLayout.implicitHeight + 20
                    opacity: modelData.enabled ? 1.0 : 0.55

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: editorPreview.activeMaskIndex = modelData.flatIndex
                    }

                    ColumnLayout {
                        id: retouchRowLayout
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
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
                                          ? root.retouchAuditText(modelData)
                                          : "retouch"
                                    color: "#8f98a2"
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            spacing: 4

                            ActionButton {
                                objectName: "retouchListSelectButton"
                                text: "Select"
                                compact: true
                                active: modelData.flatIndex === editorPreview.activeMaskIndex
                                accent: editRoot.maskAccent(modelData)
                                onClicked: editorPreview.activeMaskIndex = modelData.flatIndex
                            }

                            ActionButton {
                                objectName: "retouchListApplyButton"
                                text: "Apply"
                                compact: true
                                visible: !modelData.committed
                                accent: editRoot.maskAccent(modelData)
                                onClicked: {
                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                    editorPreview.applyActiveRetouch()
                                }
                            }

                            ActionButton {
                                objectName: "retouchListCancelButton"
                                text: "Cancel"
                                compact: true
                                visible: !modelData.committed
                                accent: "#d98962"
                                onClicked: {
                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                    editorPreview.cancelActiveRetouch()
                                }
                            }

                            ActionButton {
                                objectName: "retouchListRenameButton"
                                text: "Rename"
                                compact: true
                                onClicked: {
                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                    activeMaskNameField.forceActiveFocus()
                                    activeMaskNameField.selectAll()
                                }
                            }

                            ActionButton {
                                objectName: "retouchListEnableButton"
                                text: modelData.enabled ? "Off" : "On"
                                compact: true
                                accent: modelData.enabled ? "#d98962" : editRoot.maskAccent(modelData)
                                onClicked: {
                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                    editorPreview.activeMaskEnabled = !modelData.enabled
                                }
                            }

                            ActionButton {
                                objectName: "retouchListMoveUpButton"
                                text: "Up"
                                compact: true
                                enabled: modelData.flatIndex > 0
                                onClicked: {
                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                    editorPreview.moveActiveMask(-1)
                                }
                            }

                            ActionButton {
                                objectName: "retouchListMoveDownButton"
                                text: "Down"
                                compact: true
                                enabled: modelData.flatIndex < editorPreview.maskCount - 1
                                onClicked: {
                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                    editorPreview.moveActiveMask(1)
                                }
                            }

                            ActionButton {
                                objectName: "retouchListDeleteButton"
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

            RowLayout {
                width: parent.width
                spacing: 6
                visible: editorPreview.retouchCount > 0

                ActionButton {
                    Layout.fillWidth: true
                    text: "Before"
                    compact: true
                    accent: "#8fb7d8"
                    tone: active ? "primary" : "secondary"
                    active: editorPreview.retouchBeforeAfterEnabled
                    onClicked: editorPreview.retouchBeforeAfterEnabled = true
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: "After"
                    compact: true
                    accent: "#8fb7d8"
                    tone: active ? "primary" : "secondary"
                    active: !editorPreview.retouchBeforeAfterEnabled
                    onClicked: editorPreview.retouchBeforeAfterEnabled = false
                }
            }
        }
    }

    MaskSection {
        objectName: "maskPropertiesSection"
        width: parent.width
        title: editorPreview.activeMaskToolRole === "retouch" ? "Retouch Properties" : "Mask Properties"
        visible: editorPreview.hasActiveMask

        ToneTextField {
            id: activeMaskNameField
            width: parent.width
            text: editorPreview.activeMaskName
            placeholderText: editorPreview.activeMaskToolRole === "retouch" ? "Retouch name" : "Mask set name"
            enabled: !editorPreview.activeMaskLocked
            onEditingFinished: editorPreview.activeMaskName = text
        }

        ToneTextField {
            width: parent.width
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch")
            text: editorPreview.inpaintPrompt
            placeholderText: "Optional inpaint prompt"
            enabled: !editorPreview.activeMaskCommitted
            onEditingFinished: editorPreview.inpaintPrompt = text
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch")

            ToneTextField {
                Layout.fillWidth: true
                text: editorPreview.inpaintSeed.toString()
                placeholderText: "Seed"
                enabled: !editorPreview.activeMaskCommitted
                onEditingFinished: editorPreview.inpaintSeed = Math.max(1, parseInt(text) || 1)
            }

            ActionButton {
                Layout.preferredWidth: 112
                text: "Regenerate"
                enabled: !editorPreview.activeMaskCommitted
                onClicked: editorPreview.regenerateInpaintSeed()
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch")

            ActionButton {
                Layout.fillWidth: true
                text: "Preview"
                active: editorPreview.inpaintQualityMode === "preview"
                enabled: !editorPreview.activeMaskCommitted
                onClicked: editorPreview.inpaintQualityMode = "preview"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Final"
                active: editorPreview.inpaintQualityMode === "final"
                enabled: !editorPreview.activeMaskCommitted
                onClicked: editorPreview.inpaintQualityMode = "final"
            }
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch")
            label: "INPAINT BLEND"
            from: 0
            to: 100
            stepSize: 1
            defaultValue: 100
            value: Math.round(editorPreview.inpaintBlendAmount * 100)
            valueText: Math.round(editorPreview.inpaintBlendAmount * 100) + "%"
            accent: root.activeMaskAccent
            onValueDragged: function(value) { editorPreview.inpaintBlendAmount = value / 100 }
            onResetRequested: editorPreview.inpaintBlendAmount = 1.0
        }

        ActionButton {
            width: parent.width
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch") &&
                     editorPreview.activeInpaintPatchBounds.width > 0
            text: editorPreview.inpaintPatchDiagnosticsEnabled ? "Hide Patch Bounds" : "Show Patch Bounds"
            active: editorPreview.inpaintPatchDiagnosticsEnabled
            accent: "#f2c56b"
            onClicked: editorPreview.inpaintPatchDiagnosticsEnabled = !editorPreview.inpaintPatchDiagnosticsEnabled
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole !== "retouch" &&
                     editorPreview.activeMaskKind !== "brush"

            ActionButton {
                objectName: "activeMaskOperationAddButton"
                Layout.fillWidth: true
                text: "Add"
                tooltipText: "Add this source to the mask set."
                active: editorPreview.activeMaskOperation === "add"
                onClicked: editorPreview.activeMaskOperation = "add"
            }

            ActionButton {
                objectName: "activeMaskOperationSubtractButton"
                Layout.fillWidth: true
                text: "Subtract"
                tooltipText: "Remove this source from the mask set."
                active: editorPreview.activeMaskOperation === "subtract"
                onClicked: editorPreview.activeMaskOperation = "subtract"
            }

            ActionButton {
                objectName: "activeMaskOperationIntersectButton"
                Layout.fillWidth: true
                text: "Intersect"
                tooltipText: "Keep only the overlap with the mask set."
                active: editorPreview.activeMaskOperation === "intersect"
                onClicked: editorPreview.activeMaskOperation = "intersect"
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole !== "retouch" &&
                     editorPreview.activeMaskKind !== "brush"

            ActionButton {
                objectName: "activeMaskOperationExcludeButton"
                Layout.fillWidth: true
                text: "Exclude"
                tooltipText: "Exclude overlapping pixels from this source."
                active: editorPreview.activeMaskOperation === "exclude"
                onClicked: editorPreview.activeMaskOperation = "exclude"
            }

            ActionButton {
                objectName: "activeMaskOperationReplaceButton"
                Layout.fillWidth: true
                text: "Replace"
                tooltipText: "Use this source as the mask set result."
                active: editorPreview.activeMaskOperation === "replace"
                onClicked: editorPreview.activeMaskOperation = "replace"
            }

            ActionButton {
                objectName: "activeMaskOperationUnionButton"
                Layout.fillWidth: true
                text: "Union"
                tooltipText: "Union this source with the current mask set."
                active: editorPreview.activeMaskOperation === "union"
                onClicked: editorPreview.activeMaskOperation = "union"
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskEnabled ? "Enabled" : "Disabled"
                active: editorPreview.activeMaskEnabled
                enabled: !editorPreview.activeMaskLocked
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

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskSolo ? "Soloed" : "Solo"
                active: editorPreview.activeMaskSolo
                enabled: !editorPreview.activeMaskLocked
                onClicked: editorPreview.activeMaskSolo = !editorPreview.activeMaskSolo
            }

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskLocked ? "Locked" : "Unlocked"
                active: editorPreview.activeMaskLocked
                accent: editorPreview.activeMaskLocked ? "#d98962" : root.activeMaskAccent
                onClicked: editorPreview.activeMaskLocked = !editorPreview.activeMaskLocked
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                objectName: "activeMaskInvertButton"
                Layout.fillWidth: true
                text: editorPreview.activeMaskInverted ? "Inverted" : "Normal"
                active: editorPreview.activeMaskInverted
                tooltipText: editorPreview.activeMaskInverted ? "Return this mask source to normal." : "Invert this mask source."
                onClicked: editorPreview.activeMaskInverted = !editorPreview.activeMaskInverted
            }

            ComboBox {
                id: maskBlendModeCombo
                Layout.fillWidth: true
                model: ["normal", "multiply", "screen", "overlay", "lighten", "darken", "replace"]
                currentIndex: Math.max(0, model.indexOf(editorPreview.activeMaskBlendMode))
                enabled: editorPreview.hasActiveMask
                onActivated: editorPreview.activeMaskBlendMode = model[index]
            }
        }

        InspectorSlider {
            width: parent.width
            label: "OPACITY"
            from: 0
            to: 100
            stepSize: 1
            defaultValue: 100
            value: Math.round(editorPreview.activeMaskOpacity * 100)
            valueText: Math.round(editorPreview.activeMaskOpacity * 100) + "%"
            accent: root.activeMaskAccent
            onValueDragged: function(value) { editorPreview.activeMaskOpacity = value / 100 }
            onResetRequested: editorPreview.activeMaskOpacity = 1.0
        }

        InspectorSlider {
            width: parent.width
            label: "DENSITY"
            from: 0
            to: 100
            stepSize: 1
            defaultValue: 100
            value: Math.round(editorPreview.activeMaskDensity * 100)
            valueText: Math.round(editorPreview.activeMaskDensity * 100) + "%"
            accent: root.activeMaskAccent
            onValueDragged: function(value) { editorPreview.activeMaskDensity = value / 100 }
            onResetRequested: editorPreview.activeMaskDensity = 1.0
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
        objectName: "maskSelectionSection"
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
                    editorPreview.activeMaskSelectionSmooth = 0
                    editorPreview.activeMaskSelectionFineEdge = 0
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
        objectName: "maskEdgeSection"
        width: parent.width
        title: "Edge"
        subtitle: "Trim or recover boundaries, then tighten soft edges or clean color spill."
        visible: editRoot.activeMaskSupportsSelectionRefine() &&
                 editorPreview.activeMaskToolRole !== "retouch"
        expandable: true
        expanded: editRoot.isAiMaskKind(editorPreview.activeMaskKind)

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Edge Add"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    if (editRoot.isAiMaskKind(editorPreview.activeMaskKind))
                        editorPreview.startAIMaskRefineBrush(false)
                    else
                        editorPreview.startSelectionEdgeBrush(false)
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Edge Erase"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    if (editRoot.isAiMaskKind(editorPreview.activeMaskKind))
                        editorPreview.startAIMaskRefineBrush(true)
                    else
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

        Rectangle {
            objectName: "maskRefineEffectMeter"
            property real expandStrength: Math.min(1, Math.abs(editorPreview.activeMaskSelectionExpand) / 100)
            property real featherStrength: Math.min(1, Math.abs(editorPreview.activeMaskFeather - editRoot.activeMaskResetFeather()) / 0.95)
            property real smoothStrength: Math.min(1, editorPreview.activeMaskSelectionSmooth / 100)
            property real fineEdgeStrength: Math.min(1, editorPreview.activeMaskSelectionFineEdge / 100)
            property real edgeContrastStrength: Math.min(1, editorPreview.activeMaskSelectionEdgeContrast / 100)
            property real decontaminateStrength: Math.min(1, editorPreview.activeMaskSelectionDecontaminate / 100)
            property int activeEffectCount: root.activeRefineEffectCount()

            width: parent.width
            radius: 8
            color: "#11161a"
            border.width: 1
            border.color: activeEffectCount > 0 ? Qt.alpha(root.activeMaskAccent, 0.46) : "#252c32"
            implicitHeight: 72

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: activeEffectCount > 0 ? activeEffectCount + " refine effects active" : "Neutral refine"
                        color: activeEffectCount > 0 ? "#f2f4f6" : "#8f98a2"
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: editorPreview.activeMaskSelectionInverted ? "Inverted" : "Normal"
                        color: editorPreview.activeMaskSelectionInverted ? "#d98962" : "#8f98a2"
                        font.family: theme.fontUi
                        font.pixelSize: 10
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    Repeater {
                        model: root.refineStrengths()

                        delegate: ColumnLayout {
                            required property var modelData

                            Layout.fillWidth: true
                            spacing: 3

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 8
                                radius: 4
                                color: "#0a0d10"
                                border.width: 1
                                border.color: "#20272d"
                                clip: true

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: Math.max(2, parent.width * modelData.value)
                                    radius: 4
                                    color: Qt.alpha(modelData.accent, modelData.value > 0.01 ? 0.88 : 0.22)
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.label
                                horizontalAlignment: Text.AlignHCenter
                                color: modelData.value > 0.01 ? "#cfd5db" : "#65707a"
                                font.family: theme.fontUi
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
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
            from: (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.0 : 0.05
            to: (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.2 : 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskFeather
            onValueDragged: function(value) { editorPreview.activeMaskFeather = value }
            onResetRequested: editorPreview.activeMaskFeather = editRoot.activeMaskResetFeather()
        }

        InspectorSlider {
            width: parent.width
            label: "Smooth"
            valueText: Math.round(editorPreview.activeMaskSelectionSmooth).toString()
            from: 0
            to: 100
            value: editorPreview.activeMaskSelectionSmooth
            onValueDragged: function(value) { editorPreview.activeMaskSelectionSmooth = value }
            onResetRequested: editorPreview.activeMaskSelectionSmooth = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Fine Edge"
            valueText: Math.round(editorPreview.activeMaskSelectionFineEdge).toString()
            from: 0
            to: 100
            value: editorPreview.activeMaskSelectionFineEdge
            onValueDragged: function(value) { editorPreview.activeMaskSelectionFineEdge = value }
            onResetRequested: editorPreview.activeMaskSelectionFineEdge = 0
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
        objectName: "maskAdjustSection"
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
        objectName: "maskShapeSection"
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
                     editorPreview.activeMaskRetouchMode === "patch" ||
                     editorPreview.activeMaskRetouchMode === "clone"
            text: editorPreview.activeMaskRetouchMode === "remove"
                  ? "Paint over unwanted content to remove it from the image."
                  : editorPreview.activeMaskRetouchMode === "heal"
                    ? "Heal uses a softer fill pass for blemishes and seams."
                    : editorPreview.activeMaskRetouchMode === "patch"
                      ? "Patch fills a painted repair area with an auto-masked retouch pass."
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
            visible: editorPreview.activeMaskKind === "brush"

            Repeater {
                model: editorPreview.brushPresetNames

                ActionButton {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    compact: true
                    active: editorPreview.activeMaskBrushPreset === modelData
                    accent: root.activeMaskAccent
                    onClicked: editorPreview.applyActiveMaskBrushPreset(modelData)
                }
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
            label: "Brush Hardness"
            valueText: Math.round(editorPreview.activeMaskBrushHardness * 100).toString() + "%"
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushHardness
            onValueDragged: function(value) { editorPreview.activeMaskBrushHardness = value }
            onResetRequested: editorPreview.activeMaskBrushHardness = 0.25
        }

        InspectorSlider {
            width: parent.width
            label: "Brush Spacing"
            valueText: Math.round(editorPreview.activeMaskBrushSpacing * 100).toString() + "%"
            from: 0.05
            to: 2.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushSpacing
            onValueDragged: function(value) { editorPreview.activeMaskBrushSpacing = value }
            onResetRequested: editorPreview.activeMaskBrushSpacing = 0.75
        }

        InspectorSlider {
            width: parent.width
            label: "Stroke Smoothing"
            valueText: Math.round(editorPreview.activeMaskBrushSmoothing * 100).toString() + "%"
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushSmoothing
            onValueDragged: function(value) { editorPreview.activeMaskBrushSmoothing = value }
            onResetRequested: editorPreview.activeMaskBrushSmoothing = 0.0
        }

        ActionButton {
            width: parent.width
            text: editorPreview.activeMaskBrushBuildup ? "Flow Buildup" : "Single Pass Flow"
            active: editorPreview.activeMaskBrushBuildup
            compact: true
            onClicked: editorPreview.activeMaskBrushBuildup = !editorPreview.activeMaskBrushBuildup
        }

        ActionButton {
            width: parent.width
            text: editorPreview.activeMaskBrushEraseMode ? "Erase Mode" : "Paint Mode"
            active: editorPreview.activeMaskBrushEraseMode
            accent: editorPreview.activeMaskBrushEraseMode ? "#d98962" : root.activeMaskAccent
            compact: true
            onClicked: editorPreview.activeMaskBrushEraseMode = !editorPreview.activeMaskBrushEraseMode
        }

        ActionButton {
            width: parent.width
            text: editorPreview.activeMaskBrushAutoMask ? "Auto Mask" : "Freehand Mask"
            active: editorPreview.activeMaskBrushAutoMask
            compact: true
            onClicked: editorPreview.activeMaskBrushAutoMask = !editorPreview.activeMaskBrushAutoMask
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskBrushAutoMask
            label: "Auto-Mask Range"
            valueText: Math.round(editorPreview.activeMaskBrushAutoMaskTolerance * 100).toString() + "%"
            from: 0.02
            to: 0.75
            stepSize: 0.01
            value: editorPreview.activeMaskBrushAutoMaskTolerance
            onValueDragged: function(value) { editorPreview.activeMaskBrushAutoMaskTolerance = value }
            onResetRequested: editorPreview.activeMaskBrushAutoMaskTolerance = 0.18
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
        title: editorPreview.activeMaskKind === "lasso" ? "Lasso" : "Polygon"
        visible: editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso"
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
        visible: editRoot.isAiMaskKind(editorPreview.activeMaskKind)
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
                  : "AI " + editRoot.aiMaskLabel(editorPreview.activeMaskKind).toLowerCase() + " masks stay editable inside the current mask set."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        width: parent.width
        title: "Range Refine"
        subtitle: "Limit the active selection by tone, color, depth, or edge confidence."
        visible: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        expandable: true
        expanded: false

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Reset"
                compact: true
                onClicked: {
                    editorPreview.activeMaskRefineLuminanceMin = 0
                    editorPreview.activeMaskRefineLuminanceMax = 1
                    editorPreview.activeMaskRefineHueCenter = 0
                    editorPreview.activeMaskRefineHueRange = 0.5
                    editorPreview.activeMaskRefineSaturationMin = 0
                    editorPreview.activeMaskRefineSaturationMax = 1
                    editorPreview.activeMaskRefineDepthMin = 0
                    editorPreview.activeMaskRefineDepthMax = 1
                    editorPreview.activeMaskRefineEdgeConfidenceMin = 0
                    editorPreview.activeMaskRefineRangeFeather = 0.05
                }
            }

            Label {
                Layout.fillWidth: true
                text: "Source-aware"
                horizontalAlignment: Text.AlignRight
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Luma Min"
            valueText: editorPreview.activeMaskRefineLuminanceMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineLuminanceMin
            onValueDragged: function(value) { editorPreview.activeMaskRefineLuminanceMin = value }
            onResetRequested: editorPreview.activeMaskRefineLuminanceMin = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Luma Max"
            valueText: editorPreview.activeMaskRefineLuminanceMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineLuminanceMax
            onValueDragged: function(value) { editorPreview.activeMaskRefineLuminanceMax = value }
            onResetRequested: editorPreview.activeMaskRefineLuminanceMax = 1
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Center"
            valueText: Math.round(editorPreview.activeMaskRefineHueCenter * 360).toString() + "°"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineHueCenter
            onValueDragged: function(value) { editorPreview.activeMaskRefineHueCenter = value }
            onResetRequested: editorPreview.activeMaskRefineHueCenter = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Width"
            valueText: Math.round(editorPreview.activeMaskRefineHueRange * 360).toString() + "°"
            from: 0.02
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskRefineHueRange
            onValueDragged: function(value) { editorPreview.activeMaskRefineHueRange = value }
            onResetRequested: editorPreview.activeMaskRefineHueRange = 0.5
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Min"
            valueText: editorPreview.activeMaskRefineSaturationMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineSaturationMin
            onValueDragged: function(value) { editorPreview.activeMaskRefineSaturationMin = value }
            onResetRequested: editorPreview.activeMaskRefineSaturationMin = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Max"
            valueText: editorPreview.activeMaskRefineSaturationMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineSaturationMax
            onValueDragged: function(value) { editorPreview.activeMaskRefineSaturationMax = value }
            onResetRequested: editorPreview.activeMaskRefineSaturationMax = 1
        }

        InspectorSlider {
            width: parent.width
            label: "Depth Near"
            valueText: editorPreview.activeMaskRefineDepthMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineDepthMin
            onValueDragged: function(value) { editorPreview.activeMaskRefineDepthMin = value }
            onResetRequested: editorPreview.activeMaskRefineDepthMin = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Depth Far"
            valueText: editorPreview.activeMaskRefineDepthMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineDepthMax
            onValueDragged: function(value) { editorPreview.activeMaskRefineDepthMax = value }
            onResetRequested: editorPreview.activeMaskRefineDepthMax = 1
        }

        InspectorSlider {
            width: parent.width
            label: "Edge Confidence"
            valueText: editorPreview.activeMaskRefineEdgeConfidenceMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineEdgeConfidenceMin
            onValueDragged: function(value) { editorPreview.activeMaskRefineEdgeConfidenceMin = value }
            onResetRequested: editorPreview.activeMaskRefineEdgeConfidenceMin = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskRefineRangeFeather.toFixed(2)
            from: 0
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskRefineRangeFeather
            onValueDragged: function(value) { editorPreview.activeMaskRefineRangeFeather = value }
            onResetRequested: editorPreview.activeMaskRefineRangeFeather = 0.05
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

    MaskSection {
        width: parent.width
        title: "Depth Range"
        visible: editorPreview.activeMaskKind === "depth"
        expandable: true
        expanded: true

        InspectorSlider {
            width: parent.width
            label: "Near"
            valueText: editorPreview.activeMaskDepthMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskDepthMin
            onValueDragged: function(value) { editorPreview.activeMaskDepthMin = value }
            onResetRequested: editorPreview.activeMaskDepthMin = 0.2
        }

        InspectorSlider {
            width: parent.width
            label: "Far"
            valueText: editorPreview.activeMaskDepthMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskDepthMax
            onValueDragged: function(value) { editorPreview.activeMaskDepthMax = value }
            onResetRequested: editorPreview.activeMaskDepthMax = 0.8
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
}
