pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

StudioPanel {
    id: root
    objectName: "libraryNavigationPanel"
    title: ""
    headerVisible: false

    Theme { id: theme }

    property var controller: null
    property var catalogModel: null
    property var projectNames: []
    property var recentImportSessions: []
    property var currentFilters: ({})
    property bool creatingProject: false
    property bool creatingSearch: false
    property string projectNameDraft: ""
    property string searchNameDraft: ""
    property string workflowMessage: ""

    readonly property bool collapsed: controller ? controller.navigationCollapsed : false
    readonly property bool recentImportsActive: controller !== null &&
                                                (controller.activeDestination === "recent-imports" ||
                                                 String(controller.activeDestination || "").startsWith("import:"))
    readonly property real expandedWidth: theme.scaled(252)
    readonly property real collapsedWidth: theme.scaled(54)
    readonly property var stageValues: ["review", "edit", "ready", "delivered", "archived"]
    readonly property var stageLabels: ["Review", "Edit", "Ready", "Delivered", "Archived"]

    signal createProjectRequested(string projectName)

    implicitWidth: collapsed ? collapsedWidth : expandedWidth
    implicitHeight: theme.scaled(520)

    function importsForNavigation() {
        const records = Array.from(root.recentImportSessions || [])
        records.sort(function(left, right) {
            return new Date(String(right.createdAt || "")).getTime()
                   - new Date(String(left.createdAt || "")).getTime()
        })
        return records.filter(function(record) { return String(record.status || "") === "Completed" }).slice(0, 6)
    }

    function importNavigationLabel(record) {
        let label = String(record && record.displayLabel || "").trim()
        if (label.length === 0)
            label = String(record && record.sourcePath || "").trim()
        label = label.replace(/[\\/]+$/, "")
        const parts = label.split(/[\\/]/)
        const leaf = parts.length > 0 ? String(parts[parts.length - 1] || "").trim() : ""
        return leaf.length > 0 ? leaf : "Completed import"
    }

    function projectAssetCount(projectName) {
        if (!root.catalogModel)
            return 0
        const revision = Number(root.catalogModel.projectStatsRevision || 0)
        const count = Number(root.catalogModel.assetCountForProject(projectName) || 0)
        return revision >= 0 ? count : 0
    }

    function beginProjectCreation() {
        const name = root.projectNameDraft.trim()
        if (name.length === 0)
            return
        root.createProjectRequested(name)
        root.projectNameDraft = ""
        root.creatingProject = false
    }

    function beginSearchSave() {
        if (!root.controller)
            return
        if (root.controller.saveSearch(root.searchNameDraft, root.currentFilters)) {
            root.searchNameDraft = ""
            root.creatingSearch = false
            root.workflowMessage = ""
        } else {
            root.workflowMessage = root.controller.persistenceError.length > 0
                    ? root.controller.persistenceError
                    : "Choose a unique name for this saved search."
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space6

            Label {
                Layout.fillWidth: true
                visible: !root.collapsed
                text: "Library"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: theme.typeTitle
                font.weight: Font.DemiBold
                maximumLineCount: 1
                elide: Text.ElideRight
            }

            MiniButton {
                objectName: "libraryNavigationCollapseButton"
                Layout.alignment: Qt.AlignRight
                role: "icon"
                iconName: root.collapsed ? "panel-right" : "panel-left"
                tooltip: root.collapsed ? "Expand Library navigation" : "Collapse Library navigation"
                a11yName: tooltip
                onClicked: if (root.controller) root.controller.navigationCollapsed = !root.collapsed
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.hairlineWidth
            color: theme.alpha(theme.lineSoft, 0.24)
        }

        ScrollView {
            id: navigationScroll
            objectName: "libraryNavigationScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: navigationScroll.availableWidth
                spacing: theme.space10

                Label {
                    Layout.fillWidth: true
                    visible: !root.collapsed && root.controller && root.controller.persistenceError.length > 0
                    text: root.controller ? root.controller.persistenceError : ""
                    color: theme.error
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                MiniButton {
                    objectName: "libraryNavigationAllImagesButton"
                    Layout.fillWidth: true
                    role: "ghost"
                    contentAlignment: Qt.AlignLeft
                    iconName: "image"
                    text: "All Images"
                    labelVisible: !root.collapsed
                    active: root.controller && root.controller.activeDestination === "all-images"
                    tooltip: "Show all library images"
                    onClicked: if (root.controller) root.controller.requestAllImages()
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space4

                    RowLayout {
                        Layout.fillWidth: true
                        visible: !root.collapsed
                        Label {
                            Layout.fillWidth: true
                            text: "Projects"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                            maximumLineCount: 1
                        }
                        MiniButton {
                            objectName: "libraryNavigationNewShootButton"
                            role: "icon"
                            iconName: "add"
                            tooltip: "Create a project"
                            a11yName: "Create a project"
                            onClicked: root.creatingProject = !root.creatingProject
                        }
                    }

                    MiniButton {
                        objectName: "libraryNavigationProjectsButton"
                        Layout.fillWidth: true
                        visible: root.collapsed
                        role: "ghost"
                        contentAlignment: Qt.AlignLeft
                        iconName: "folder"
                        text: "Projects"
                        labelVisible: !root.collapsed
                        tooltip: "Projects"
                        a11yName: "Projects"
                        active: root.controller && root.controller.activeDestination.startsWith("project:")
                        onClicked: if (root.controller) root.controller.navigationCollapsed = false
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.creatingProject && !root.collapsed
                        spacing: theme.space4
                        ToneTextField {
                            objectName: "libraryNavigationProjectNameField"
                            Layout.fillWidth: true
                            text: root.projectNameDraft
                            placeholderText: "Project name"
                            Accessible.name: "New project name"
                            onTextChanged: root.projectNameDraft = text
                            onAccepted: root.beginProjectCreation()
                        }
                        MiniButton {
                            objectName: "libraryNavigationCreateProjectConfirm"
                            role: "icon"
                            iconName: "check"
                            tooltip: "Create project"
                            enabled: root.projectNameDraft.trim().length > 0
                            onClicked: root.beginProjectCreation()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.collapsed && (!root.projectNames || root.projectNames.length === 0)
                        text: "No projects yet"
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                        leftPadding: theme.space8
                    }

                    Repeater {
                        model: root.projectNames || []

                        delegate: ColumnLayout {
                            id: projectRow
                            required property string modelData
                            readonly property string projectName: modelData
                            readonly property int assetCount: root.projectAssetCount(projectName)
                            readonly property string accessibleSummary: projectName + ", " + assetCount +
                                                                        (assetCount === 1 ? " photo" : " photos")
                            Layout.fillWidth: true
                            spacing: theme.space2

                            Item {
                                objectName: "libraryNavigationProjectRow_" + projectRow.projectName
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(theme.controlHeightDense,
                                                                 projectRowNameLabel.implicitHeight + theme.space8)

                                MiniButton {
                                    id: projectRowButton
                                    objectName: "libraryNavigationProject_" + projectRow.projectName
                                    anchors.fill: parent
                                    role: "ghost"
                                    contentAlignment: Qt.AlignLeft
                                    text: ""
                                    iconName: root.collapsed ? "folder" : ""
                                    labelVisible: false
                                    active: root.controller && root.controller.activeDestination === "project:" + projectRow.projectName
                                    a11yName: projectRow.accessibleSummary
                                    tooltip: projectRow.accessibleSummary
                                    Accessible.description: "Open project " + projectRow.projectName
                                    onClicked: if (root.controller) root.controller.requestProject(projectRow.projectName)
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: root.collapsed ? 0 : theme.space8
                                    anchors.rightMargin: root.collapsed ? 0 : theme.space8
                                    spacing: theme.space6
                                    enabled: false
                                    visible: !root.collapsed

                                    AppIcon {
                                        name: "folder"
                                        color: projectRowButton.active ? theme.textStrong : theme.textMuted
                                        Layout.preferredWidth: theme.scaled(14)
                                        Layout.preferredHeight: theme.scaled(14)
                                    }

                                    Label {
                                        id: projectRowNameLabel
                                        Layout.fillWidth: true
                                        text: projectRow.projectName
                                        color: projectRowButton.active ? theme.textStrong : theme.textMain
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                        font.weight: theme.weightControl
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        maximumLineCount: 2
                                        elide: Text.ElideNone
                                    }

                                    Label {
                                        objectName: "libraryNavigationProjectCount_" + projectRow.projectName
                                        text: String(projectRow.assetCount)
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeCaption
                                        horizontalAlignment: Text.AlignRight
                                        Accessible.ignored: true
                                    }
                                }
                            }

                            ToneComboBox {
                                id: popupTooltipAnchor1
                                objectName: "libraryProjectStage_" + projectRow.projectName
                                Layout.fillWidth: true
                                Layout.leftMargin: theme.space8
                                visible: !root.collapsed && root.controller &&
                                         root.controller.activeDestination === "project:" + projectRow.projectName
                                accessibleLabel: "Workflow stage for " + projectRow.projectName
                                model: root.stageLabels
                                currentIndex: {
                                    const projectStages = root.controller ? root.controller.projectStages : ({})
                                    return Math.max(0, root.stageValues.indexOf(String(projectStages[projectRow.projectName] || "review")))
                                }
                                onActivated: function(index) {
                                    if (root.controller)
                                        root.controller.setProjectStage(projectRow.projectName, root.stageValues[index])
                                }
                                AppToolTip {
                                    id: sharedTooltip2
                                    targetItem: popupTooltipAnchor1
                                    visible: popupTooltipAnchor1.visible && (popupTooltipAnchor1.currentIndex === 3 && popupTooltipAnchor1.hovered)
                                    text: "Manual delivery tracking only; this does not verify an export."
                                }
                            }
                        }
                    }

                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space4

                    MiniButton {
                        objectName: "libraryNavigationImportsButton"
                        Layout.fillWidth: true
                        role: "ghost"
                        contentAlignment: Qt.AlignLeft
                        iconName: "clock"
                        text: "Recent imports"
                        labelVisible: !root.collapsed
                        tooltip: "Recent imports"
                        active: root.recentImportsActive
                        onClicked: if (root.controller) root.controller.requestRecentImports()
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.collapsed && root.recentImportsActive &&
                                 root.importsForNavigation().length === 0
                        text: "No completed imports"
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                        leftPadding: theme.space8
                    }

                    Repeater {
                        model: root.collapsed || !root.recentImportsActive
                               ? [] : root.importsForNavigation()
                        delegate: MiniButton {
                            id: importRow
                            required property var modelData
                            objectName: "libraryNavigationImport_" + String(modelData.jobId || "")
                            Layout.fillWidth: true
                            Layout.leftMargin: theme.space8
                            role: "ghost"
                            contentAlignment: Qt.AlignLeft
                            text: root.importNavigationLabel(modelData)
                            tooltip: String(modelData.sourcePath || modelData.displayLabel || "Open completed import")
                            Accessible.description: String(modelData.sourcePath || "")
                            active: root.controller && root.controller.activeDestination === "import:" + String(modelData.jobId || "")
                            onClicked: if (root.controller) root.controller.requestImportSession(String(modelData.jobId || ""))
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space4

                    RowLayout {
                        Layout.fillWidth: true
                        visible: !root.collapsed
                        Label {
                            Layout.fillWidth: true
                            text: "Saved searches"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                        }
                        MiniButton {
                            objectName: "libraryNavigationSaveSearchButton"
                            role: "icon"
                            iconName: "add"
                            tooltip: "Save current filters and sort order"
                            a11yName: "Save current filters and sort order"
                            enabled: root.currentFilters && Object.keys(root.currentFilters).length > 0
                            onClicked: root.creatingSearch = !root.creatingSearch
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.creatingSearch && !root.collapsed
                        spacing: theme.space4
                        ToneTextField {
                            objectName: "libraryNavigationSearchNameField"
                            Layout.fillWidth: true
                            text: root.searchNameDraft
                            placeholderText: "Name this search"
                            Accessible.name: "Saved search name"
                            onTextChanged: root.searchNameDraft = text
                            onAccepted: root.beginSearchSave()
                        }
                        MiniButton {
                            objectName: "libraryNavigationSaveSearchConfirm"
                            role: "icon"
                            iconName: "check"
                            tooltip: "Save search"
                            enabled: root.searchNameDraft.trim().length > 0
                            onClicked: root.beginSearchSave()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.collapsed && root.workflowMessage.length > 0
                        text: root.workflowMessage
                        color: theme.warning
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: root.collapsed || !root.controller ? [] : root.controller.savedSearches
                        delegate: RowLayout {
                            id: savedSearchRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: theme.space2

                            MiniButton {
                                objectName: "libraryNavigationSavedSearch_" + String(savedSearchRow.modelData.id || "")
                                Layout.fillWidth: true
                                role: "ghost"
                                contentAlignment: Qt.AlignLeft
                                text: String(savedSearchRow.modelData.name || "Saved search")
                                tooltip: text
                                active: root.controller && root.controller.activeDestination === "saved-search:" + String(savedSearchRow.modelData.id || "")
                                onClicked: if (root.controller) root.controller.activateSavedSearch(String(savedSearchRow.modelData.id || ""))
                            }
                            HoverHandler { id: savedSearchHover }
                            MiniButton {
                                objectName: "libraryNavigationDeleteSearch_" + String(savedSearchRow.modelData.id || "")
                                visible: savedSearchHover.hovered ||
                                         (root.controller && root.controller.activeDestination ===
                                          "saved-search:" + String(savedSearchRow.modelData.id || ""))
                                role: "icon"
                                iconName: "close"
                                tooltip: "Delete saved search"
                                a11yName: "Delete saved search " + String(savedSearchRow.modelData.name || "")
                                onClicked: {
                                    if (!root.controller)
                                        return
                                    if (!root.controller.deleteSearch(String(savedSearchRow.modelData.id || "")))
                                        root.workflowMessage = root.controller.persistenceError.length > 0
                                                ? root.controller.persistenceError
                                                : "Saved search could not be deleted."
                                    else
                                        root.workflowMessage = ""
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
