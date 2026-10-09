pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    objectName: "agentTerminalScreen"
    required property var dialog
    required property var themeObject

    readonly property color phosphor: "#EEC58A"
    readonly property color textColor: "#E8E1D3"
    readonly property color muted: "#A49C90"
    readonly property string mono: themeObject.fontMono
    readonly property bool compact: height < 340
    readonly property int rowsPerPage: 3
    readonly property var checkedAssets: dialog.targetAssets()
    readonly property int checkedCount: dialog.scopeCount(dialog.scopeMode)
    property string panel: "edit"
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
        panel = "model"
        Qt.callLater(function() { existingModelButton.forceActiveFocus(Qt.TabFocusReason) })
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
    readonly property string latestOutput: outputRows.length > 0 ? String(outputRows[outputRows.length - 1].text) : dialog.statusLine()
    property int revealedCharacters: 0
    property bool cursorLit: true

    function stageIndex(stage) {
        if (stage === "connect" || stage === "observe") return 0
        if (stage === "plan" || stage.indexOf("plan-worker") === 0 || stage === "constrain" || stage === "fallback") return 1
        if (stage === "act" || stage === "apply" || stage === "render" || stage === "preview" || stage === "repair") return 2
        return 3
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
    onRowsPerPageChanged: {
        archivePage = Math.min(archivePage, archivePageCount - 1)
        targetsPage = Math.min(targetsPage, targetPageCount - 1)
    }

    component TerminalLabel: Label {
        color: root.textColor
        textFormat: Text.PlainText
        font.family: root.mono
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    component TerminalButton: Button {
        id: button
        property bool primary: false
        property bool selected: false
        property string accessibleLabel: text
        implicitHeight: 34
        implicitWidth: label.implicitWidth + 20
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.name: accessibleLabel
        Accessible.role: Accessible.Button
        contentItem: Label {
            id: label
            text: button.text
            color: button.primary ? "#23201B" : root.phosphor
            font.family: root.mono
            font.pixelSize: 11
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 1
            color: button.primary ? root.phosphor : button.hovered || button.selected ? "#343027" : "transparent"
            border.width: button.visualFocus ? 2 : 1
            border.color: button.visualFocus ? root.textColor : root.phosphor
            opacity: button.enabled ? 1 : 0.35
        }
        HoverHandler { cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        TerminalButton {
            objectName: "agentCheckedSelectionAlert"
            Layout.fillWidth: true
            implicitHeight: 32
            accessibleLabel: root.checkedCount + " photos checked. " + root.dialog.targetSummary() + ". Show checked files."
            text: root.checkedCount > 0 ? "[x] " + root.checkedCount + " FILE" + (root.checkedCount === 1 ? "" : "S") +
                  (root.dialog.scopeMode === "project" ? " IN PROJECT" : root.dialog.scopeMode === "filtered-library" ? " IN FILTER" : " CHECKED") +
                  " / " + (root.checkedAssets.length > 0 ? root.dialog.assetDisplayTitle(root.checkedAssets[0]) : "VIEW")
                                        : "[ ] NO FILES CHECKED"
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
                        text: "EDIT REQUEST_"
                        color: root.phosphor
                        font.pixelSize: root.compact ? 18 : 22
                        font.weight: Font.DemiBold
                    }
                    TerminalButton {
                        objectName: "agentChooseModelButton"
                        text: "Model..."
                        onClicked: root.showModelConnection()
                    }
                }
                TerminalLabel {
                    Layout.fillWidth: true
                    text: "Write a prompt. Or use inspiration from your archive."
                    color: root.muted
                    font.pixelSize: 11
                }
                TerminalLabel { text: "PROMPT >"; color: root.phosphor; font.pixelSize: 10 }
                TextField {
                    id: promptEditor
                    objectName: "agentPromptEditor"
                    Layout.fillWidth: true
                    implicitHeight: 52
                    text: root.dialog.terminalPrompt
                    placeholderText: "What should change?"
                    placeholderTextColor: root.muted
                    color: root.textColor
                    selectionColor: root.phosphor
                    selectedTextColor: "#23201B"
                    font.family: root.mono
                    font.pixelSize: 13
                    leftPadding: 12
                    rightPadding: 12
                    activeFocusOnTab: true
                    selectByMouse: true
                    Accessible.name: "AI edit instructions"
                    Accessible.description: "Optional when using saved inspiration. Press Enter to prepare an edit."
                    background: Rectangle {
                        color: "#202124"
                        radius: 1
                        border.width: promptEditor.activeFocus ? 2 : 1
                        border.color: promptEditor.activeFocus ? root.phosphor : "#61584A"
                    }
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
                    text: root.attachedInspiration.length > 0 ? "INSPIRATION / " + root.attachedInspiration : "INSPIRATION / NONE"
                    color: root.attachedInspiration.length > 0 ? root.phosphor : root.muted
                    font.pixelSize: 10
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Accessible.name: text
                }
                TerminalLabel {
                    Layout.fillWidth: true
                    visible: root.dialog.directionError.length > 0
                    text: root.dialog.directionError
                    color: root.themeObject.error
                    font.pixelSize: 10
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TerminalButton {
                        objectName: "agentUseInspirationButton"
                        text: "Use inspiration"
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
                TerminalLabel { text: "PAST INSPIRATION_"; color: root.phosphor; font.pixelSize: 18 }
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
                                        font.pixelSize: 11
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }
                                    TerminalLabel {
                                        Layout.fillWidth: true
                                        text: String(inspirationRow.modelData.detail || "Saved inspiration")
                                        color: root.muted
                                        font.pixelSize: 9
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
                        text: root.awaitingInput ? "INPUT REQUIRED_" : root.running ? "PROCESSING_" : "RUN STATUS_"
                        color: root.phosphor
                        font.pixelSize: root.compact ? 17 : 20
                    }
                    Item { Layout.fillWidth: true }
                    TerminalLabel {
                        objectName: "agentRunElapsed"
                        text: Math.floor(root.dialog.runElapsedSeconds / 60) + ":" + String(root.dialog.runElapsedSeconds % 60).padStart(2, "0")
                        color: root.muted
                        font.pixelSize: 11
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Repeater {
                        model: ["READ", "PLAN", "EDIT", "CHECK"]
                        delegate: Rectangle {
                            id: phase
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            implicitHeight: 25
                            color: root.activeStage === index ? "#393329" : "#202124"
                            border.color: root.activeStage === index ? root.phosphor : "#61584A"
                            TerminalLabel {
                                anchors.centerIn: parent
                                text: (root.activeStage === phase.index && root.running ? "> " : root.stageComplete(phase.index) ? "+ " : "") + phase.modelData
                                font.pixelSize: 10
                                color: root.activeStage === phase.index ? root.phosphor : root.muted
                            }
                        }
                    }
                }
                Rectangle {
                    id: activityTrack
                    objectName: "agentRunActivityBar"
                    Layout.fillWidth: true
                    implicitHeight: 8
                    color: "#202124"
                    border.color: "#61584A"
                    clip: true
                    Accessible.role: Accessible.ProgressBar
                    Accessible.name: root.awaitingInput ? "Waiting for your answer" : root.running ? "Agent is working" : "Agent run stopped"
                    Rectangle {
                        id: activityBeam
                        x: 1
                        y: 1
                        width: root.running ? Math.max(28, activityTrack.width * 0.24) : 0
                        height: activityTrack.height - 2
                        color: root.phosphor
                        opacity: root.awaitingInput ? 0.45 : 0.95
                    }
                    SequentialAnimation {
                        objectName: "agentRunActivityAnimation"
                        running: root.activityVisible && root.running && !root.awaitingInput && !root.themeObject.reducedMotion
                        loops: Animation.Infinite
                        XAnimator { target: activityBeam; from: 1; to: Math.max(1, activityTrack.width - activityBeam.width - 1); duration: 1050; easing.type: Easing.InOutSine }
                        XAnimator { target: activityBeam; from: Math.max(1, activityTrack.width - activityBeam.width - 1); to: 1; duration: 1050; easing.type: Easing.InOutSine }
                    }
                }
                Button {
                    id: terminalStatusDetail
                    objectName: "agentTerminalFailureMessage"
                    Layout.fillWidth: true
                    implicitHeight: Math.max(24, contentItem.implicitHeight)
                    background: Rectangle { color: "transparent" }
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
                        font.pixelSize: 11
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#1B1D20"
                    border.color: "#484238"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: root.compact ? 4 : 8
                        TerminalLabel { text: "LIVE OUTPUT /"; color: root.muted; font.pixelSize: 9 }
                        Repeater {
                            model: root.outputRows
                            delegate: ColumnLayout {
                                id: outputRow
                                required property int index
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                TerminalLabel {
                                    Layout.fillWidth: true
                                    text: String(outputRow.modelData.time).padStart(2, "0") + "s / " + outputRow.modelData.source + " / " + ["READ", "PLAN", "EDIT", "CHECK"][root.stageIndex(outputRow.modelData.key)]
                                    font.pixelSize: 9
                                    color: outputRow.modelData.source === "MODEL" ? root.phosphor : root.muted
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                }
                                TerminalLabel {
                                    objectName: "agentLiveOutput-" + outputRow.index
                                    Layout.fillWidth: true
                                    text: outputRow.index === root.outputRows.length - 1
                                          ? String(outputRow.modelData.text).slice(0, root.themeObject.reducedMotion ? root.latestOutput.length : root.revealedCharacters) +
                                            (root.running && !root.awaitingInput && root.cursorLit ? " ▋" : "")
                                          : outputRow.modelData.text
                                    Accessible.name: outputRow.modelData.text
                                    font.pixelSize: 10
                                    maximumLineCount: root.compact ? 2 : 3
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        TerminalLabel {
                            visible: root.outputRows.length === 0
                            text: root.dialog.statusLine() + (root.cursorLit && root.running ? " ▋" : "")
                            font.pixelSize: 11
                            color: root.phosphor
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
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
                TerminalLabel { text: root.dialog.editCommitted ? "EDIT SAVED_" : "REVIEW EDIT_"; color: root.phosphor; font.pixelSize: 20 }
                TerminalLabel { Layout.fillWidth: true; text: root.dialog.planSummary(); maximumLineCount: 3; elide: Text.ElideRight }
                TerminalLabel { Layout.fillWidth: true; text: "CHECK / " + root.dialog.validationLabel(); color: root.muted; maximumLineCount: 2; elide: Text.ElideRight }
                TerminalLabel {
                    Layout.fillWidth: true
                    text: root.dialog.applyError.length > 0 ? root.dialog.applyError : "Inspect the rendered result in the editor before applying."
                    color: root.dialog.applyError.length > 0 ? root.themeObject.error : root.muted
                    font.pixelSize: 10
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
                TerminalLabel { text: "CHECKED FILES_"; color: root.phosphor; font.pixelSize: 18 }
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
                TerminalLabel { text: "MODEL CONNECTION_"; color: root.phosphor; font.pixelSize: 18 }
                TerminalLabel { Layout.fillWidth: true; text: String(root.selectedModel.name || "Local vision model"); maximumLineCount: 2; elide: Text.ElideRight }
                TerminalLabel {
                    Layout.fillWidth: true
                    text: root.dialog.hasController() && root.dialog.agentEditController.modelProbeRunning ? "Checking the model in your existing Ollama server..."
                          : root.selectedModel.available ? "Connected. Your model is ready."
                          : root.selectedModel.runtime === "managed" ? "This connection uses Looksy's own model store. You can use your existing Ollama model below."
                          : String(root.selectedModel.statusReason || "Connect your existing Ollama model, or set up a new copy.")
                    color: root.muted
                    font.pixelSize: 11
                    maximumLineCount: 4
                    elide: Text.ElideRight
                }
                TerminalLabel {
                    Layout.fillWidth: true
                    text: "Already have this model? Use the copy in Ollama."
                    color: root.phosphor
                    font.pixelSize: 11
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
