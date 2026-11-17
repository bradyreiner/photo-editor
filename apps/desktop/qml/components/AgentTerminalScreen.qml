pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    objectName: "agentTerminalScreen"
    required property var dialog
    required property var themeObject

    readonly property color phosphor: themeObject.textStrong
    readonly property color textColor: themeObject.textStrong
    readonly property color muted: themeObject.textMuted
    readonly property string mono: themeObject.fontNumeric
    readonly property bool compact: height < 340
    readonly property int rowsPerPage: 3
    readonly property var checkedAssets: dialog.targetAssets()
    readonly property int checkedCount: dialog.scopeCount(dialog.scopeMode)
    property string panel: "edit"
    property bool activityDetailsExpanded: false
    property bool activityHistoryExpanded: false
    AgentFailureDetails { id: failureDetails }
    readonly property bool failureDetailsOpen: failureDetails.visible
    function showFailureDetails(opener) {
        failureDetails.openFor(root.dialog.lastReport(), root.dialog.failureDetail(), opener)
    }
    property int archivePage: 0
    property int targetsPage: 0
    property string archiveFilter: "looks"
    readonly property var archiveItems: (dialog.inspirationSuggestions || []).filter(function(item) {
        return archiveFilter === "looks" ? item.kind === "preset" : item.kind === "photo"
    })
    readonly property int archivePageCount: Math.max(1, Math.ceil(archiveItems.length / rowsPerPage))
    readonly property int targetPageCount: Math.max(1, Math.ceil(checkedAssets.length / rowsPerPage))
    readonly property string attachedInspiration: (dialog.inspirationSuggestions || []).filter(function(item) {
        return dialog.suggestionSelected(item)
    }).map(function(item) { return item.title }).join(" + ")
    readonly property var selectedModel: dialog.activeModel()
    readonly property var existingOllamaModel: {
        const models = dialog.hasController() ? dialog.agentEditController.availableModels || [] : []
        return models.find(function(model) {
            return model.backend === "ollama" && model.runtime === "external" &&
                   String(model.model || "") === String(selectedModel.model || "")
        }) || ({})
    }
    readonly property var fastOllamaModel: {
        const models = dialog.hasController() ? dialog.agentEditController.availableModels || [] : []
        return models.find(function(model) {
            return model.backend === "ollama" && model.runtime === "external" && model.model === "qwen3.6:35b"
        }) || ({})
    }

    function showModelConnection() {
        root.dialog.modelSetupRequested()
    }

    function useExistingOllama() {
        if (!dialog.hasController() || !existingOllamaModel.id || running || dialog.agentEditController.modelProbeRunning)
            return
        dialog.agentEditController.selectedModelId = existingOllamaModel.id
        dialog.agentEditController.probeSelectedModelAsync(true)
    }

    Connections {
        target: root.dialog.agentEditController
        function onModelProbeChanged() {
            if (root.panel === "model" && !root.dialog.agentEditController.modelProbeRunning && root.selectedModel.available)
                root.panel = "edit"
        }
    }

    readonly property bool running: dialog.agentRunning()
    readonly property bool awaitingInput: dialog.hasController() && Boolean(dialog.agentEditController.awaitingUserInput)
    readonly property bool activityVisible: visible && dialog.visible && dialog.currentPage === 2 && panel !== "targets"
    readonly property var events: dialog.runEvents()
    readonly property var latestEvent: events.length > 0 ? events[events.length - 1] : ({})
    readonly property int activeStage: stageIndex(String(latestEvent.stage || "connect"))
    readonly property var outputRows: terminalRows()
    readonly property var activityHistoryRows: events.slice(-8).reverse()
    readonly property string latestOutput: outputRows.length > 0 ? String(outputRows[outputRows.length - 1].text) : dialog.statusLine()
    readonly property string currentTaskSummary: {
        const detail = String(latestEvent.detail || latestEvent.explanation || latestEvent.label || "").trim()
        if (detail.length > 0)
            return detail
        return String(dialog.statusLine() || (root.running ? "Working on your edit." : "Ready for your next step."))
    }
    readonly property string modelOutputText: {
        for (let index = events.length - 1; index >= 0; --index) {
            const evidence = (events[index] || ({})).evidence || ({})
            const output = String(evidence.modelOutput || "").trim()
            if (evidence.modelOutputAvailable && output.length > 0)
                return output
        }
        return ""
    }
    property int revealedCharacters: 0
    property bool cursorLit: true

    function stageIndex(stage) {
        if (stage === "connect" || stage === "observe") return 0
        if (stage === "plan" || stage.indexOf("plan-worker") === 0 || stage === "constrain" || stage === "fallback") return 1
        if (stage === "act" || stage === "apply" || stage === "render" || stage === "preview" || stage === "repair") return 2
        return 3
    }

    function activityEventTitle(event) {
        const item = event || ({})
        const label = String(item.label || "").trim()
        if (label.length > 0)
            return label
        const stage = String(item.stage || "").toLowerCase()
        if (stage.indexOf("plan") === 0 || stage === "constrain") return "Planning"
        if (stage === "render" || stage === "apply" || stage === "edit") return "Editing"
        if (stage === "validate" || stage === "check") return "Checking the result"
        if (stage === "connect" || stage === "observe") return "Reviewing the photo"
        return stage.length > 0 ? stage.charAt(0).toUpperCase() + stage.slice(1) : "Working"
    }

    function activityEventStatus(event) {
        const status = String((event || ({})).status || "").toLowerCase()
        if (status === "running") return "In progress"
        if (status === "complete") return "Done"
        if (status === "failed") return "Stopped"
        return status.length > 0 ? status.charAt(0).toUpperCase() + status.slice(1) : "Recorded"
    }

    function terminalRows() {
        // Coalesce worker updates while retaining the actual returned model summary.
        const rows = []
        for (let i = 0; i < events.length; ++i) {
            const event = events[i] || ({})
            const evidence = event.evidence || ({})
            const modelOutput = evidence.modelOutputAvailable ? String(evidence.modelOutput || "").trim() : ""
            const text = modelOutput || String(event.detail || event.explanation || event.label || "").trim()
            if (!text.length) continue
            const row = { key: String(event.stage || ""), text: text, source: modelOutput.length > 0 ? "MODEL" : "SYSTEM",
                status: String(event.status || "running"), time: Math.floor(Number(event.elapsedMsSinceRunStart || 0) / 1000) }
            if (rows.length > 0 && rows[rows.length - 1].key === row.key && rows[rows.length - 1].source === row.source)
                rows[rows.length - 1] = row
            else
                rows.push(row)
        }
        const recent = rows.slice(-3)
        const modelRow = rows.slice().reverse().find(function(row) { return row.source === "MODEL" })
        if (modelRow && !recent.includes(modelRow)) recent[0] = modelRow
        return recent
    }

    function stageComplete(index) {
        return events.some(function(event) {
            return stageIndex(String(event.stage || "")) === index && String(event.status || "") === "complete"
        })
    }

    onLatestOutputChanged: revealedCharacters = themeObject.reducedMotion ? latestOutput.length : 0
    onActivityVisibleChanged: {
        revealedCharacters = !activityVisible || themeObject.reducedMotion ? latestOutput.length : 0
    }
    Timer {
        interval: 24
        repeat: true
        running: root.activityVisible && !root.themeObject.reducedMotion && root.revealedCharacters < root.latestOutput.length
        onTriggered: root.revealedCharacters = Math.min(root.latestOutput.length, root.revealedCharacters + Math.max(3, Math.ceil(root.latestOutput.length / 36)))
    }
    Timer {
        interval: 530
        repeat: true
        running: root.activityVisible && root.running && !root.awaitingInput && !root.themeObject.reducedMotion
        onTriggered: root.cursorLit = !root.cursorLit
        onRunningChanged: if (!running) root.cursorLit = true
    }

    function focusPageEntry() {
        panel = "edit"
        if (dialog.currentPage === 0)
            promptEditor.forceActiveFocus(Qt.TabFocusReason)
        else if (dialog.currentPage === 1)
            archiveBack.forceActiveFocus(Qt.TabFocusReason)
        else if (dialog.currentPage === 2)
            cancelButton.forceActiveFocus(Qt.TabFocusReason)
    }

    onArchiveFilterChanged: archivePage = 0
    onRunningChanged: {
        if (!running) {
            activityDetailsExpanded = false
            activityHistoryExpanded = false
        }
    }
    onRowsPerPageChanged: {
        archivePage = Math.min(archivePage, archivePageCount - 1)
        targetsPage = Math.min(targetsPage, targetPageCount - 1)
    }

    component TerminalLabel: BodyLabel {
        color: root.textColor
        textFormat: Text.PlainText
        font.family: root.themeObject.fontUi
        font.pixelSize: root.themeObject.typeBody
        wrapMode: Text.WordWrap
    }

    component TerminalButton: ActionButton {
        property bool primary: false
        property bool selected: false
        property bool subtle: false
        property string accessibleLabel: text
        accessibleName: accessibleLabel
        compact: true
        embedded: subtle
        active: selected
        tone: primary ? "primary" : subtle ? "quiet" : "secondary"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        TerminalButton {
            objectName: "agentCheckedSelectionAlert"
            Layout.fillWidth: true
            implicitHeight: 32
            accessibleLabel: root.checkedCount + " photos checked. " + root.dialog.targetSummary() + ". Show checked files."
            text: root.checkedCount > 0 ? root.checkedCount + " file" + (root.checkedCount === 1 ? "" : "s") +
                  (root.dialog.scopeMode === "project" ? " in project" : root.dialog.scopeMode === "filtered-library" ? " in filter" : " checked") +
                  " · " + (root.checkedAssets.length > 0 ? root.dialog.assetDisplayTitle(root.checkedAssets[0]) : "View")
                                        : "No files checked"
            onClicked: { root.panel = root.panel === "targets" ? "edit" : "targets"; root.targetsPage = 0 }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.panel === "targets" ? 4 : root.panel === "model" ? 5 : root.dialog.currentPage

            ColumnLayout {
                spacing: root.compact ? 8 : 12
                RowLayout {
                    Layout.fillWidth: true
                    TerminalLabel {
                        Layout.fillWidth: true
                        text: "Edit request"
                        color: root.phosphor
                        font.pixelSize: root.compact ? root.themeObject.typeDialogTitle : root.themeObject.typeHero
                        font.weight: Font.DemiBold
                    }
                    TerminalButton {
                        objectName: "agentChooseModelButton"
                        text: "Model settings…"
                        onClicked: root.showModelConnection()
                    }
                }
                TerminalLabel {
                    Layout.fillWidth: true
                    text: "Describe the edit, or start from a reference."
                    color: root.muted
                    font.pixelSize: root.themeObject.typeBody
                }
                ToneTextField {
                    id: promptEditor
                    objectName: "agentPromptEditor"
                    Layout.fillWidth: true
                    implicitHeight: 52
                    text: root.dialog.terminalPrompt
                    placeholderText: "What should change?"
                    leftPadding: 12
                    rightPadding: 12
                    activeFocusOnTab: true
                    selectByMouse: true
                    Accessible.name: "AI edit instructions"
                    Accessible.description: "Optional when using a saved look or reference photo. Press Enter to prepare an edit."
                    onTextChanged: {
                        root.dialog.terminalPrompt = text
                        if (root.dialog.hasController())
                            root.dialog.agentEditController.userInstructions = text
                        root.dialog.directionError = ""
                    }
                    onAccepted: root.dialog.runTerminalDirection()
                    Keys.priority: Keys.BeforeItem
                    Keys.onEscapePressed: function(event) { root.dialog.close(); event.accepted = true }
                }
                TerminalLabel {
                    objectName: "agentAttachedInspirationLabel"
                    Layout.fillWidth: true
                    visible: root.attachedInspiration.length > 0
                    text: "Using · " + root.attachedInspiration
                    color: root.attachedInspiration.length > 0 ? root.phosphor : root.muted
                    font.pixelSize: root.themeObject.typeCaption
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Accessible.name: text
                }
                TerminalLabel {
                    Layout.fillWidth: true
                    visible: root.dialog.directionError.length > 0
                    text: root.dialog.directionError
                    color: root.themeObject.error
                    font.pixelSize: root.themeObject.typeCaption
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TerminalButton {
                        objectName: "agentUseInspirationButton"
                        text: "Add reference"
                        tooltipText: "Choose a saved look or photo to guide the edit. Reference photos are never edited."
                        onClicked: { root.archivePage = 0; root.dialog.showSavedInspiration() }
                    }
                    Item { Layout.fillWidth: true }
                    TerminalButton {
                        objectName: "agentPrimaryCreateDraftButton"
                        text: root.dialog.workflowPrimaryKey() === "checking_local_model" ? "Checking AI..."
                              : root.dialog.workflowPrimaryKey() === "set_up_local_ai" || root.dialog.workflowPrimaryKey() === "check_local_model" ? "Connect AI >" : "Prepare edit >"
                        primary: true
                        enabled: root.dialog.terminalDirectionReady()
                        onClicked: {
                            const key = root.dialog.workflowPrimaryKey()
                            if (key === "set_up_local_ai" || key === "check_local_model") root.showModelConnection()
                            else root.dialog.runTerminalDirection()
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 8
                TerminalLabel { text: "Past inspiration"; color: root.phosphor; font.pixelSize: root.themeObject.typeDialogTitle }
                RowLayout {
                    TerminalButton { text: "Saved looks"; selected: root.archiveFilter === "looks"; onClicked: root.archiveFilter = "looks" }
                    TerminalButton { text: "Past references"; selected: root.archiveFilter === "references"; onClicked: root.archiveFilter = "references" }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 5
                    Repeater {
                        model: root.archiveItems.slice(root.archivePage * root.rowsPerPage, (root.archivePage + 1) * root.rowsPerPage)
                        delegate: TerminalButton {
                            id: inspirationRow
                            required property var modelData
                            objectName: "agentSavedInspiration-" + String(modelData.id)
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.maximumHeight: 62
                            selected: root.dialog.suggestionSelected(modelData)
                            accessibleLabel: String(modelData.title) + (selected ? ", selected" : ", select inspiration")
                            contentItem: RowLayout {
                                spacing: 10
                                TerminalLabel { text: inspirationRow.selected ? "[x]" : "[ ]"; color: root.phosphor }
                                Image {
                                    visible: inspirationRow.modelData.kind === "photo" && String(inspirationRow.modelData.thumbnailSource || "").length > 0
                                    Layout.preferredWidth: visible ? 30 : 0
                                    Layout.preferredHeight: 30
                                    source: root.dialog.normalizeImageSource(inspirationRow.modelData.thumbnailSource || "")
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    clip: true
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    TerminalLabel {
                                        Layout.fillWidth: true
                                        text: inspirationRow.modelData.title
                                        color: root.phosphor
                                        font.pixelSize: root.themeObject.typeBody
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }
                                    TerminalLabel {
                                        Layout.fillWidth: true
                                        text: String(inspirationRow.modelData.detail || "Saved inspiration")
                                        color: root.muted
                                        font.pixelSize: root.themeObject.typeMicro
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            onClicked: root.dialog.toggleSuggestion(modelData)
                        }
                    }
                    TerminalLabel {
                        Layout.fillWidth: true
                        visible: root.archiveItems.length === 0
                        text: root.archiveFilter === "looks" ? "No saved inspiration yet.\nYou can still start with a prompt."
                                                              : "No past reference photos are available here."
                        color: root.muted
                    }
                    Item { Layout.fillHeight: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton { objectName: "agentArchivePrevious"; text: "<"; enabled: root.archivePage > 0; onClicked: root.archivePage -= 1 }
                    TerminalLabel { Layout.fillWidth: true; text: (root.archivePage + 1) + " / " + root.archivePageCount; horizontalAlignment: Text.AlignHCenter; color: root.muted }
                    TerminalButton { objectName: "agentArchiveNext"; text: ">"; enabled: root.archivePage + 1 < root.archivePageCount; onClicked: root.archivePage += 1 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        id: archiveBack
                        objectName: "agentChangePromptButton"
                        text: "< Back"
                        onClicked: root.dialog.setPage(0)
                    }
                    Item { Layout.fillWidth: true }
                    TerminalButton {
                        objectName: "agentUseSavedInspirationButton"
                        text: "Use selected >"
                        primary: true
                        enabled: root.dialog.selectedSuggestionKeys.length > 0
                        onClicked: root.dialog.setPage(0)
                    }
                }
            }

            ColumnLayout {
                spacing: root.compact ? 6 : 10
                RowLayout {
                    Layout.fillWidth: true
                    TerminalLabel {
                        text: root.awaitingInput ? "Input required" : root.running ? "Working" : "Run status"
                        color: root.phosphor
                        font.pixelSize: root.compact ? root.themeObject.typeTitle : root.themeObject.typeHero
                    }
                    Item { Layout.fillWidth: true }
                    TerminalLabel {
                        objectName: "agentRunElapsed"
                        text: Math.floor(root.dialog.runElapsedSeconds / 60) + ":" + String(root.dialog.runElapsedSeconds % 60).padStart(2, "0")
                        color: root.muted
                        font.pixelSize: root.themeObject.typeBody
                        font.family: root.mono
                        font.features: root.themeObject.numericFontFeatures
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Repeater {
                        model: ["Review", "Plan", "Edit", "Check"]
                        delegate: Rectangle {
                            id: phase
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            implicitHeight: 25
                            color: root.activeStage === index ? "#393329" : "#202124"
                            border.width: root.themeObject.highContrast ? 1 : 0
                            TerminalLabel {
                                anchors.centerIn: parent
                                text: (root.activeStage === phase.index && root.running ? "> " : root.stageComplete(phase.index) ? "+ " : "") + phase.modelData
                                font.pixelSize: root.themeObject.typeCaption
                                color: root.activeStage === phase.index ? root.phosphor : root.muted
                            }
                        }
                    }
                }
                ProgressStrip {
                    id: activityTrack
                    objectName: "agentRunActivityBar"
                    Layout.fillWidth: true
                    implicitHeight: 4
                    active: root.activityVisible && root.running && !root.awaitingInput
                    reducedMotion: root.themeObject.reducedMotion
                    opacity: root.awaitingInput ? 0.45 : 1
                    Accessible.name: root.awaitingInput ? "Waiting for your answer" : root.running ? "Agent is working" : "Agent run stopped"
                }
                ActionButton {
                    id: terminalStatusDetail
                    objectName: "agentTerminalFailureMessage"
                    Layout.fillWidth: true
                    implicitHeight: Math.max(24, contentItem.implicitHeight)
                    embedded: true
                    visible: root.awaitingInput || !root.running
                    text: root.awaitingInput ? root.dialog.pendingApprovalQuestion()
                          : root.dialog.hasRunFailure() ? root.dialog.failureDetail() : root.dialog.statusLine()
                    padding: 0
                    focusPolicy: root.dialog.hasRunFailure() ? Qt.StrongFocus : Qt.NoFocus
                    onClicked: if (root.dialog.hasRunFailure()) root.showFailureDetails(this)
                    Accessible.name: root.dialog.hasRunFailure() ? "Show failure details: " + text : text
                    HoverHandler { cursorShape: root.dialog.hasRunFailure() ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    contentItem: TerminalLabel {
                        text: terminalStatusDetail.text
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: root.themeObject.typeBody
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.minimumHeight: root.themeObject.scaled(56)
                    Layout.preferredHeight: Math.max(root.themeObject.scaled(56),
                                                     currentActivityContent.implicitHeight + root.themeObject.scaled(18))
                    color: "transparent"
                    border.width: 0
                    ColumnLayout {
                        id: currentActivityContent
                        anchors.fill: parent
                        anchors.margins: root.themeObject.scaled(9)
                        spacing: root.compact ? 4 : 8
                        TerminalLabel {
                            objectName: "agentTerminalCurrentTaskSummary"
                            Layout.fillWidth: true
                            text: root.currentTaskSummary
                            font.family: root.themeObject.fontUi
                            font.pixelSize: root.themeObject.typeBody
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            Accessible.name: "Current task"
                            Accessible.description: text
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            TerminalButton {
                                objectName: "agentTerminalOutputDetailsButton"
                                visible: root.modelOutputText.length > 0
                                text: root.activityDetailsExpanded ? "Hide details" : "Details"
                                accessibleLabel: root.activityDetailsExpanded
                                                 ? "Hide model output details" : "Show model output details"
                                subtle: true
                                onClicked: root.activityDetailsExpanded = !root.activityDetailsExpanded
                            }
                            TerminalButton {
                                objectName: "agentTerminalHistoryToggle"
                                visible: root.events.length > 1
                                text: root.activityHistoryExpanded ? "Hide history" : "History"
                                accessibleLabel: root.activityHistoryExpanded
                                                 ? "Hide recent activity history" : "Show recent activity history"
                                subtle: true
                                onClicked: root.activityHistoryExpanded = !root.activityHistoryExpanded
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Flickable {
                            id: modelOutputDetailsScroll
                            objectName: "agentTerminalModelOutputDetailsScroll"
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.activityDetailsExpanded
                                                      ? Math.min(root.height * 0.20,
                                                                 root.themeObject.scaled(112),
                                                                 modelOutputDetailsLabel.implicitHeight) : 0
                            visible: root.activityDetailsExpanded && root.modelOutputText.length > 0
                            clip: true
                            contentWidth: width
                            contentHeight: modelOutputDetailsLabel.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            TerminalLabel {
                                id: modelOutputDetailsLabel
                                objectName: "agentTerminalModelOutputDetails"
                                width: modelOutputDetailsScroll.width
                                text: "Model output · " + root.modelOutputText
                                color: root.phosphor
                                font.family: root.themeObject.fontUi
                                font.pixelSize: root.themeObject.typeCaption
                                Accessible.name: "Model output details"
                                Accessible.description: root.modelOutputText
                            }
                        }
                        ListView {
                            id: activityHistoryList
                            objectName: "agentTerminalActivityHistoryList"
                            Layout.fillWidth: true
                            Layout.fillHeight: root.activityHistoryExpanded
                            Layout.preferredHeight: root.activityHistoryExpanded
                                                      ? Math.min(180, activityHistoryList.count * (root.compact ? 34 : 42)) : 0
                            visible: root.activityHistoryExpanded
                            clip: true
                            spacing: 3
                            model: root.activityHistoryRows
                            delegate: ColumnLayout {
                                id: activityHistoryRow
                                required property var modelData
                                required property int index
                                objectName: "agentTerminalActivityHistoryRow-" + index
                                width: ListView.view.width
                                height: root.compact ? 32 : 40
                                spacing: 1
                                TerminalLabel {
                                    Layout.fillWidth: true
                                    text: root.activityEventTitle(activityHistoryRow.modelData) +
                                          " · " + root.activityEventStatus(activityHistoryRow.modelData)
                                    color: root.muted
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                }
                                TerminalLabel {
                                    Layout.fillWidth: true
                                    text: String(activityHistoryRow.modelData.detail ||
                                                 ((activityHistoryRow.modelData.evidence || ({})).modelOutputAvailable
                                                  ? "Model response received" : ""))
                                    visible: text.length > 0
                                    color: root.textColor
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        id: cancelButton
                        objectName: "agentStopRunButton"
                        text: "Stop"
                        enabled: root.running
                        onClicked: { if (root.dialog.hasController()) root.dialog.agentEditController.cancelRun("user_requested") }
                    }
                    Item { Layout.fillWidth: true }
                    TerminalButton {
                        objectName: "agentViewRunningEditorButton"
                        text: root.awaitingInput ? "Answer in editor >" : "View editor >"
                        visible: root.running || root.awaitingInput
                        onClicked: root.dialog.close()
                    }
                    TerminalButton {
                        text: "< Edit request"
                        visible: !root.running && !root.dialog.canReviewDraft()
                        onClicked: root.dialog.setPage(0)
                    }
                    TerminalButton {
                        objectName: "agentTerminalFailureDetailsButton"
                        text: "Details"
                        visible: root.dialog.hasRunFailure()
                        onClicked: root.showFailureDetails(this)
                    }
                    TerminalButton {
                        objectName: "agentRevertTaskButton"
                        text: "Revert task"
                        visible: !root.running && root.dialog.hasController()
                                 && (root.dialog.agentEditController.taskTransaction || {}).revertAvailable === true
                                 && !root.dialog.canReviewDraft()
                        onClicked: {
                            var result = root.dialog.agentEditController.revertTask()
                            if (!result.ok) root.dialog.applyError = result.error || "The task could not be reverted."
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 8
                TerminalLabel { text: root.dialog.editCommitted ? "Edit saved" : "Review edit"; color: root.phosphor; font.pixelSize: root.themeObject.typeHero }
                TerminalLabel { Layout.fillWidth: true; text: root.dialog.planSummary(); maximumLineCount: 3; elide: Text.ElideRight }
                TerminalLabel { Layout.fillWidth: true; text: "Check · " + root.dialog.validationLabel(); color: root.muted; maximumLineCount: 2; elide: Text.ElideRight }
                TerminalLabel {
                    Layout.fillWidth: true
                    text: root.dialog.applyError.length > 0 ? root.dialog.applyError : "Inspect the rendered result in the editor before applying."
                    color: root.dialog.applyError.length > 0 ? root.themeObject.error : root.muted
                    font.pixelSize: root.themeObject.typeCaption
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    TerminalButton { text: "View in editor"; onClicked: root.dialog.close() }
                    TerminalButton { text: "Change request"; visible: !root.dialog.editCommitted; onClicked: root.dialog.reviseDraft() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton { text: "Discard"; visible: !root.dialog.editCommitted; onClicked: root.dialog.rejectDraft() }
                    Item { Layout.fillWidth: true }
                    TerminalButton {
                        objectName: "aiEditPrimaryReviewButton"
                        text: root.dialog.editCommitted ? "Done" : root.dialog.sampleReviewWaiting() ? "Apply to all >" : "Apply edit >"
                        primary: true
                        enabled: root.dialog.editCommitted || (root.dialog.canReviewDraft() && root.dialog.reviewIssues().length === 0 && !root.dialog.directionComparisonPending())
                        onClicked: { if (root.dialog.editCommitted) root.dialog.close(); else root.dialog.acceptDraft() }
                    }
                }
            }

            ColumnLayout {
                spacing: 8
                TerminalLabel { text: "Checked files"; color: root.phosphor; font.pixelSize: root.themeObject.typeDialogTitle }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    Repeater {
                        model: root.checkedAssets.slice(root.targetsPage * root.rowsPerPage, (root.targetsPage + 1) * root.rowsPerPage)
                        delegate: RowLayout {
                            id: checkedRow
                            required property var modelData
                            Layout.fillWidth: true
                            TerminalLabel { text: "[x]"; color: root.phosphor }
                            TerminalLabel {
                                Layout.fillWidth: true
                                text: root.dialog.assetDisplayTitle(checkedRow.modelData)
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                Accessible.name: text
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton { text: "<"; enabled: root.targetsPage > 0; onClicked: root.targetsPage -= 1 }
                    TerminalLabel { Layout.fillWidth: true; text: (root.targetsPage + 1) + " / " + root.targetPageCount; horizontalAlignment: Text.AlignHCenter; color: root.muted }
                    TerminalButton { text: ">"; enabled: root.targetsPage + 1 < root.targetPageCount; onClicked: root.targetsPage += 1 }
                }
                TerminalButton { text: "< Back"; onClicked: { root.panel = "edit"; root.focusPageEntry() } }
            }

            ColumnLayout {
                spacing: 10
                TerminalLabel { text: "Model connection"; color: root.phosphor; font.pixelSize: root.themeObject.typeDialogTitle }
                TerminalLabel { Layout.fillWidth: true; text: String(root.selectedModel.name || "Local vision model"); maximumLineCount: 2; elide: Text.ElideRight }
                TerminalLabel {
                    Layout.fillWidth: true
                    text: root.dialog.hasController() && root.dialog.agentEditController.modelProbeRunning ? "Checking the model in your existing Ollama server..."
                          : root.selectedModel.available ? "Connected. Your model is ready."
                          : root.selectedModel.runtime === "managed" ? "This connection uses Looksy's own model store. You can use your existing Ollama model below."
                          : String(root.selectedModel.statusReason || "Connect your existing Ollama model, or set up a new copy.")
                    color: root.muted
                    font.pixelSize: root.themeObject.typeBody
                    maximumLineCount: 4
                    elide: Text.ElideRight
                }
                TerminalLabel {
                    Layout.fillWidth: true
                    text: "Already have this model? Use the copy in Ollama."
                    color: root.phosphor
                    font.pixelSize: root.themeObject.typeBody
                    visible: Boolean(root.existingOllamaModel.id)
                }
                TerminalButton {
                    id: existingModelButton
                    objectName: "agentUseExistingOllamaButton"
                    Layout.fillWidth: true
                    text: root.dialog.hasController() && root.dialog.agentEditController.modelProbeRunning ? "Checking..." : "Use existing Ollama >"
                    primary: true
                    visible: Boolean(root.existingOllamaModel.id)
                    enabled: root.dialog.hasController() && !root.dialog.agentEditController.modelProbeRunning && !root.dialog.agentEditController.modelInstallRunning
                    onClicked: root.useExistingOllama()
                }
                TerminalButton {
                    objectName: "agentUseFastOllamaButton"
                    Layout.fillWidth: true
                    text: "Use Qwen3.6 35B · 60s limit >"
                    visible: Boolean(root.fastOllamaModel.id)
                    enabled: root.dialog.hasController() && !root.dialog.agentEditController.modelProbeRunning && !root.running
                    onClicked: {
                        root.dialog.agentEditController.selectedModelId = root.fastOllamaModel.id
                        root.dialog.agentEditController.probeSelectedModelAsync(true)
                    }
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton { text: "< Back"; onClicked: { root.panel = "edit"; root.focusPageEntry() } }
                    Item { Layout.fillWidth: true }
                    TerminalButton {
                        text: "Model setup..."
                        enabled: root.dialog.hasController() && !root.dialog.agentEditController.modelProbeRunning
                        onClicked: root.dialog.modelSetupRequested()
                    }
                }
            }
        }
    }
}
