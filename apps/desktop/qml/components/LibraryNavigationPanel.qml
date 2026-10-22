pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    objectName: "libraryNavigationPanel"

    Theme { id: theme }

    property var controller: null
    property var projectNames: []
    property var recentImportSessions: []
    property var currentFilters: ({})
    property bool creatingProject: false
    property bool creatingSearch: false
    property string projectNameDraft: ""
    property string searchNameDraft: ""
    property string workflowMessage: ""

    readonly property bool collapsed: controller ? controller.navigationCollapsed : false
    readonly property real expandedWidth: theme.scaled(252)
    readonly property real collapsedWidth: theme.scaled(54)
    readonly property var stageValues: ["review", "edit", "ready", "delivered", "archived"]
    readonly property var stageLabels: ["Review", "Edit", "Ready", "Delivered · manual", "Archived"]

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

    Rectangle {
        anchors.fill: parent
        radius: theme.radiusPanel
        color: theme.alpha(theme.ink950, 0.54)
        border.width: theme.hairlineWidth
        border.color: theme.alpha(theme.lineSoft, 0.28)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.collapsed ? theme.space6 : theme.space10
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
                    role: "secondary"
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
                            text: "SHOOTS & ALBUMS"
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeMicro
                            font.letterSpacing: 0.6
                            maximumLineCount: 1
                        }
                        MiniButton {
                            objectName: "libraryNavigationNewShootButton"
                            role: "icon"
                            iconName: "add"
                            tooltip: "Create a shoot or album"
                            a11yName: "Create a shoot or album"
                            onClicked: root.creatingProject = !root.creatingProject
                        }
                    }

                    MiniButton {
                        objectName: "libraryNavigationProjectsButton"
                        Layout.fillWidth: true
                        visible: root.collapsed
                        role: "secondary"
                        iconName: "folder"
                        tooltip: "Shoots and albums"
                        a11yName: "Shoots and albums"
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
                            placeholderText: "Shoot or album name"
                            Accessible.name: "New shoot or album name"
                            onTextChanged: root.projectNameDraft = text
                            onAccepted: root.beginProjectCreation()
                        }
                        MiniButton {
                            objectName: "libraryNavigationCreateProjectConfirm"
                            role: "icon"
                            iconName: "check"
                            tooltip: "Create shoot or album"
                            enabled: root.projectNameDraft.trim().length > 0
                            onClicked: root.beginProjectCreation()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.collapsed && (!root.projectNames || root.projectNames.length === 0)
                        text: "No shoots or albums yet"
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
                            Layout.fillWidth: true
                            spacing: theme.space2

                            MiniButton {
                                objectName: "libraryNavigationProject_" + projectRow.projectName
                                Layout.fillWidth: true
                                role: "secondary"
                                iconName: "folder"
                                text: projectRow.projectName
                                labelVisible: !root.collapsed
                                active: root.controller && root.controller.activeDestination === "project:" + projectRow.projectName
                                tooltip: projectRow.projectName
                                onClicked: if (root.controller) root.controller.requestProject(projectRow.projectName)
                            }

                            ToneComboBox {
                                id: popupTooltipAnchor1
                                objectName: "libraryProjectStage_" + projectRow.projectName
                                Layout.fillWidth: true
                                visible: !root.collapsed
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
                                    visible: popupTooltipAnchor1.visible && (currentIndex === 3 && popupTooltipAnchor1.hovered)
                                    text: "Manual delivery tracking only; this does not verify an export."
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.collapsed && root.controller && root.controller.activeDestination.startsWith("project:")
                                 && String(root.controller.projectStages[root.controller.activeDestination.slice(8)] || "review") === "delivered"
                        text: "Delivered is a manual marker; export is not verified."
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        wrapMode: Text.WordWrap
                        leftPadding: theme.space8
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space4

                    Label {
                        Layout.fillWidth: true
                        visible: !root.collapsed
                        text: "RECENT IMPORTS"
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeMicro
                        font.letterSpacing: 0.6
                    }

                    MiniButton {
                        objectName: "libraryNavigationImportsButton"
                        Layout.fillWidth: true
                        visible: root.collapsed
                        role: "secondary"
                        iconName: "clock"
                        tooltip: "Recent imports"
                        a11yName: "Recent imports"
                        active: root.controller && root.controller.activeDestination === "recent-imports"
                        onClicked: if (root.controller) root.controller.requestRecentImports()
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.collapsed && root.importsForNavigation().length === 0
                        text: "Completed imports will appear here"
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                        leftPadding: theme.space8
                    }

                    Repeater {
                        model: root.collapsed ? [] : root.importsForNavigation()
                        delegate: MiniButton {
                            id: importRow
                            required property var modelData
                            objectName: "libraryNavigationImport_" + String(modelData.jobId || "")
                            Layout.fillWidth: true
                            role: "secondary"
                            iconName: "source"
                            text: String(modelData.displayLabel || modelData.sourcePath || "Completed import")
                            tooltip: text
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
                            text: "SAVED SEARCHES"
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeMicro
                            font.letterSpacing: 0.6
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
                        font.pixelSize: theme.typeMicro
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.collapsed && root.controller && root.controller.savedSearches.length === 0
                        text: "Save a search to return to these filters."
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                        leftPadding: theme.space8
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
                                role: "secondary"
                                iconName: "search"
                                text: String(savedSearchRow.modelData.name || "Saved search")
                                tooltip: text
                                active: root.controller && root.controller.activeDestination === "saved-search:" + String(savedSearchRow.modelData.id || "")
                                onClicked: if (root.controller) root.controller.activateSavedSearch(String(savedSearchRow.modelData.id || ""))
                            }
                            MiniButton {
                                objectName: "libraryNavigationDeleteSearch_" + String(savedSearchRow.modelData.id || "")
                                role: "icon"
                                iconName: "close"
                                tooltip: "Delete saved search " + String(savedSearchRow.modelData.name || "")
                                a11yName: tooltip
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
