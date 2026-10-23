pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    objectName: "agentSessionTerminal"
    clip: true

    required property var dialog
    required property var themeObject
    required property var sessionController

    // The terminal is instantiated alongside the legacy review surface, even
    // when the caller intentionally has no conversational-session controller.
    // Keep its bindings inert in that mode instead of reading properties from
    // null while the legacy surface is active.
    readonly property bool sessionAvailable: sessionController !== null && sessionController !== undefined
    readonly property var controller: sessionAvailable ? sessionController : ({
        stage: "idle",
        brief: ({prompt: "", answers: ({}), context: ({})}),
        plan: ({actions: [], exactCopy: [], deliverables: [], constraints: []}),
        questions: [],
        busy: false,
        canStart: false,
        status: "",
        activity: "",
        canRevert: false,
        revertPending: false,
        revertOwner: ""
    })

    property string subview: ""
    property string inspirationFilter: "all"
    property int inspirationPage: 0
    property int questionPage: 0
    property int planActionPage: 0
    property var rememberedQuestions: []
    property var answerEdits: ({})
    property var firstQuestionField: null
    property string revisionPrompt: ""
    property string actionMessage: ""
    property string latestActivity: ""

    readonly property string stage: String(controller.stage || "idle")
    readonly property var brief: controller.brief || ({})
    readonly property var plan: controller.plan || ({})
    readonly property var planActions: {
        const actions = root.plan.actions || []
        if (typeof actions.length !== "number")
            return []
        const result = []
        for (let index = 0; index < actions.length && result.length < 6; ++index) {
            const action = actions[index] || ({})
            const summary = String(action.summary || "").trim()
            if (summary.length > 0)
                result.push({summary: summary})
        }
        return result
    }
    readonly property int planActionPageSize: 1
    readonly property int planActionPageCount: Math.max(1, Math.ceil(root.planActions.length / root.planActionPageSize))
    readonly property var visiblePlanActions: root.planActions.slice(
        root.planActionPage * root.planActionPageSize,
        root.planActionPage * root.planActionPageSize + root.planActionPageSize)
    readonly property string canvasSettingsSummary: {
        const settings = root.plan.canvasSettings || ({})
        const context = root.brief.context || root.dialog.sessionContext || ({})
        const width = Number(settings.width || 0)
        const height = Number(settings.height || 0)
        if (!(root.brief.blankCanvas || context.blankCanvas) || !isFinite(width) || !isFinite(height) ||
                width <= 0 || height <= 0)
            return ""
        return "CANVAS · " + width + " × " + height
    }
    readonly property var questions: controller.questions || []
    readonly property var activeQuestions: questions.length > 0 ? questions : rememberedQuestions
    readonly property bool busy: Boolean(controller.busy)
    readonly property bool canStart: Boolean(controller.canStart)
    readonly property string statusText: String(controller.status || "")
    readonly property string activityText: latestActivity.length > 0
                                            ? latestActivity
                                            : String(controller.activity || "")
    readonly property bool reducedMotion: Boolean(themeObject.reducedMotion)
    readonly property color paper: "#e8ece9"
    readonly property color muted: "#93a6aa"
    readonly property color amber: "#cdded7"
    readonly property color cyan: "#a5d9e0"
    readonly property color coral: "#d9a08d"
    readonly property color screen: "#111719"
    readonly property color panel: "#172125"
    readonly property color line: "#3b5259"
    readonly property string mono: themeObject.fontMono
    readonly property string uiFont: themeObject.fontUi
    readonly property var allInspiration: dialog.inspirationSuggestions || []
    readonly property var filteredInspiration: allInspiration.filter(function(item) {
        if (root.inspirationFilter === "looks") return String(item.kind || "") === "preset"
        if (root.inspirationFilter === "photos") return String(item.kind || "") === "photo"
        return true
    })
    readonly property int inspirationPageSize: 2
    readonly property int inspirationPageCount: Math.max(1, Math.ceil(filteredInspiration.length / inspirationPageSize))
    readonly property var visibleInspiration: filteredInspiration.slice(
        inspirationPage * inspirationPageSize,
        inspirationPage * inspirationPageSize + inspirationPageSize)
    readonly property int questionsPerPage: 2
    readonly property int questionPageCount: Math.max(1, Math.ceil(activeQuestions.length / questionsPerPage))
    readonly property var visibleQuestions: activeQuestions.slice(
        questionPage * questionsPerPage,
        questionPage * questionsPerPage + questionsPerPage)
    readonly property var runReport: dialog.hasController()
                                       ? (dialog.agentEditController.lastRunReport || ({}))
                                       : ({})
    readonly property bool draftAvailable: dialog.hasController() &&
                                            Boolean(dialog.agentEditController.draftActive) &&
                                            !Boolean(dialog.agentEditController.running)
    readonly property bool failureRevertAvailable: (root.stage === "error" || root.stage === "interrupted") &&
                                                     Boolean(root.controller.canRevert)
    readonly property bool failureRevertPending: root.busy || Boolean(root.controller.revertPending) ||
                                                  (dialog.hasController() && Boolean(dialog.agentEditController.running))
    readonly property string failureRevertOwner: String(root.controller.revertOwner || "")
    readonly property bool failureDetailsOpen: failureDetails.visible

    Accessible.role: Accessible.Pane
    Accessible.name: "Agent conversation"

    AgentFailureDetails {
        id: failureDetails
    }

    function briefText(key) {
        return String((root.brief || ({}))[key] || "")
    }

    function suggestedCopyNeedsReview() {
        if (Boolean(root.plan.copySuggestionApprovalRequired))
            return true
        const items = root.plan.exactCopy || []
        return items.some(function(item) {
            return String((item || ({})).source || "") === "proposed" || Boolean((item || ({})).needsApproval)
        })
    }

    function exactCopyLabel() {
        return root.suggestedCopyNeedsReview() ? "SUGGESTED COPY · REVIEW BEFORE START" : "EXACT COPY"
    }

    function exactCopyText() {
        return (root.plan.exactCopy || []).slice(0, 2).map(function(item) {
            const source = String((item || ({})).source || "")
            const prefix = source === "proposed" || Boolean((item || ({})).needsApproval)
                           ? "Suggested: " : ""
            return prefix + "“" + String((item || ({})).text || "") + "”"
        }).join("   ·   ")
    }

    function targetSummary() {
        const ctx = root.brief.context || root.dialog.sessionContext || ({})
        const snapshot = root.brief.targetSnapshot || ctx.targetSnapshot || []
        if (Array.isArray(snapshot) && snapshot.length > 0) {
            const labels = snapshot.slice(0, 2).map(function(item) {
                return String((item || ({})).title || (item || ({})).name || (item || ({})).label || item || "")
            }).filter(function(label) { return label.length > 0 })
            if (labels.length > 0)
                return labels.join(" · ") + (snapshot.length > 2 ? " +" + (snapshot.length - 2) : "")
        }
        if (Boolean(root.brief.blankCanvas || ctx.blankCanvas))
            return "New blank canvas"
        const count = (root.brief.targetIds || ctx.targetIds || []).length
        if (count > 0)
            return count + " selected target" + (count === 1 ? "" : "s")
        if (root.dialog && typeof root.dialog.targetSummary === "function") {
            const summary = String(root.dialog.targetSummary() || "")
            if (summary.length > 0 && summary !== "No current photo")
                return summary
        }
        return "No source selected · describe what to create"
    }

    function selectedSuggestions() {
        return root.allInspiration.filter(function(item) { return root.dialog.suggestionSelected(item) })
    }

    function selectedInspirationIds() {
        return root.selectedSuggestions().map(function(item) { return String(item.id || "") }).filter(function(id) { return id.length > 0 })
    }

    function sessionContext() {
        const provided = root.dialog.sessionContext || ({})
        const context = Object.assign({}, provided)
        const currentIds = typeof root.dialog.targetAssetIds === "function"
                           ? root.dialog.targetAssetIds() : []
        // The current workspace selection is authoritative. The injected context
        // supplies project/model metadata, while these values are recomputed at
        // every submit and selection notification so Start cannot approve stale targets.
        context.targetIds = currentIds.slice()
        context.assetIds = currentIds.slice()
        context.currentAssetIds = currentIds.slice()
        context.scope = String(root.dialog.scopeMode || context.scope || "current-photo")
        context.blankCanvas = currentIds.length === 0
        context.targetSummary = context.blankCanvas
                                  ? "New blank canvas"
                                  : String(root.dialog.targetSummary() || "Selected targets")
        const selected = root.selectedSuggestions()
        context.inspirationIds = selected.map(function(item) { return String(item.id || "") })
        context.inspirationKinds = selected.map(function(item) { return String(item.kind || "") })
        context.inspirationReferences = selected.filter(function(item) { return String(item.kind || "") === "photo" })
                                                    .map(function(item) { return String(item.id || "") })
        context.selectedStyles = selected.filter(function(item) { return String(item.kind || "") === "preset" })
                                          .map(function(item) { return String(item.id || "") })
        if (root.dialog.hasController())
            context.selectedModelId = String(root.dialog.agentEditController.selectedModelId || "")
        return context
    }

    function syncContext() {
        if (root.sessionAvailable && typeof root.controller.updateContext === "function")
            root.controller.updateContext(root.sessionContext())
    }

    function prepareInspiration() {
        if (!root.dialog.inspirationSuggestions || root.dialog.inspirationSuggestions.length === 0)
            root.dialog.inspirationSuggestions = root.dialog.savedInspirationSuggestions()
        root.inspirationFilter = "all"
        root.inspirationPage = 0
        root.subview = "inspiration"
        root.syncContext()
    }

    function showTargets() {
        root.subview = "targets"
    }

    function chooseScope(index) {
        if (typeof root.dialog.chooseAvailableScope === "function")
            root.dialog.chooseAvailableScope(index)
        root.syncContext()
        root.subview = ""
        root.actionMessage = "The target changed. The plan will be refreshed before Start."
    }

    function submitPrompt() {
        if (!root.sessionAvailable)
            return
        const selected = root.selectedSuggestions()
        let prompt = String(root.dialog.terminalPrompt || "").trim()
        if (prompt.length === 0 && selected.length > 0)
            prompt = "Use the selected inspiration to guide the result."
        if (prompt.length === 0)
            return
        root.dialog.terminalPrompt = prompt
        root.actionMessage = ""
        root.syncContext()
        root.controller.submit(prompt, root.sessionContext())
        root.subview = ""
    }

    function retryPlanning() {
        if (!root.sessionAvailable)
            return
        root.actionMessage = ""
        root.subview = ""
        if (typeof root.controller.retryPlanning === "function")
            root.controller.retryPlanning()
        else
            root.controller.submit(String(root.brief.prompt || root.dialog.terminalPrompt || ""), root.sessionContext())
    }

    function answerValue(question) {
        const id = String((question || ({})).id || "")
        if (Object.prototype.hasOwnProperty.call(root.answerEdits, id))
            return String(root.answerEdits[id] || "")
        return String((root.brief.answers || ({}))[id] || "")
    }

    function setAnswer(id, value) {
        const next = Object.assign({}, root.answerEdits)
        next[String(id)] = String(value || "")
        root.answerEdits = next
        root.actionMessage = ""
    }

    function mergeRememberedQuestions(items) {
        const merged = root.rememberedQuestions.slice()
        for (const question of items) {
            const id = String(question.id || "")
            const existingIndex = merged.findIndex(function(item) { return String(item.id || "") === id })
            if (existingIndex >= 0)
                merged[existingIndex] = question
            else if (id.length > 0)
                merged.push(question)
        }
        root.rememberedQuestions = merged
    }

    function answerMap() {
        return Object.assign({}, root.brief.answers || ({}), root.answerEdits)
    }

    function advanceQuestions() {
        if (!root.sessionAvailable)
            return
        const visibleAnswers = {}
        for (const question of root.visibleQuestions) {
            const id = String(question.id || "")
            const value = root.answerValue(question).trim()
            if (Boolean(question.required) && value.length === 0) {
                root.actionMessage = "Answer the required detail to continue."
                return
            }
            visibleAnswers[id] = value
        }
        if (root.questionPage + 1 < root.questionPageCount) {
            root.questionPage += 1
            if (root.stage === "clarifying")
                root.controller.answerQuestions(visibleAnswers)
            return
        }
        const answers = root.answerMap()
        root.actionMessage = ""
        if (root.stage === "plan-ready" && typeof root.controller.reviseAnswers === "function") {
            root.controller.reviseAnswers(answers)
        } else {
            root.controller.answerQuestions(visibleAnswers)
        }
        root.subview = ""
    }

    function planWithDefaults() {
        if (!root.sessionAvailable || typeof root.controller.planWithDefaults !== "function")
            return
        root.actionMessage = ""
        root.controller.planWithDefaults(root.answerMap())
        root.subview = ""
    }

    function openQuestions() {
        root.questionPage = 0
        root.subview = "questions"
    }

    function updatePlanContext() {
        root.syncContext()
        root.subview = ""
    }

    function startApprovedPlan() {
        if (!root.sessionAvailable)
            return
        if (!root.canStart || root.stage !== "plan-ready")
            return
        root.actionMessage = ""
        root.controller.start()
    }

    function controlError(report, fallback) {
        const result = report || ({})
        const error = result.error
        if (typeof error === "string" && error.trim().length > 0)
            return error.trim()
        if (error && typeof error === "object") {
            const detail = String(error.message || error.detail || error.summary || error.code || "").trim()
            if (detail.length > 0)
                return detail
        }
        const errors = result.errors || []
        if (errors.length > 0) {
            const first = errors[0]
            if (first && typeof first === "object") {
                const detail = String(first.message || first.detail || first.summary || first.code || "").trim()
                if (detail.length > 0)
                    return detail
            }
            const detail = String(first || "").trim()
            if (detail.length > 0)
                return detail
        }
        if (Boolean(result.pending))
            return "The request is waiting for the task owner to settle safely."
        return fallback
    }

    function stopWork() {
        if (!root.sessionAvailable)
            return
        root.actionMessage = ""
        if (root.stage === "planning" && typeof root.controller.cancelPlanning === "function") {
            root.controller.cancelPlanning()
            return
        }
        if (typeof root.controller.requestStop === "function") {
            const report = root.controller.requestStop() || ({})
            if (!Boolean(report.ok))
                root.actionMessage = root.controlError(report, "Stop was requested; waiting for the task owner to settle.")
            return
        }
        if (root.dialog.hasController() && Boolean(root.dialog.agentEditController.running)) {
            const report = root.dialog.agentEditController.cancelRun("user_requested") || ({})
            if (!Boolean(report.ok))
                root.actionMessage = root.controlError(report, "Stop was requested; waiting for the task owner to settle.")
            return
        }
        root.actionMessage = "There is no active task to stop."
    }

    function revertWork() {
        if (!root.sessionAvailable)
            return
        root.actionMessage = ""
        if (typeof root.controller.requestRevert === "function") {
            const report = root.controller.requestRevert() || ({})
            if (!Boolean(report.ok))
                root.actionMessage = root.controlError(report, "The task owner could not restore this work yet.")
            return
        }
        if (root.dialog.hasController()) {
            const report = root.dialog.agentEditController.revertTask() || ({})
            if (!Boolean(report.ok))
                root.actionMessage = root.controlError(report, "The task owner could not restore this work yet.")
            return
        }
        root.actionMessage = "There is no reversible task available."
    }

    function reviseResult() {
        if (!root.sessionAvailable)
            return
        const prompt = root.revisionPrompt.trim()
        if (prompt.length === 0)
            return
        root.dialog.terminalPrompt = prompt
        root.syncContext()
        root.actionMessage = ""
        root.controller.submit(prompt, root.sessionContext())
        root.subview = ""
    }

    function showFailureDetails(opener) {
        failureDetails.openFor(root.runReport, root.statusText || root.activityText, opener)
    }

    function focusPageEntry() {
        if (root.subview === "inspiration") {
            inspirationBackButton.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        if (root.subview === "targets") {
            targetBackButton.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        if (root.subview === "questions") {
            questionBackButton.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        if (root.stage === "idle" || root.stage === "error" || root.stage === "interrupted") {
            promptEditor.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        if (root.stage === "clarifying") {
            if (root.firstQuestionField && root.firstQuestionField.visible)
                root.firstQuestionField.forceActiveFocus(Qt.TabFocusReason)
            else
                questionBackButton.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        if (root.stage === "plan-ready") {
            startButton.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        if (root.stage === "review") {
            revisionField.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        if (root.stage === "executing" || root.stage === "planning")
            stopButton.forceActiveFocus(Qt.TabFocusReason)
    }

    onStageChanged: {
        root.actionMessage = ""
        root.subview = ""
        if (root.stage === "idle") {
            root.rememberedQuestions = []
            root.answerEdits = ({})
        }
        if (root.stage === "clarifying") {
            root.questionPage = Math.min(root.questionPage, root.questionPageCount - 1)
            Qt.callLater(function() { root.focusPageEntry() })
        }
        if (root.stage === "review")
            root.revisionPrompt = ""
        if (root.stage === "plan-ready")
            root.planActionPage = 0
    }
    onPlanChanged: root.planActionPage = 0
    onQuestionsChanged: {
        if (root.questions.length > 0)
            root.mergeRememberedQuestions(root.questions)
        root.questionPage = 0
        root.firstQuestionField = null
    }
    onFilteredInspirationChanged: root.inspirationPage = Math.min(root.inspirationPage, root.inspirationPageCount - 1)

    Connections {
        target: root.sessionController
        function onActivityReceived(message) { root.latestActivity = String(message || "") }
        function onActivityChanged() { root.latestActivity = String(root.controller.activity || "") }
    }

    Connections {
        target: root.dialog
        function onScopeModeChanged() { root.syncContext() }
        function onSessionContextChanged() { root.syncContext() }
        function onSelectedSuggestionKeysChanged() { root.syncContext() }
        function onConfirmedTargetIdsChanged() { root.syncContext() }
    }

    Connections {
        target: root.dialog.catalogModel
        ignoreUnknownSignals: true
        function onSelectionChanged() { root.syncContext() }
        function onSelectedAssetChanged() { root.syncContext() }
        function onFiltersChanged() { root.syncContext() }
        function onAssetCountChanged() { root.syncContext() }
    }

    component TerminalButton: Button {
        id: button
        property bool primary: false
        property bool subtle: false
        property string accessibleLabel: button.text
        implicitHeight: 32
        implicitWidth: Math.max(84, buttonLabel.implicitWidth + 24)
        leftPadding: 11
        rightPadding: 11
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.name: button.accessibleLabel
        Accessible.role: Accessible.Button
        contentItem: Label {
            id: buttonLabel
            text: button.text
            color: button.primary ? root.screen : button.subtle ? root.muted : root.paper
            font.family: root.mono
            font.pixelSize: 10
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 3
            color: button.primary ? root.amber : button.hovered ? "#28383b" : "#1b292d"
            border.width: button.visualFocus ? 2 : 1
            border.color: button.visualFocus ? root.paper : button.primary ? "#dbe9e3" : root.line
            opacity: button.enabled ? 1 : 0.42
            Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 110 } }
        }
        HoverHandler { cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor }
    }

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.screen
        border.width: 1
        border.color: "#4b6574"
    }

    // Low-contrast phosphor texture; the content remains crisp and fully readable.
    Repeater {
        model: Math.max(0, Math.floor(root.height / 6))
        delegate: Rectangle {
            required property int index
            x: 1
            y: index * 6 + 3
            width: root.width - 2
            height: 1
            color: "#9edbe8"
            opacity: 0.025
            Accessible.ignored: true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 7

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            spacing: 7

            Rectangle {
                implicitWidth: 7
                implicitHeight: 7
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                radius: 3.5
                color: root.stage === "error" ? root.coral
                      : root.stage === "review" ? root.cyan
                      : root.busy || root.stage === "executing" ? root.amber : root.muted
                Layout.alignment: Qt.AlignVCenter
            }

            Label {
                Layout.fillWidth: true
                text: root.subview === "inspiration" ? "PAST LOOKS / REFERENCES"
                      : root.subview === "targets" ? "WORKSPACE TARGET"
                      : root.subview === "questions" ? "A FEW DETAILS"
                      : root.stage === "planning" ? "PREPARING PLAN"
                      : root.stage === "plan-ready" ? "READY TO START"
                      : root.stage === "executing" ? "WORKING"
                      : root.stage === "review" ? "RESULT"
                      : root.stage === "interrupted" ? "PAUSED"
                      : root.stage === "error" ? "NEEDS ATTENTION" : "NEW REQUEST"
                color: root.cyan
                font.family: root.mono
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
                elide: Text.ElideRight
            }

            TerminalButton {
                objectName: "agentSessionDetailsButton"
                visible: root.stage === "error" || root.stage === "interrupted"
                text: "Details"
                subtle: true
                onClicked: root.showFailureDetails(this)
            }
        }

        StackLayout {
            id: pages
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            currentIndex: root.subview === "prompt" ? 0
                          : root.subview === "inspiration" ? 1
                          : root.subview === "targets" ? 2
                          : root.subview === "questions" || root.stage === "clarifying" ? 3
                          : root.stage === "planning" ? 4
                          : root.stage === "plan-ready" ? 5
                          : root.stage === "executing" ? 6
                          : root.stage === "review" ? 7
                          : root.stage === "error" || root.stage === "interrupted" ? 8 : 0

            // Request
            ColumnLayout {
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: "What would you like to make?"
                    color: root.paper
                    font.family: root.uiFont
                    font.pixelSize: root.width < 450 ? 16 : 19
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: Math.max(0, root.width - 96)
                        Layout.preferredWidth: Math.max(0, root.width - 96)
                        implicitHeight: 28
                        radius: 3
                        color: "#1b2934"
                        border.width: 1
                        border.color: "#3b5663"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 7
                            spacing: 6
                            Label {
                                text: "TARGET"
                                color: root.amber
                                font.family: root.mono
                                font.pixelSize: 8
                                font.weight: Font.Bold
                            }
                            Label {
                                id: targetSummaryLabel
                                objectName: "agentSessionTargetSummary"
                                Layout.fillWidth: true
                                text: root.targetSummary()
                                color: root.paper
                                font.family: root.uiFont
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }
                    TerminalButton {
                        objectName: "agentSessionChangeTargetButton"
                        text: "Change"
                        subtle: true
                        Layout.minimumWidth: 70
                        Layout.preferredWidth: 70
                        Layout.maximumWidth: 70
                        onClicked: root.showTargets()
                    }
                }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: root.width - 20
                        Layout.preferredHeight: Math.max(120, Math.min(190, root.height * 0.48))
                        Layout.minimumHeight: 92
                        Layout.maximumHeight: 190
                    radius: 3
                    color: root.panel
                    border.width: promptEditor.activeFocus ? 2 : 1
                    border.color: promptEditor.activeFocus ? root.cyan : "#3b4d5d"

                    TextArea {
                        id: promptEditor
                        objectName: "agentSessionPromptEditor"
                        anchors.fill: parent
                        anchors.margins: 8
                        text: root.dialog.terminalPrompt
                        placeholderText: "A YouTube thumbnail with bold title text, warm light, and room for the subject…"
                        placeholderTextColor: "#8f9ba3"
                        color: root.paper
                        selectionColor: root.cyan
                        selectedTextColor: root.screen
                        font.family: root.uiFont
                        font.pixelSize: 12
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        activeFocusOnTab: true
                        background: Item {}
                        Accessible.name: "Describe what you want the agent to make"
                        onTextChanged: {
                            if (root.dialog.terminalPrompt !== text)
                                root.dialog.terminalPrompt = text
                        }
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Escape) {
                                event.accepted = false
                                return
                            }
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) &&
                                    (event.modifiers & Qt.ControlModifier)) {
                                root.submitPrompt()
                                event.accepted = true
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    TerminalButton {
                        id: inspirationButton
                        objectName: "agentSessionUseInspirationButton"
                        text: "Use inspiration"
                        subtle: true
                        onClicked: root.prepareInspiration()
                    }
                    Label {
                        id: requestMessage
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: root.width < 520 ? 180 : root.width
                        text: root.selectedSuggestions().length > 0
                              ? root.selectedSuggestions().length + " selected · references stay separate from targets"
                              : "Optional · your words are enough"
                        color: root.muted
                        font.family: root.uiFont
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                    TerminalButton {
                        id: submitButton
                        objectName: "agentSessionSubmitButton"
                        text: "Continue"
                        primary: true
                        enabled: root.dialog.terminalPrompt.trim().length > 0 || root.selectedSuggestions().length > 0
                        onClicked: root.submitPrompt()
                    }
                }

                Label {
                    objectName: "agentSessionActionMessage"
                    Layout.fillWidth: true
                    visible: root.actionMessage.length > 0
                    text: root.actionMessage
                    color: root.coral
                    font.family: root.uiFont
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }
            }

            // Inspiration replaces the request page and returns to it unchanged.
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        id: inspirationBackButton
                        objectName: "agentSessionInspirationBackButton"
                        text: "Back"
                        subtle: true
                        onClicked: root.subview = ""
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Choose saved looks or past photos as references."
                        color: root.muted
                        font.family: root.uiFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [
                            {key: "all", label: "All"},
                            {key: "looks", label: "Saved looks"},
                            {key: "photos", label: "Past photos"}
                        ]
                        delegate: TerminalButton {
                            id: filterButton
                            required property var modelData
                            objectName: "agentSessionInspirationFilter-" + String(modelData.key || "all")
                            text: modelData.label
                            subtle: true
                            Layout.minimumWidth: 48
                            Layout.preferredWidth: modelData.key === "all" ? 52 : 94
                            Layout.maximumWidth: modelData.key === "all" ? 52 : 94
                            enabled: root.filteredInspiration.length > 0 || modelData.key === "all"
                            onClicked: {
                                root.inspirationFilter = modelData.key
                                root.inspirationPage = 0
                            }
                            background: Rectangle {
                                radius: 3
                                color: root.inspirationFilter === filterButton.modelData.key ? "#334653" : "#1b292d"
                                border.width: filterButton.visualFocus ? 2 : 1
                                border.color: filterButton.visualFocus ? root.paper : root.line
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    TerminalButton {
                        objectName: "agentSessionInspirationPreviousButton"
                        text: "‹"
                        accessibleLabel: "Previous inspiration page"
                        Layout.minimumWidth: 40
                        Layout.preferredWidth: 40
                        Layout.maximumWidth: 40
                        enabled: root.inspirationPage > 0
                        onClicked: root.inspirationPage -= 1
                    }
                    TerminalButton {
                        objectName: "agentSessionInspirationNextButton"
                        text: "›"
                        accessibleLabel: "Next inspiration page"
                        Layout.minimumWidth: 40
                        Layout.preferredWidth: 40
                        Layout.maximumWidth: 40
                        enabled: root.inspirationPage + 1 < root.inspirationPageCount
                        onClicked: root.inspirationPage += 1
                    }
                }
                RowLayout {
                    id: inspirationCards
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 80
                    Layout.preferredHeight: Math.max(96, root.height - 140)
                    Layout.maximumHeight: root.height
                    spacing: 8
                    Repeater {
                        id: inspirationRepeater
                        model: root.visibleInspiration
                        delegate: Button {
                            id: inspirationCard
                            required property var modelData
                            objectName: "agentSessionInspiration-" + String(modelData.id || "item")
                            property bool chosen: root.dialog.suggestionSelected(modelData)
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            activeFocusOnTab: true
                            hoverEnabled: true
                            Accessible.role: Accessible.CheckBox
                            Accessible.name: String(modelData.title || "Saved inspiration")
                            Accessible.description: String(modelData.kind || "") === "photo"
                                                    ? "Reference photo. It will guide the task and will not be edited."
                                                    : "Saved look that can guide this task."
                            Accessible.checked: chosen
                            onClicked: {
                                root.dialog.toggleSuggestion(modelData)
                                root.syncContext()
                            }
                            background: Rectangle {
                                radius: 3
                                color: inspirationCard.chosen ? "#263945" : "#1a252f"
                                border.width: inspirationCard.visualFocus ? 2 : inspirationCard.chosen ? 1 : 0
                                border.color: inspirationCard.visualFocus ? root.paper : root.cyan
                            }
                            contentItem: ColumnLayout {
                                spacing: 5
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 40
                                    clip: true
                                    Image {
                                        id: inspirationPhotoImage
                                        anchors.fill: parent
                                        visible: String(inspirationCard.modelData.kind || "") === "photo" &&
                                                 String(inspirationCard.modelData.thumbnailSource || "").length > 0
                                        source: root.dialog.normalizeImageSource(inspirationCard.modelData.thumbnailSource || "")
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        visible: !inspirationPhotoImage.visible
                                        color: String(inspirationCard.modelData.kind || "") === "photo" ? "#253c47" : "#443b32"
                                        Label {
                                            anchors.centerIn: parent
                                            text: String(inspirationCard.modelData.kind || "") === "photo" ? "REFERENCE" : "SAVED LOOK"
                                            color: String(inspirationCard.modelData.kind || "") === "photo" ? root.cyan : root.amber
                                            font.family: root.mono
                                            font.pixelSize: 8
                                            font.weight: Font.Bold
                                        }
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: String(inspirationCard.modelData.title || "Inspiration")
                                    color: root.paper
                                    font.family: root.uiFont
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: String(inspirationCard.modelData.kind || "") === "photo"
                                          ? "Reference only · not an edit target"
                                          : String(inspirationCard.modelData.detail || "Saved starting look")
                                    color: root.muted
                                    font.family: root.uiFont
                                    font.pixelSize: 8
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                    Label {
                        visible: root.visibleInspiration.length === 0
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: "No saved examples yet. You can continue with your own words."
                        color: root.muted
                        font.family: root.uiFont
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        objectName: "agentSessionInspirationDoneButton"
                        text: "Use selected"
                        primary: true
                        onClicked: {
                            root.syncContext()
                            root.subview = ""
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: root.selectedSuggestions().length > 0
                              ? root.selectedSuggestions().length + " reference item" + (root.selectedSuggestions().length === 1 ? "" : "s") + " selected"
                              : "Nothing selected · continue without inspiration"
                        color: root.muted
                        font.family: root.uiFont
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }
            }

            // Target scope is intentionally separate from task type.
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        id: targetBackButton
                        objectName: "agentSessionTargetBackButton"
                        text: "Back"
                        subtle: true
                        onClicked: root.subview = ""
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Choose where the result belongs. Your request and answers stay here."
                        color: root.muted
                        font.family: root.uiFont
                        font.pixelSize: 9
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: 2
                    rowSpacing: 6
                    columnSpacing: 6
                    Repeater {
                        model: root.dialog.availableScopeTiles().filter(function(item) { return Number(item.count || 0) > 0 })
                        delegate: TerminalButton {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 48
                            text: String(modelData.title || modelData.key || "Target") + " · " + String(modelData.count || 0)
                            accessibleLabel: String(modelData.title || modelData.key || "Target") + ". " + String(modelData.detail || "")
                            subtle: true
                            onClicked: root.chooseScope(root.dialog.availableScopeTiles().findIndex(function(item) {
                                return String(item.key || "") === String(modelData.key || "")
                            }))
                        }
                    }
                    Label {
                        visible: root.dialog.availableScopeTiles().filter(function(item) { return Number(item.count || 0) > 0 }).length === 0
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: root.targetSummary() + ". You can still describe a new blank canvas in your request."
                        color: root.muted
                        font.family: root.uiFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // One or two questions per screen; earlier prompt and answers remain in the brief.
            ColumnLayout {
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        id: questionBackButton
                        objectName: "agentSessionQuestionBackButton"
                        text: "Request"
                        subtle: true
                        onClicked: root.subview = "prompt"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.questions.length + " detail" + (root.questions.length === 1 ? "" : "s") + " · " + (root.questionPage + 1) + "/" + root.questionPageCount
                        color: root.muted
                        font.family: root.mono
                        font.pixelSize: 9
                    }
                    TerminalButton {
                        objectName: "agentSessionQuestionChangeTargetButton"
                        text: "Target"
                        subtle: true
                        onClicked: root.showTargets()
                    }
                }
                ColumnLayout {
                    id: questionList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    Repeater {
                        model: root.visibleQuestions
                        delegate: ColumnLayout {
                            id: questionItem
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 3
                            Component.onCompleted: {
                                if (index === 0)
                                    root.firstQuestionField = String(modelData.inputType || "text") === "choice"
                                                               ? questionChoice : answerEditor
                            }
                            Label {
                                Layout.fillWidth: true
                                text: String(questionItem.modelData.prompt || "") + (Boolean(questionItem.modelData.required) ? " *" : "")
                                color: root.paper
                                font.family: root.uiFont
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            TextField {
                                id: answerEditor
                                objectName: "agentSessionAnswerTextField-" + String(questionItem.modelData.id || "question")
                                visible: String(questionItem.modelData.inputType || "text") !== "choice"
                                Layout.fillWidth: true
                                implicitHeight: 30
                                text: root.answerValue(questionItem.modelData)
                                placeholderText: "Your answer, or use my judgment"
                                placeholderTextColor: "#8f9ba3"
                                color: root.paper
                                selectionColor: root.cyan
                                font.family: root.uiFont
                                font.pixelSize: 10
                                selectByMouse: true
                                activeFocusOnTab: true
                                onTextChanged: root.setAnswer(questionItem.modelData.id, text)
                                background: Rectangle {
                                    radius: 3
                                    color: root.panel
                                    border.width: parent.activeFocus ? 2 : 1
                                    border.color: parent.activeFocus ? root.cyan : root.line
                                }
                            }
                            ComboBox {
                                id: questionChoice
                                visible: String(questionItem.modelData.inputType || "text") === "choice"
                                Layout.fillWidth: true
                                implicitHeight: 30
                                model: questionItem.modelData.options || []
                                textRole: "label"
                                currentIndex: {
                                    const answer = root.answerValue(questionItem.modelData)
                                    const options = questionItem.modelData.options || []
                                    return options.findIndex(function(option) {
                                        return String(option.id || "") === answer || String(option.label || "") === answer
                                    })
                                }
                                onActivated: function(index) {
                                    const options = questionItem.modelData.options || []
                                    if (index >= 0 && index < options.length)
                                        root.setAnswer(questionItem.modelData.id, String(options[index].id || options[index].label || ""))
                                }
                                contentItem: Label {
                                    leftPadding: 8
                                    text: questionChoice.currentText || "Choose an option"
                                    color: root.paper
                                    font.family: root.uiFont
                                    font.pixelSize: 10
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    radius: 3
                                    color: root.panel
                                    border.width: questionChoice.visualFocus ? 2 : 1
                                    border.color: questionChoice.visualFocus ? root.cyan : root.line
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        objectName: "agentSessionQuestionPreviousButton"
                        text: "Previous"
                        subtle: true
                        visible: root.questionPage > 0
                        onClicked: root.questionPage -= 1
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.actionMessage
                        color: root.coral
                        font.family: root.uiFont
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                    TerminalButton {
                        objectName: "agentSessionQuestionDefaultsButton"
                        text: "Use your judgment"
                        subtle: true
                        visible: typeof root.controller.planWithDefaults === "function"
                        Accessible.description: "Skip unanswered details and review a plan with editable defaults and placeholders."
                        onClicked: root.planWithDefaults()
                    }
                    TerminalButton {
                        objectName: "agentSessionQuestionContinueButton"
                        text: root.questionPage + 1 < root.questionPageCount ? "Next" : "Continue"
                        primary: true
                        onClicked: root.advanceQuestions()
                    }
                }
            }

            // Planning status comes from the session owner; there is no invented percent.
            ColumnLayout {
                spacing: 9
                Label {
                    Layout.fillWidth: true
                    text: "Preparing a plan"
                    color: root.paper
                    font.family: root.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    objectName: "agentSessionPlanningStatus"
                    Layout.fillWidth: true
                    text: root.statusText.length > 0 ? root.statusText : "Reading the request and workspace context."
                    color: root.muted
                    font.family: root.uiFont
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        objectName: "agentSessionCancelPlanningButton"
                        text: "Stop planning"
                        subtle: true
                        onClicked: root.stopWork()
                    }
                    Label { Layout.fillWidth: true; text: "No edits happen until you approve Start."; color: root.muted; font.family: root.uiFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
                }
            }

            // The approved outcome and exact copy are visible before Start.
            ColumnLayout {
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: "Here’s the plan"
                        color: root.paper
                        font.family: root.uiFont
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    TerminalButton {
                        objectName: "agentSessionPlanChangeTargetButton"
                        text: "Target"
                        subtle: true
                        onClicked: root.showTargets()
                    }
                }
                Label {
                    objectName: "agentSessionPlanSummary"
                    Layout.fillWidth: true
                    text: String(root.plan.summary || root.plan.outcome || "").trim()
                    color: root.paper
                    font.family: root.uiFont
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                ColumnLayout {
                    objectName: "agentSessionPlanActionList"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 0
                    spacing: 3
                    visible: root.planActions.length > 0
                    Label {
                        Layout.fillWidth: true
                        text: "WHAT I’LL DO"
                        color: root.cyan
                        font.family: root.mono
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                    Repeater {
                        model: root.visiblePlanActions
                        delegate: RowLayout {
                            id: planActionRow
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.minimumHeight: 0
                            spacing: 5
                            Label {
                                text: "›"
                                color: root.amber
                                font.family: root.mono
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                            Label {
                                objectName: "agentSessionPlanActionSummary-" +
                                            (root.planActionPage * root.planActionPageSize + planActionRow.index)
                                Layout.fillWidth: true
                                text: String((planActionRow.modelData || ({})).summary || "").trim()
                                color: root.paper
                                font.family: root.uiFont
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                Accessible.name: text
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.planActionPageCount > 1
                        Label {
                            Layout.fillWidth: true
                            text: "More steps · " + (root.planActionPage + 1) + "/" + root.planActionPageCount
                            color: root.muted
                            font.family: root.mono
                            font.pixelSize: 10
                        }
                        TerminalButton {
                            objectName: "agentSessionPlanActionPreviousButton"
                            text: "Back"
                            subtle: true
                            enabled: root.planActionPage > 0
                            onClicked: root.planActionPage = Math.max(0, root.planActionPage - 1)
                        }
                        TerminalButton {
                            objectName: "agentSessionPlanActionNextButton"
                            text: "More"
                            subtle: true
                            enabled: root.planActionPage + 1 < root.planActionPageCount
                            onClicked: root.planActionPage = Math.min(root.planActionPageCount - 1, root.planActionPage + 1)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: "TARGET · " + root.targetSummary()
                        color: root.cyan
                        font.family: root.mono
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: "agentSessionPlanCanvasSummary"
                        Layout.preferredWidth: visible ? implicitWidth : 0
                        visible: root.canvasSettingsSummary.length > 0
                        text: root.canvasSettingsSummary
                        color: root.amber
                        font.family: root.mono
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
                Label {
                    objectName: "agentSessionExactCopyReviewLabel"
                    visible: (root.plan.exactCopy || []).length > 0
                    text: root.exactCopyLabel()
                    color: root.suggestedCopyNeedsReview() ? root.coral : root.amber
                    font.family: root.mono
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
                Label {
                    objectName: "agentSessionPlanExactCopy"
                    Layout.fillWidth: true
                    visible: (root.plan.exactCopy || []).length > 0
                    text: root.exactCopyText()
                    color: root.paper
                    font.family: root.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Label {
                    objectName: "agentSessionPlanDeliverables"
                    Layout.fillWidth: true
                    visible: (root.plan.deliverables || []).length > 0
                    text: "DELIVERABLES · " + (root.plan.deliverables || []).slice(0, 3).join(" · ")
                    color: root.muted
                    font.family: root.uiFont
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Label {
                    objectName: "agentSessionPlanConstraints"
                    Layout.fillWidth: true
                    visible: (root.plan.constraints || []).length > 0
                    text: "Keep in mind · " + (root.plan.constraints || []).slice(0, 2).join(" · ")
                    color: root.muted
                    font.family: root.uiFont
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        objectName: "agentSessionEditQuestionsButton"
                        text: "Change details"
                        subtle: true
                        visible: root.activeQuestions.length > 0
                        onClicked: root.openQuestions()
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.actionMessage
                        color: root.coral
                        font.family: root.uiFont
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                    TerminalButton {
                        id: startButton
                        objectName: "agentSessionStartButton"
                        text: "Start"
                        primary: true
                        enabled: root.canStart && root.stage === "plan-ready"
                        Accessible.description: enabled ? "Approve this plan and begin the task." : "The plan is not ready to start."
                        onClicked: root.startApprovedPlan()
                    }
                }
            }

            // Actual activity is shown as text from the session/task owner.
            ColumnLayout {
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: "Working on your approved plan"
                    color: root.paper
                    font.family: root.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 62
                    radius: 3
                    color: root.panel
                    border.width: 1
                    border.color: "#3b4e5d"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8
                        OpticalActivityIndicator {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            Layout.alignment: Qt.AlignTop
                            active: root.busy
                            reducedMotion: root.reducedMotion
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                objectName: "agentSessionWorkingActivity"
                                Layout.fillWidth: true
                                text: root.activityText.length > 0 ? root.activityText
                                      : root.statusText.length > 0 ? root.statusText
                                      : "The approved task is running."
                                color: root.paper
                                font.family: root.uiFont
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                maximumLineCount: 4
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: root.targetSummary()
                                color: root.muted
                                font.family: root.mono
                                font.pixelSize: 8
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TerminalButton {
                        id: stopButton
                        objectName: "agentSessionStopButton"
                        text: "Stop"
                        subtle: true
                        enabled: root.stage === "executing"
                        onClicked: root.stopWork()
                    }
                    TerminalButton {
                        objectName: "agentSessionRevertButton"
                        text: "Revert"
                        subtle: true
                        enabled: root.stage === "executing" && !Boolean(root.controller.revertPending)
                        onClicked: root.revertWork()
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.actionMessage
                        color: root.coral
                        font.family: root.uiFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }

            // A completed result stays editable and can be revised through a new approved plan.
            ColumnLayout {
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    text: "Your editable result is ready"
                    color: root.paper
                    font.family: root.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    objectName: "agentSessionReviewStatus"
                    Layout.fillWidth: true
                    text: root.statusText.length > 0 ? root.statusText : "Review the result in the editor."
                    color: root.muted
                    font.family: root.uiFont
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TerminalButton {
                        objectName: "agentSessionOpenDraftButton"
                        text: "Review result"
                        primary: true
                        visible: root.draftAvailable
                        onClicked: root.dialog.setPage(3)
                    }
                    TerminalButton {
                        objectName: "agentSessionReviewRevertButton"
                        text: "Revert task"
                        subtle: true
                        visible: root.draftAvailable
                        onClicked: root.revertWork()
                    }
                }
                Label {
                    text: "ASK FOR A REVISION"
                    color: root.cyan
                    font.family: root.mono
                    font.pixelSize: 8
                    font.weight: Font.Bold
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: revisionField
                        objectName: "agentSessionRevisionPrompt"
                        Layout.fillWidth: true
                        implicitHeight: 32
                        text: root.revisionPrompt
                        placeholderText: "Make the headline larger…"
                        placeholderTextColor: "#8f9ba3"
                        color: root.paper
                        selectionColor: root.cyan
                        font.family: root.uiFont
                        font.pixelSize: 10
                        selectByMouse: true
                        activeFocusOnTab: true
                        onTextChanged: root.revisionPrompt = text
                        background: Rectangle {
                            radius: 3
                            color: root.panel
                            border.width: parent.activeFocus ? 2 : 1
                            border.color: parent.activeFocus ? root.cyan : root.line
                        }
                    }
                    TerminalButton {
                        objectName: "agentSessionReviseButton"
                        text: "Revise"
                        primary: true
                        enabled: root.revisionPrompt.trim().length > 0
                        onClicked: root.reviseResult()
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: root.actionMessage
                    visible: root.actionMessage.length > 0
                    color: root.coral
                    font.family: root.uiFont
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }
            }

            // Failure and interruption retain the request and offer a direct retry.
            ColumnLayout {
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: root.stage === "interrupted" ? "The task was stopped." : "The plan needs another try."
                    color: root.paper
                    font.family: root.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    objectName: "agentSessionFailureStatus"
                    Layout.fillWidth: true
                    text: root.statusText.length > 0 ? root.statusText
                          : root.activityText.length > 0 ? root.activityText
                          : "Your request and answers are still here."
                    color: root.muted
                    font.family: root.uiFont
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    TerminalButton {
                        objectName: "agentSessionRetryButton"
                        text: "Try again"
                        primary: true
                        onClicked: root.retryPlanning()
                    }
                    TerminalButton {
                        objectName: "agentSessionModelSettingsButton"
                        visible: root.dialog.hasController() &&
                                 !Boolean((root.dialog.activeModel() || ({})).available)
                        text: "Model settings"
                        subtle: true
                        onClicked: root.dialog.modelSetupRequested()
                    }
                    TerminalButton {
                        objectName: "agentSessionChangeRequestButton"
                        text: "Edit request"
                        subtle: true
                        onClicked: root.subview = "prompt"
                    }
                    Label { Layout.fillWidth: true; text: "" }
                }
                RowLayout {
                    objectName: "agentSessionFailureRecoveryRow"
                    Layout.fillWidth: true
                    visible: root.failureRevertAvailable
                    spacing: 8
                    TerminalButton {
                        objectName: "agentSessionFailureRevertButton"
                        text: root.failureRevertOwner === "prepared-canvas" ? "Restore canvas" : "Revert task"
                        subtle: true
                        enabled: root.failureRevertAvailable && !root.failureRevertPending
                        Accessible.description: enabled
                            ? "Restore the reversible work owned by this interrupted task."
                            : "Wait for the task owner to finish settling before restoring work."
                        onClicked: root.revertWork()
                    }
                }
                Label {
                    objectName: "agentSessionFailureRecoveryStatus"
                    Layout.fillWidth: true
                    visible: root.failureRevertPending || root.actionMessage.length > 0
                    text: root.actionMessage.length > 0 ? root.actionMessage : "Waiting for the task owner to settle."
                    color: root.actionMessage.length > 0 ? root.coral : root.muted
                    font.family: root.uiFont
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }
    }
}
