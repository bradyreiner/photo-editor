pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property var agentEditController: null
    property var catalogModel: null
    property string initialScopeMode: "current-photo"
    property string scopeMode: initialScopeMode.length > 0 ? initialScopeMode : "current-photo"
    property var thinkingLines: []
    property var proofImages: []
    property int currentPage: 0
    property string styleGalleryFilter: "all"
    property string styleGalleryQuery: ""
    readonly property real availableDialogHeight: parent ? Math.max(430, parent.height - 160) : 600
    readonly property real targetDialogHeight: currentPage === 0 ? 640
                                            : currentPage === 1 ? 540
                                            : 600

    signal scopeModeRequested(string scopeMode)
    signal runRequested()

    Theme { id: theme }

    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0
    width: Math.min(720, Math.max(410, parent ? parent.width - 192 : 660))
    height: Math.min(availableDialogHeight, targetDialogHeight)
    implicitHeight: height

    Behavior on height {
        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: theme.durationSlow; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: theme.durationSlow; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: theme.durationMedium; easing.type: Easing.InOutSine }
            NumberAnimation { property: "scale"; from: 1; to: 0.985; duration: theme.durationMedium; easing.type: Easing.InOutSine }
        }
    }

    background: Rectangle {
        radius: 28
        color: "#202226"
        border.width: 1
        border.color: theme.alpha(theme.polar, 0.16)

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 27
            color: "transparent"
            border.width: 1
            border.color: theme.alpha(theme.ink950, 0.42)
        }
    }

    onOpened: {
        root.scopeMode = root.bestScope(root.initialScopeMode)
        root.scopeModeRequested(root.scopeMode)
        root.setPage(root.agentRunning() ? 2 : 0)
    }

    Connections {
        target: root.agentEditController
        function onRunningChanged() {
            if (root.agentRunning())
                root.setPage(2)
        }
    }

    function hasController() {
        return root.agentEditController !== null && root.agentEditController !== undefined
    }

    function hasCatalog() {
        return root.catalogModel !== null && root.catalogModel !== undefined
    }

    function agentRunning() {
        return root.hasController() && root.agentEditController.running
    }

    function workflowState() {
        return root.hasController()
               ? root.agentEditController.aiEditWorkflowState(root.scopeMode) || ({})
               : ({})
    }

    function setPage(page) {
        root.currentPage = Math.max(0, Math.min(2, page))
    }

    function nextPage() {
        root.setPage(root.currentPage + 1)
    }

    function previousPage() {
        root.setPage(root.currentPage - 1)
    }

    function selectedStyleId() {
        return root.hasController() ? String(root.agentEditController.selectedStyleId || "") : ""
    }

    function selectedStyleName() {
        const styles = root.availableStyles()
        const selected = root.selectedStyleId()
        for (let index = 0; index < styles.length; ++index) {
            const style = styles[index] || ({})
            if (String(style.id || "") === selected)
                return String(style.name || style.id || "Inspiration")
        }
        return styles.length > 0 ? String(styles[0].name || "Inspiration") : "No inspiration"
    }

    function availableStyles() {
        return root.hasController() ? root.agentEditController.availableStyles || [] : []
    }

    function scopeCount(scope) {
        if (!root.hasCatalog())
            return 0
        if (scope === "current-photo")
            return String(root.catalogModel.selectedAssetId || "").length > 0 ? 1 : 0
        if (scope === "project") {
            const project = String(root.catalogModel.projectFilter || "all")
            if (project !== "all" && root.catalogModel.assetCountForProject !== undefined)
                return root.catalogModel.assetCountForProject(project)
            return Number(root.catalogModel.assetCount || 0)
        }
        return Number(root.catalogModel.selectedAssetCount || 0)
    }

    function bestScope(preferred) {
        const candidate = String(preferred || "")
        if (candidate.length > 0 && root.scopeCount(candidate) > 0)
            return candidate
        if (root.scopeCount("current-photo") > 0)
            return "current-photo"
        if (root.scopeCount("project") > 0)
            return "project"
        return "current-photo"
    }

    function chooseScope(scope) {
        root.scopeMode = scope
        root.scopeModeRequested(scope)
    }

    function projectLabel() {
        if (!root.hasCatalog())
            return "Project"
        const project = String(root.catalogModel.projectFilter || "all")
        if (project === "all")
            return "Project Library"
        if (project === "unassigned")
            return "Unassigned Photos"
        return project
    }

    function targetTiles() {
        const currentCount = root.scopeCount("current-photo")
        const projectCount = root.scopeCount("project")
        return [
            {
                key: "current-photo",
                title: "Current Photo",
                detail: currentCount > 0 ? "Edit the photo in front of you." : "Pick a photo first.",
                count: currentCount,
                thumbnailSource: root.hasCatalog() ? String(root.catalogModel.selectedThumbnailSource || "") : "",
                icon: "adjust"
            },
            {
                key: "project",
                title: root.projectLabel(),
                detail: projectCount > 0 ? "Edit the active project together." : "Create or open a project first.",
                count: projectCount,
                thumbnailSource: "",
                icon: "folder"
            }
        ]
    }

    function selectedReferenceCount() {
        return root.hasCatalog() ? Number(root.catalogModel.selectedAssetCount || 0) : 0
    }

    function styleLearningAvailable() {
        const state = root.workflowState()
        const learning = state.styleLearning || ({})
        return Boolean(learning.available)
    }

    function selectedReferenceLabel() {
        const count = root.selectedReferenceCount()
        if (count <= 0)
            return "Select edited photos"
        return count + " selected reference" + (count === 1 ? "" : "s")
    }

    function assetSelected(asset) {
        if (!root.hasCatalog())
            return false
        const assetId = String((asset || ({})).assetId || "")
        return assetId.length > 0 && root.catalogModel.assetSelected(assetId)
    }

    function assetDisplayTitle(asset) {
        const item = asset || ({})
        const label = String(item.cameraLabel || item.filePath || item.assetId || "Photo")
        const slash = Math.max(label.lastIndexOf("/"), label.lastIndexOf("\\"))
        return slash >= 0 ? label.substring(slash + 1) : label
    }

    function assetMatchesStyleFilter(asset) {
        const item = asset || ({})
        if (root.styleGalleryFilter === "selected" && !root.assetSelected(item))
            return false
        if (root.styleGalleryFilter === "picked" && !Boolean(item.picked))
            return false
        if (root.styleGalleryFilter === "rated" && Number(item.rating || 0) <= 0)
            return false
        if (root.styleGalleryFilter === "project" &&
                String(item.projectSummary || "").length === 0 &&
                String(item.projectNames || "").length === 0) {
            return false
        }

        const query = root.styleGalleryQuery.trim().toLowerCase()
        if (query.length === 0)
            return true
        const haystack = [
            root.assetDisplayTitle(item),
            String(item.captureDate || ""),
            String(item.lensModel || ""),
            String(item.projectSummary || ""),
            String(item.filePath || "")
        ].join(" ").toLowerCase()
        return haystack.indexOf(query) >= 0
    }

    function styleGalleryAssets() {
        const assets = []
        if (!root.hasCatalog())
            return assets
        const count = Math.min(Number(root.catalogModel.assetCount || 0), 96)
        for (let index = 0; index < count; ++index) {
            const asset = root.catalogModel.assetAt(index) || ({})
            if (String(asset.assetId || "").length === 0 || !root.assetMatchesStyleFilter(asset))
                continue
            asset.visibleIndex = index
            assets.push(asset)
        }
        return assets
    }

    function chooseStyleGalleryAsset(asset) {
        if (!root.hasCatalog())
            return
        const index = Number((asset || ({})).visibleIndex)
        if (index >= 0 && index < Number(root.catalogModel.assetCount || 0)) {
            root.catalogModel.toggleSelectionIndex(index)
            root.scopeMode = "selected-assets"
            root.scopeModeRequested(root.scopeMode)
        }
    }

    function saveSelectedReferencesAsStyle() {
        root.learnSelectedInspiration()
    }

    function normalizeImageSource(source) {
        const text = String(source || "")
        if (text.length === 0)
            return ""
        if (text.indexOf("file:") === 0 ||
                text.indexOf("qrc:") === 0 ||
                text.indexOf("image:") === 0 ||
                text.indexOf("http:") === 0 ||
                text.indexOf("https:") === 0) {
            return text
        }
        if (text.charAt(0) === "/")
            return "file://" + text
        return text
    }

    function styleThumbnailSource(style) {
        const data = style || ({})
        return root.normalizeImageSource(data.thumbnailSource ||
                                         data.thumbnail ||
                                         data.previewSource ||
                                         data.preview ||
                                         data.imageSource ||
                                         "")
    }

    function styleAccent(style, slot) {
        const id = String((style || ({})).id || "")
        if (id.indexOf("portrait") >= 0)
            return slot === 0 ? "#C99A7A" : "#476B83"
        if (id.indexOf("wedding") >= 0 || id.indexOf("warm") >= 0)
            return slot === 0 ? "#D7A451" : "#7A4E74"
        if (id.indexOf("product") >= 0 || id.indexOf("commercial") >= 0)
            return slot === 0 ? "#DDE6EC" : "#2E8AC7"
        if (id.indexOf("landscape") >= 0)
            return slot === 0 ? "#589C67" : "#4E83B8"
        if (id.indexOf("black") >= 0 || id.indexOf("white") >= 0)
            return slot === 0 ? "#D9D9D6" : "#4A4B50"
        if (id.indexOf("street") >= 0)
            return slot === 0 ? "#C84E3F" : "#2B2E36"
        return slot === 0 ? "#7B8BA5" : "#C0A46B"
    }

    function inspirationTiles() {
        const tiles = []
        tiles.push({
            kind: "learn",
            styleId: "",
            title: "Selected Photos",
            detail: root.selectedReferenceCount() > 0
                    ? "Learn the look from your edited references."
                    : "Select edited reference photos in the library.",
            thumbnailSource: root.hasCatalog() ? root.normalizeImageSource(root.catalogModel.selectedThumbnailSource || "") : "",
            enabled: root.styleLearningAvailable(),
            accentA: "#D8DCE6",
            accentB: "#A87B5B"
        })

        const styles = root.availableStyles()
        for (let index = 0; index < styles.length; ++index) {
            const style = styles[index] || ({})
            tiles.push({
                kind: "style",
                styleId: String(style.id || ""),
                title: String(style.name || style.id || "Style"),
                detail: String(style.goal || style.description || style.summary || ""),
                thumbnailSource: root.styleThumbnailSource(style),
                enabled: String(style.id || "").length > 0,
                accentA: root.styleAccent(style, 0),
                accentB: root.styleAccent(style, 1)
            })
        }
        return tiles
    }

    function chooseInspiration(tile) {
        const item = tile || ({})
        if (!Boolean(item.enabled))
            return
        if (String(item.kind || "") === "learn") {
            root.learnSelectedInspiration()
            return
        }
        if (root.hasController() && String(item.styleId || "").length > 0)
            root.agentEditController.selectedStyleId = String(item.styleId)
    }

    function learnSelectedInspiration() {
        if (!root.hasController())
            return
        const count = root.selectedReferenceCount()
        const label = count > 1 ? "Learned Project Style" : "Learned Photo Style"
        const result = root.agentEditController.createLearnedStyleProfileFromSelection(
                    label,
                    "Edit selected photos consistently in my demonstrated style.",
                    String(root.agentEditController.userInstructions || ""))
        if (Boolean((result || ({})).ok))
            root.setPage(2)
    }

    function runAgent() {
        if (!root.hasController())
            return
        root.setPage(3)
        root.runRequested()
    }

    function runButtonLabel() {
        const state = root.workflowState()
        const action = state.primaryAction || ({})
        return String(action.label || "Run Agent")
    }

    function runButtonEnabled() {
        const state = root.workflowState()
        if (state.primaryActionEnabled !== undefined)
            return Boolean(state.primaryActionEnabled)
        const action = state.primaryAction || ({})
        return Boolean(action.enabled)
    }

    function statusLine() {
        const state = root.workflowState()
        if (root.hasController() && String(root.agentEditController.errorMessage || "").length > 0)
            return String(root.agentEditController.errorMessage)
        return String(state.statusLine || (root.agentRunning() ? "Working" : "Ready"))
    }

    function selectedTargetText() {
        const selectedCount = root.selectedReferenceCount()
        if (root.scopeMode === "selected-assets" && selectedCount > 0)
            return selectedCount + " selected photo" + (selectedCount === 1 ? "" : "s")
        if (root.scopeMode === "current-photo")
            return "Current Photo"
        return root.projectLabel()
    }

    function activityRows() {
        const status = root.statusLine().toLowerCase()
        const rows = [
            { label: "Looking at photos", active: status.indexOf("prepar") >= 0 || status.indexOf("creating") >= 0 },
            { label: "Reading histogram and color", active: status.indexOf("valid") >= 0 || status.indexOf("proof") >= 0 },
            { label: "Planning the edit", active: status.indexOf("planning") >= 0 || status.indexOf("sample") >= 0 },
            { label: "Applying adjustments", active: status.indexOf("applying") >= 0 || status.indexOf("commit") >= 0 },
            { label: "Reviewing result", active: status.indexOf("review") >= 0 || status.indexOf("ready") >= 0 }
        ]
        if (!root.agentRunning()) {
            for (let index = 0; index < rows.length; ++index)
                rows[index].active = index === rows.length - 1
        }
        return rows
    }

    function displayThinkingLines() {
        const lines = root.thinkingLines || []
        if (lines.length > 0)
            return lines
        const status = root.statusLine()
        return [status.length > 0 ? status : "Ready to run the photo agent."]
    }

    component StepDot: Rectangle {
        id: dot
        required property int modelData
        readonly property int stepIndex: modelData
        readonly property bool activeStep: root.currentPage === stepIndex
        readonly property bool pastStep: root.currentPage > stepIndex

        width: activeStep ? 34 : 9
        height: 9
        radius: 5
        color: activeStep || pastStep ? theme.textStrong : theme.alpha(theme.polar, 0.18)

        Behavior on width {
            NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
        }
    }

    component TargetTile: Rectangle {
        id: tile
        property string tileKey: ""
        property string title: ""
        property string detail: ""
        property string thumbnailSource: ""
        property string iconName: "adjust"
        property int count: 0
        readonly property bool selected: root.scopeMode === tileKey
        readonly property bool tileEnabled: count > 0

        Layout.fillWidth: true
        implicitHeight: 208
        radius: 24
        color: selected ? "#2C3037" : "#26292F"
        border.width: 1
        border.color: selected ? "#D7DAE0" : theme.alpha(theme.polar, 0.10)
        opacity: tileEnabled ? 1 : 0.52
        clip: true

        Behavior on color {
            ColorAnimation { duration: theme.durationMedium }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 108
                radius: 18
                color: theme.alpha(theme.ink950, 0.36)
                clip: true

                Image {
                    anchors.fill: parent
                    visible: tile.thumbnailSource.length > 0
                    source: root.normalizeImageSource(tile.thumbnailSource)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }

                Rectangle {
                    anchors.fill: parent
                    visible: tile.thumbnailSource.length === 0
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#59616C" }
                        GradientStop { position: 1.0; color: "#262D36" }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    color: theme.alpha(theme.ink950, 0.12)
                }

                AppIcon {
                    anchors.centerIn: parent
                    width: 30
                    height: 30
                    visible: tile.thumbnailSource.length === 0
                    name: tile.iconName
                    color: theme.alpha(theme.textStrong, 0.82)
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    width: 26
                    height: 26
                    radius: 13
                    visible: tile.selected
                    color: "#F4F5F6"

                    AppIcon {
                        anchors.centerIn: parent
                        width: 14
                        height: 14
                        name: "check"
                        color: "#181A1F"
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: tile.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 20
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: tile.count + " photo" + (tile.count === 1 ? "" : "s")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 1
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: tile.detail
                color: theme.alpha(theme.textMuted, 0.86)
                font.family: theme.fontUi
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }

        HoverHandler { cursorShape: tile.tileEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor }
        TapHandler {
            enabled: tile.tileEnabled
            onTapped: root.chooseScope(tile.tileKey)
        }
    }

    component StyleFilterChip: Rectangle {
        id: chip
        property string filterKey: "all"
        property string label: ""
        readonly property bool selected: root.styleGalleryFilter === filterKey

        width: chipLabel.implicitWidth + 26
        height: 34
        radius: 17
        color: selected ? "#F4F5F6" : "#26292F"
        border.width: 1
        border.color: selected ? "#F4F5F6" : theme.alpha(theme.polar, 0.11)

        Behavior on color {
            ColorAnimation { duration: theme.durationMedium }
        }

        Label {
            id: chipLabel
            anchors.centerIn: parent
            text: chip.label
            color: chip.selected ? "#181A1F" : theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 13
            font.weight: chip.selected ? Font.DemiBold : Font.Medium
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler {
            onTapped: root.styleGalleryFilter = chip.filterKey
        }
    }

    component StyleSourceTile: Rectangle {
        id: tile
        property var asset: ({})
        readonly property bool selected: root.assetSelected(asset)
        readonly property string thumbnailSource: root.normalizeImageSource(String(asset.thumbnailSource || ""))

        Layout.fillWidth: true
        implicitHeight: 142
        radius: 20
        color: selected ? "#2C3037" : "#26292F"
        border.width: 1
        border.color: selected ? "#F4F5F6" : theme.alpha(theme.polar, 0.10)
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 7

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 92
                radius: 15
                color: "#1A1C21"
                clip: true

                Image {
                    anchors.fill: parent
                    source: tile.thumbnailSource
                    visible: tile.thumbnailSource.length > 0
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }

                Rectangle {
                    anchors.fill: parent
                    visible: tile.thumbnailSource.length === 0
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#59616C" }
                        GradientStop { position: 1.0; color: "#2B3038" }
                    }
                }

                AppIcon {
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    visible: tile.thumbnailSource.length === 0
                    name: "image"
                    color: theme.alpha(theme.textStrong, 0.82)
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    width: 24
                    height: 24
                    radius: 12
                    visible: tile.selected
                    color: "#F4F5F6"

                    AppIcon {
                        anchors.centerIn: parent
                        width: 13
                        height: 13
                        name: "check"
                        color: "#181A1F"
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.assetDisplayTitle(tile.asset)
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 13
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 1
                elide: Text.ElideRight
            }
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler {
            onTapped: root.chooseStyleGalleryAsset(tile.asset)
        }
    }

    component InspirationTile: Rectangle {
        id: tile
        property var tileData: ({})
        readonly property string styleId: String(tileData.styleId || "")
        readonly property string kind: String(tileData.kind || "style")
        readonly property string title: String(tileData.title || "Style")
        readonly property string thumbnailSource: String(tileData.thumbnailSource || "")
        readonly property color accentA: tileData.accentA || "#7B8BA5"
        readonly property color accentB: tileData.accentB || "#C0A46B"
        readonly property bool tileEnabled: Boolean(tileData.enabled)
        readonly property bool selected: kind === "style" && styleId.length > 0 && root.selectedStyleId() === styleId

        Layout.fillWidth: true
        implicitHeight: 164
        radius: 20
        color: selected ? "#2C3037" : "#26292F"
        border.width: 1
        border.color: selected ? "#D7DAE0" : theme.alpha(theme.polar, 0.10)
        opacity: tileEnabled ? 1 : 0.46
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 9

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                radius: 16
                clip: true
                color: theme.alpha(theme.ink950, 0.42)

                Image {
                    anchors.fill: parent
                    visible: tile.thumbnailSource.length > 0
                    source: tile.thumbnailSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }

                Rectangle {
                    anchors.fill: parent
                    visible: tile.thumbnailSource.length === 0
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: tile.accentA }
                        GradientStop { position: 1.0; color: tile.accentB }
                    }
                    opacity: 0.88
                }

                Rectangle {
                    anchors.fill: parent
                    color: theme.alpha(theme.ink950, 0.16)
                }

                AppIcon {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    visible: tile.thumbnailSource.length === 0
                    name: tile.kind === "learn" ? "select" : "star"
                    color: theme.alpha(theme.textStrong, 0.86)
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    width: 24
                    height: 24
                    radius: 12
                    visible: tile.selected
                    color: "#F4F5F6"

                    AppIcon {
                        anchors.centerIn: parent
                        width: 13
                        height: 13
                        name: "check"
                        color: "#181A1F"
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: tile.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 15
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }

        HoverHandler { cursorShape: tile.tileEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor }
        TapHandler {
            enabled: tile.tileEnabled
            onTapped: root.chooseInspiration(tile.tileData)
        }
    }

    contentItem: Rectangle {
        id: walkthroughSurface
        width: root.width
        height: root.height
        implicitWidth: root.width
        implicitHeight: root.height
        radius: 28
        color: "transparent"
        clip: true

        Behavior on implicitHeight {
            NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            id: walkthroughLayout
            anchors.fill: parent
            anchors.margins: 24
            spacing: 18

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 16
                    color: theme.alpha(theme.polar, 0.08)
                    border.width: 1
                    border.color: theme.alpha(theme.polar, 0.12)

                    AppIcon {
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        name: "adjust"
                        color: theme.textStrong
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: root.currentPage === 2 ? "Agent is editing" : "Welcome to your photo agent"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 26
                        font.weight: Font.Bold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.currentPage === 0
                              ? "Pick reference images from your library or save the selection as a reusable style."
                              : root.currentPage === 1
                                ? "Add any final requests before the agent starts."
                                  : root.statusLine()
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 15
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 92
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: "Step " + (root.currentPage + 1) + " of 3"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: 6
                        Repeater {
                            model: [0, 1, 2]
                            StepDot {}
                        }
                    }
                }
            }

            Item {
                id: pageStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    enabled: root.currentPage === 0
                    opacity: root.currentPage === 0 ? 1 : 0
                    x: root.currentPage === 0 ? 0 : -22
                    spacing: 12

                    Behavior on opacity {
                        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
                    }

                    Behavior on x {
                        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            radius: 22
                            color: "#26292F"
                            border.width: 1
                            border.color: theme.alpha(theme.polar, 0.10)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                spacing: 8

                                AppIcon {
                                    Layout.preferredWidth: 17
                                    Layout.preferredHeight: 17
                                    name: "search"
                                    color: theme.textMuted
                                }

                                TextField {
                                    Layout.fillWidth: true
                                    text: root.styleGalleryQuery
                                    placeholderText: "Search your library"
                                    color: theme.textStrong
                                    placeholderTextColor: theme.alpha(theme.textMuted, 0.62)
                                    font.family: theme.fontUi
                                    font.pixelSize: 15
                                    selectByMouse: true
                                    onTextChanged: {
                                        if (activeFocus)
                                            root.styleGalleryQuery = text
                                    }
                                    background: Rectangle {
                                        color: "transparent"
                                        border.width: 0
                                    }
                                }
                            }
                        }

                        MiniButton {
                            Layout.preferredWidth: 142
                            text: "Save Style"
                            iconName: "star"
                            role: "primary"
                            accent: theme.textStrong
                            enabled: root.selectedReferenceCount() > 0 && root.styleLearningAvailable()
                            disabledReason: root.selectedReferenceCount() <= 0
                                            ? "Select one or more reference photos."
                                            : root.statusLine()
                            onClicked: root.saveSelectedReferencesAsStyle()
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        StyleFilterChip { filterKey: "all"; label: "All" }
                        StyleFilterChip { filterKey: "selected"; label: "Selected" }
                        StyleFilterChip { filterKey: "picked"; label: "Picked" }
                        StyleFilterChip { filterKey: "rated"; label: "Rated" }
                        StyleFilterChip { filterKey: "project"; label: "Project" }
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: styleSourceGrid.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds

                        GridLayout {
                            id: styleSourceGrid
                            width: parent.width
                            columns: width >= 620 ? 4 : 3
                            columnSpacing: 10
                            rowSpacing: 10

                            Repeater {
                                model: root.styleGalleryAssets()

                                StyleSourceTile {
                                    required property var modelData
                                    asset: modelData
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: styleSourceGrid.children.length === 0
                            text: "No matching photos"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 15
                        }

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.selectedReferenceLabel()
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 1
                        elide: Text.ElideNone
                    }
                }

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    enabled: false
                    opacity: 0
                    x: root.currentPage < 1 ? 22 : root.currentPage > 1 ? -22 : 0
                    spacing: 0

                    Behavior on opacity {
                        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
                    }

                    Behavior on x {
                        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14

                        Repeater {
                            model: root.targetTiles()

                            TargetTile {
                                required property var modelData
                                tileKey: String(modelData.key || "")
                                title: String(modelData.title || "")
                                detail: String(modelData.detail || "")
                                count: Number(modelData.count || 0)
                                thumbnailSource: String(modelData.thumbnailSource || "")
                                iconName: String(modelData.icon || "adjust")
                            }
                        }
                    }
                }

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    enabled: root.currentPage === 1
                    opacity: root.currentPage === 1 ? 1 : 0
                    x: root.currentPage < 1 ? 22 : root.currentPage > 1 ? -22 : 0
                    spacing: 16

                    Behavior on opacity {
                        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
                    }

                    Behavior on x {
                        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Prompt your agent"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 21
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 170
                        radius: 22
                        color: theme.alpha(theme.polar, 0.055)
                        border.width: 1
                        border.color: theme.alpha(theme.polar, 0.13)

                        TextArea {
                            anchors.fill: parent
                            anchors.margins: 14
                            text: root.hasController() ? root.agentEditController.userInstructions : ""
                            placeholderText: "Tell it what to preserve, change, avoid, or match."
                            wrapMode: TextEdit.WordWrap
                            enabled: root.hasController() && !root.agentEditController.running
                            color: theme.textStrong
                            selectedTextColor: theme.ink950
                            selectionColor: theme.focusRing
                            font.family: theme.fontUi
                            font.pixelSize: 17
                            onTextChanged: {
                                if (activeFocus && root.hasController())
                                    root.agentEditController.userInstructions = text
                            }
                            background: Rectangle {
                                color: "transparent"
                                border.width: 0
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.selectedTargetText() + " + " + root.selectedStyleName()
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }

                    MiniButton {
                        Layout.alignment: Qt.AlignHCenter
                        minimumReadableWidth: 188
                        text: root.runButtonLabel()
                        iconName: "adjust"
                        role: "primary"
                        accent: theme.textStrong
                        enabled: root.runButtonEnabled()
                        disabledReason: root.statusLine()
                        onClicked: root.runAgent()
                    }
                }

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    enabled: root.currentPage === 2
                    opacity: root.currentPage === 2 ? 1 : 0
                    x: root.currentPage < 2 ? 22 : 0
                    spacing: 16

                    Behavior on opacity {
                        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
                    }

                    Behavior on x {
                        NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.agentRunning() ? "Watching the edit happen" : "Agent activity"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 21
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: root.activityRows()

                            Rectangle {
                                id: activityChip
                                required property var modelData
                                width: activityLabel.implicitWidth + 28
                                height: 34
                                radius: 17
                                color: Boolean(activityChip.modelData.active) ? theme.alpha(theme.polar, 0.13) : theme.alpha(theme.polar, 0.055)
                                border.width: 1
                                border.color: Boolean(activityChip.modelData.active) ? theme.alpha(theme.polar, 0.28) : theme.alpha(theme.polar, 0.10)

                                Label {
                                    id: activityLabel
                                    anchors.centerIn: parent
                                    text: String(activityChip.modelData.label || "")
                                    color: Boolean(activityChip.modelData.active) ? theme.textStrong : theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 13
                                    font.weight: Boolean(activityChip.modelData.active) ? Font.DemiBold : Font.Medium
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: thinkingColumn.implicitHeight + 24
                        radius: 22
                        color: theme.alpha(theme.polar, 0.06)
                        border.width: 1
                        border.color: theme.alpha(theme.polar, 0.13)

                        ColumnLayout {
                            id: thinkingColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: "What the agent is doing"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 1
                                elide: Text.ElideNone
                            }

                            Repeater {
                                model: root.displayThinkingLines()

                                Label {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: String(modelData || "")
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 14
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    maximumLineCount: 3
                                    elide: Text.ElideNone
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.proofImages.length > 0
                        spacing: 10

                        Repeater {
                            model: root.proofImages

                            Rectangle {
                                id: proofCard
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 112
                                radius: 16
                                color: theme.alpha(theme.polar, 0.06)
                                border.width: 1
                                border.color: theme.alpha(theme.polar, 0.12)
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: String(proofCard.modelData.source || "")
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }

                                Label {
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    anchors.leftMargin: 8
                                    anchors.bottomMargin: 8
                                    text: String(proofCard.modelData.label || "")
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                MiniButton {
                    Layout.preferredWidth: 104
                    visible: root.currentPage > 0
                    text: "Back"
                    iconName: "chevron-left"
                    role: "secondary"
                    enabled: root.currentPage > 0 && !root.agentRunning()
                    onClicked: root.previousPage()
                }

                Item { Layout.fillWidth: true }

                MiniButton {
                    Layout.preferredWidth: root.currentPage === 0 ? 132 : 124
                    visible: root.currentPage === 0
                    text: "Add Prompt"
                    iconName: "chevron-right"
                    role: "secondary"
                    enabled: root.scopeCount(root.scopeMode) > 0
                    disabledReason: "Select one or more photos."
                    onClicked: root.nextPage()
                }

                MiniButton {
                    Layout.preferredWidth: 108
                    visible: root.currentPage === 2 && !root.agentRunning()
                    text: "Close"
                    iconName: "close"
                    role: "secondary"
                    onClicked: root.close()
                }
            }
        }
    }
}
