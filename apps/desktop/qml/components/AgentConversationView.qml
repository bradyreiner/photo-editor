pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Item {
    id: root
    objectName: "agentConversationView"

    required property var dialog
    required property var themeObject

    readonly property color paper: themeObject.brandPaper
    readonly property color ink: themeObject.brandInk
    readonly property color primaryAccent: themeObject.primaryAccent
    readonly property color quietInk: themeObject.alpha(ink, 0.64)
    readonly property string displayFont: themeObject.fontDisplay
    readonly property string uiFont: themeObject.fontUi
    readonly property int page: dialog.currentPage
    readonly property var sessionController: dialog.sessionController || null
    readonly property bool sessionFlowAvailable: sessionController !== null && sessionController !== undefined
    readonly property bool failureDetailsOpen: sessionFlowAvailable && sessionTerminal.failureDetailsOpen
    property bool inspirationEntryFocusRetryScheduled: false
    property bool memoryDetailsExpanded: false
    property bool historyDetailsExpanded: false
    property bool activityHistoryExpanded: false
    property bool reviewDetailsExpanded: false

    Timer {
        id: inspirationEntryFocusRetryTimer
        interval: 16
        repeat: false
        onTriggered: {
            root.inspirationEntryFocusRetryScheduled = false
            if (root.page !== 1)
                return
            let firstCard = null
            if (presetSuggestionList.visible && presetSuggestionList.count > 0)
                firstCard = presetSuggestionList.itemAtIndex(0)
            if (!firstCard && photoSuggestionList.visible && photoSuggestionList.count > 0)
                firstCard = photoSuggestionList.itemAtIndex(0)
            if (firstCard && firstCard.visible && firstCard.width > 0 && firstCard.height > 0)
                firstCard.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    function focusPageEntry() {
        if (sessionFlowAvailable) {
            sessionTerminal.focusPageEntry()
            return
        }
        if (page === 0) {
            promptEditor.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        if (page === 1) {
            let firstCard = null
            if (presetSuggestionList.visible && presetSuggestionList.count > 0)
                firstCard = presetSuggestionList.itemAtIndex(0)
            if (!firstCard && photoSuggestionList.visible && photoSuggestionList.count > 0)
                firstCard = photoSuggestionList.itemAtIndex(0)
            if (firstCard && firstCard.visible && firstCard.width > 0 && firstCard.height > 0) {
                root.inspirationEntryFocusRetryScheduled = false
                firstCard.forceActiveFocus(Qt.TabFocusReason)
                return
            }
            changePromptButton.forceActiveFocus(Qt.TabFocusReason)
            const suggestionsAvailable =
                (presetSuggestionList.visible && presetSuggestionList.count > 0) ||
                (photoSuggestionList.visible && photoSuggestionList.count > 0)
            if (suggestionsAvailable && !root.inspirationEntryFocusRetryScheduled) {
                root.inspirationEntryFocusRetryScheduled = true
                inspirationEntryFocusRetryTimer.restart()
            }
            return
        }
        if (page === 2 && cancelRunButton.visible)
            cancelRunButton.forceActiveFocus(Qt.TabFocusReason)
    }

    function presetSuggestions() {
        return (dialog.inspirationSuggestions || []).filter(function(item) {
            return String((item || ({})).kind || "") === "preset"
        })
    }

    function photoSuggestions() {
        return (dialog.inspirationSuggestions || []).filter(function(item) {
            return String((item || ({})).kind || "") === "photo"
        })
    }

    function pageTitle() {
        if (page === 0)
            return "What’s your goal for this photo?"
        if (page === 1 && dialog.styleStartMode === "learn")
            return "Which edited photos feel like you?"
        if (page === 1 && dialog.styleStartMode === "built-in")
            return "Choose a starting look."
        if (page === 1)
            return "These could help shape the edit."
        if (page === 2)
            return "I'll work in the editor so you can follow along."
        return dialog.agentSpeechTitle()
    }

    function pageDetail() {
        if (page === 0)
            return "You’ll preview the edit before applying it."
        if (page === 1 && dialog.styleStartMode === "learn")
            return "Choose one or more finished examples. I'll learn their recipe and rendered character, then keep the new look project-local."
        if (page === 1 && dialog.styleStartMode === "built-in")
            return "Built-in looks are safe starting points. Your words and approved corrections still control the final result."
        if (page === 1)
            return "Keep one preset, add any useful reference photos, or skip them. Inspiration is never edited."
        if (page === 2)
            return "You'll see which photo I'm on, what I'm touching, and what I'm checking."
        return dialog.agentSpeechDetail()
    }

    component SoftButton: ActionButton {
        property bool primary: false
        property bool selected: false
        property string accessibleLabel: ""
        accessibleName: accessibleLabel
        active: selected
        tone: primary ? "primary" : "secondary"
    }

    component SuggestionCard: Item {
        id: card
        objectName: (photo ? "agentStyleReferenceCard-" : "agentBuiltInLookCard-") +
                    String((suggestion || ({})).id || "unknown")
        required property var suggestion
        property bool photo: false
        property bool selected: root.dialog.suggestionSelected(suggestion)
        signal chosen()
        activeFocusOnTab: true
        Accessible.role: Accessible.CheckBox
        Accessible.name: String((suggestion || ({})).title || "Inspiration")
        Accessible.description: photo ? "Reference photo. Select to guide the look without editing this image."
                                      : String((suggestion || ({})).detail || "Select as a starting look.")
        Accessible.checked: selected
        Accessible.onPressAction: chosen()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                card.chosen()
                event.accepted = true
            }
        }

        implicitWidth: photo ? 228 : 228
        implicitHeight: photo ? 186 : 172

        Rectangle {
            anchors.fill: parent
            radius: 16
            color: card.selected ? root.themeObject.alpha(root.paper, 0.16)
                                 : root.themeObject.alpha(root.paper, cardMouse.containsMouse ? 0.11 : 0.07)
            border.width: card.selected || card.activeFocus ? 2 : 0
            border.color: card.activeFocus ? root.paper : root.primaryAccent
            Behavior on color { ColorAnimation { duration: 130 } }

            Image {
                visible: card.photo && String(card.suggestion.thumbnailSource || "").length > 0
                anchors.fill: parent
                anchors.bottomMargin: 58
                source: root.dialog.normalizeImageSource(card.suggestion.thumbnailSource || "")
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                clip: true
            }

            Rectangle {
                visible: card.photo
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 64
                radius: 16
                color: root.themeObject.alpha(root.ink, 0.88)
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 14
                spacing: 4

                Label {
                    width: parent.width
                    text: String(card.suggestion.title || "Inspiration")
                    color: root.paper
                    font.family: root.uiFont
                    font.pixelSize: root.themeObject.typeLabel
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    width: parent.width
                    text: card.photo ? "Reference photo" : String(card.suggestion.detail || "")
                    color: root.themeObject.alpha(root.paper, 0.62)
                    font.family: root.uiFont
                    font.pixelSize: root.themeObject.typeMicro
                    maximumLineCount: card.photo ? 1 : 3
                    elide: Text.ElideRight
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 10
                width: 24
                height: 24
                radius: 12
                color: card.selected ? root.primaryAccent : root.themeObject.alpha(root.ink, 0.70)

                AppIcon {
                    anchors.centerIn: parent
                    width: 13
                    height: 13
                    name: card.selected ? "check" : "plus"
                    color: root.paper
                }
            }
        }

        MouseArea {
            id: cardMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: card.forceActiveFocus(Qt.MouseFocusReason)
            onClicked: card.chosen()
        }
    }

    ColumnLayout {
        visible: !root.sessionFlowAvailable
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: pageHeaderTextColumn.implicitHeight + 24
            Layout.minimumHeight: pageHeaderTextColumn.implicitHeight + 24
            Layout.maximumHeight: pageHeaderTextColumn.implicitHeight + 24
            spacing: 18


            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"

                ColumnLayout {
                    id: pageHeaderTextColumn
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 12
                    anchors.topMargin: 14
                    anchors.bottomMargin: 10
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: root.pageTitle()
                        color: root.paper
                        font.family: root.displayFont
                        font.pixelSize: root.width < 480 ? 21 : 25
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.width >= 480
                        text: root.pageDetail()
                        color: root.themeObject.alpha(root.paper, 0.62)
                        font.family: root.uiFont
                        font.pixelSize: root.themeObject.typeLabel
                        lineHeight: 1.18
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.page

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    anchors.topMargin: 8
                    anchors.bottomMargin: 4
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.width < 480 ? 38 : 54
                        color: "transparent"

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: root.themeObject.alpha(root.paper, 0.11)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            anchors.rightMargin: 2
                            spacing: 10

                            AppIcon {
                                Layout.preferredWidth: 17
                                Layout.preferredHeight: 17
                                name: root.dialog.scopeMode === "project" ? "folder"
                                      : root.dialog.scopeMode === "filtered-library" ? "filter"
                                      : root.dialog.scopeMode === "selected-assets" ? "check" : "media"
                                color: root.primaryAccent
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label {
                                    text: "Editing"
                                    color: root.themeObject.alpha(root.paper, 0.50)
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    font.letterSpacing: 1.2
                                }
                                Label {
                                    objectName: "agentTargetSummary"
                                    Layout.fillWidth: true
                                    text: root.dialog.targetSummary()
                                    color: root.paper
                                    font.family: root.uiFont
                                    font.pixelSize: root.themeObject.typeLabel
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }

                            Label {
                                text: root.dialog.scopeCount(root.dialog.scopeMode) +
                                      " photo" + (root.dialog.scopeCount(root.dialog.scopeMode) === 1 ? "" : "s")
                                color: root.themeObject.alpha(root.paper, 0.54)
                                font.family: root.themeObject.fontUi
                                font.pixelSize: root.themeObject.typeMicro
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: root.width < 480 ? 96 : 120
                        Layout.maximumHeight: root.width < 480 ? 96 : 120
                        Layout.preferredHeight: root.width < 480 ? 96 : 120
                        radius: 20
                        color: root.themeObject.alpha(root.paper, 0.075)
                        border.width: promptEditor.activeFocus || root.dialog.directionError.length > 0 ? 1 : 0
                        border.color: root.dialog.directionError.length > 0 ? root.themeObject.error
                                                                                : root.themeObject.alpha(root.paper, 0.36)

                        ScrollView {
                            id: promptScroll
                            anchors.fill: parent
                            anchors.margins: 10
                            clip: true
                            contentWidth: availableWidth
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ToneTextArea {
                            id: promptEditor
                            objectName: "agentPromptEditor"
                            accessibleLabel: "AI edit instructions"
                            width: promptScroll.availableWidth
                            height: Math.max(promptScroll.availableHeight, contentHeight + topPadding + bottomPadding)
                            text: root.dialog.hasController()
                                  ? String(root.dialog.agentEditController.userInstructions || "") : ""
                            placeholderText: "Make the afternoon feel quiet and cinematic. Keep the skin natural, lift the shadows, and crop for a stronger portrait…"
                            placeholderTextColor: root.themeObject.alpha(root.paper, 0.34)
                            color: root.paper
                            selectionColor: root.primaryAccent
                            selectedTextColor: root.paper
                            font.family: root.uiFont
                            font.pixelSize: root.themeObject.typeHero
                            wrapMode: TextEdit.Wrap
                            background: InputFieldSurface { inputControl: parent; inlineField: true }
                            onTextChanged: {
                                if (root.dialog.hasController())
                                    root.dialog.agentEditController.userInstructions = text
                                root.dialog.directionError = ""
                            }
                            Keys.priority: Keys.BeforeItem
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Escape && !root.dialog.agentRunning()) {
                                    root.dialog.close()
                                    event.accepted = true
                                }
                            }
                        }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            objectName: "agentScopeRiskMessage"
                            Layout.fillWidth: true
                            text: root.dialog.directionError.length > 0
                                  ? root.dialog.directionError
                                  : root.dialog.batchForecastLabel()
                            color: root.dialog.directionError.length > 0 ? root.themeObject.error
                                                                         : root.themeObject.alpha(root.paper, 0.50)
                            font.family: root.uiFont
                            font.pixelSize: root.themeObject.typeMicro
                            wrapMode: Text.WordWrap
                            Accessible.description: root.dialog.batchForecastExplanation()
                        }

                        SoftButton {
                            id: detailsButton
                            objectName: "agentDetailsToggle"
                            text: root.dialog.detailsExpanded ? "Hide details" : "Details"
                            onClicked: root.dialog.setDetailsExpanded(!root.dialog.detailsExpanded)
                        }

                        SoftButton {
                            objectName: "agentMemoryDetailsToggle"
                            text: "Memory"
                            selected: root.memoryDetailsExpanded
                            Accessible.name: root.memoryDetailsExpanded
                                             ? "Hide local memory details"
                                             : "Show local memory details"
                            onClicked: root.memoryDetailsExpanded = !root.memoryDetailsExpanded
                        }

                        SoftButton {
                            objectName: "agentRunHistoryToggle"
                            text: "History"
                            selected: root.historyDetailsExpanded
                            Accessible.name: root.historyDetailsExpanded
                                             ? "Hide run verification history"
                                             : "Show run verification history"
                            onClicked: root.historyDetailsExpanded = !root.historyDetailsExpanded
                        }

                        SoftButton {
                            objectName: "agentAddInspirationButton"
                            visible: root.dialog.detailsExpanded
                            text: "Add inspiration"
                            tooltipText: "Choose saved looks or photos as references; they will not be edited."
                            enabled: promptEditor.text.trim().length > 0
                            onClicked: root.dialog.preparePromptSuggestions()
                        }

                        SoftButton {
                            objectName: "agentPrimaryCreateDraftButton"
                            text: String(root.dialog.runButtonLabel() || "") === "Commit Edits"
                                  ? "Commit edits" : root.dialog.runButtonLabel()
                            primary: true
                            enabled: root.dialog.directionReady()
                            onClicked: root.dialog.triggerPrimaryDirectionAction()
                        }
                    }

                    Rectangle {
                        id: styleStartCard
                        objectName: "agentStyleStartCard"
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.dialog.detailsExpanded ? 76 : 110
                        color: "transparent"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: root.dialog.detailsExpanded ? 10 : 14
                            anchors.rightMargin: root.dialog.detailsExpanded ? 10 : 14
                            anchors.topMargin: root.dialog.detailsExpanded ? 4 : 10
                            anchors.bottomMargin: root.dialog.detailsExpanded ? 4 : 10
                            spacing: root.dialog.detailsExpanded ? 4 : 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    Label {
                                        text: "Your look"
                                        color: root.themeObject.alpha(root.paper, 0.50)
                                        font.family: root.themeObject.fontUi
                                        font.pixelSize: root.themeObject.typeMicro
                                        font.letterSpacing: 1.2
                                    }

                                    Label {
                                        objectName: "agentCurrentProjectLookLabel"
                                        Layout.fillWidth: true
                                        text: root.dialog.selectedStyleName()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeLabel
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: root.dialog.styleStartMessage.length > 0
                                          ? root.dialog.styleStartMessage
                                          : root.dialog.currentLookDetail()
                                    color: root.dialog.styleStartMessage.length > 0
                                           ? root.themeObject.success
                                           : root.themeObject.alpha(root.paper, 0.54)
                                    font.family: root.uiFont
                                    font.pixelSize: root.themeObject.typeMicro
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                SoftButton {
                                    objectName: "agentLearnLookFromEditsButton"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: root.dialog.detailsExpanded ? 36 : 44
                                    text: "Learn from edits"
                                    onClicked: root.dialog.showEditedLooks()
                                }

                                SoftButton {
                                    objectName: "agentDescribeLookButton"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: root.dialog.detailsExpanded ? 36 : 44
                                    text: "Make this my look"
                                    enabled: promptEditor.text.trim().length >= 4
                                    onClicked: root.dialog.saveWordsAsLook()
                                }

                                SoftButton {
                                    objectName: "agentBuiltInLooksButton"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: root.dialog.detailsExpanded ? 36 : 44
                                    text: "Built-in looks"
                                    onClicked: root.dialog.showBuiltInLooks()
                                }
                            }
                        }
                    }

                    Rectangle {
                        objectName: "agentEditingFreedomCard"
                        readonly property string editingFreedomContract:
                            "agent-editing-freedom-v1|modes:protect-balanced-transform|default:balanced|retouch:approval|destructive:approval"
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.dialog.detailsExpanded ? 0 : 68
                        visible: !root.dialog.detailsExpanded
                        radius: 14
                        color: root.themeObject.alpha(root.paper, 0.065)
                        border.width: 1
                        border.color: root.themeObject.alpha(root.paper, 0.10)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            anchors.topMargin: 8
                            anchors.bottomMargin: 8
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    text: "Editing freedom"
                                    color: root.themeObject.alpha(root.paper, 0.50)
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    font.letterSpacing: 1.1
                                }

                                Label {
                                    objectName: "agentEditingFreedomSummary"
                                    Layout.fillWidth: true
                                    text: root.dialog.editingFreedomSummary()
                                    color: root.themeObject.alpha(root.paper, 0.64)
                                    font.family: root.uiFont
                                    font.pixelSize: root.themeObject.typeMicro
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }

                            SoftButton {
                                objectName: "agentEditingFreedomProtect"
                                Layout.preferredWidth: 90
                                Layout.preferredHeight: 36
                                text: "Protect"
                                selected: root.dialog.editingFreedomMode() === "protect"
                                enabled: !root.dialog.agentRunning() &&
                                         !Boolean(root.dialog.agentEditController &&
                                                  root.dialog.agentEditController.draftActive)
                                onClicked: root.dialog.setEditingFreedomMode("protect")
                            }

                            SoftButton {
                                objectName: "agentEditingFreedomBalanced"
                                Layout.preferredWidth: 98
                                Layout.preferredHeight: 36
                                text: "Balanced"
                                selected: root.dialog.editingFreedomMode() === "balanced"
                                enabled: !root.dialog.agentRunning() &&
                                         !Boolean(root.dialog.agentEditController &&
                                                  root.dialog.agentEditController.draftActive)
                                onClicked: root.dialog.setEditingFreedomMode("balanced")
                            }

                            SoftButton {
                                objectName: "agentEditingFreedomTransform"
                                Layout.preferredWidth: 98
                                Layout.preferredHeight: 36
                                text: "Transform"
                                selected: root.dialog.editingFreedomMode() === "transform"
                                enabled: !root.dialog.agentRunning() &&
                                         !Boolean(root.dialog.agentEditController &&
                                                  root.dialog.agentEditController.draftActive)
                                onClicked: root.dialog.setEditingFreedomMode("transform")
                            }
                        }
                    }

                    Rectangle {
                        id: detailsPanel
                        objectName: "agentDetailsPanel"
                        Layout.fillWidth: true
                        visible: root.dialog.detailsExpanded || root.memoryDetailsExpanded || root.historyDetailsExpanded
                        Layout.preferredHeight: visible
                                                ? Math.min(root.height * 0.56,
                                                           detailsPanelColumn.implicitHeight +
                                                           root.themeObject.space28)
                                                : 0
                        radius: 16
                        color: root.themeObject.alpha(root.paper, 0.065)
                        clip: true

                        ColumnLayout {
                            id: detailsPanelColumn
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.dialog.detailsExpanded ? 82 : 0
                                visible: root.dialog.detailsExpanded
                                spacing: 18

                                ColumnLayout {
                                    Layout.preferredWidth: 184
                                    spacing: 5

                                    Label {
                                        text: "Scope"
                                        color: root.themeObject.alpha(root.paper, 0.46)
                                        font.family: root.themeObject.fontUi
                                        font.pixelSize: root.themeObject.typeMicro
                                        font.letterSpacing: 1.1
                                    }

                                    ToneComboBox {
                                        id: scopeSelector
                                        objectName: "agentScopeSelector"
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 34
                                        model: root.dialog.availableScopeLabels()
                                        currentIndex: root.dialog.scopeChoiceIndex()
                                        accent: root.primaryAccent
                                        font.pixelSize: root.themeObject.typeMicro
                                        fitTextToWidth: true
                                        showFullTextTooltip: false
                                        Accessible.name: "AI edit scope"
                                        Accessible.description: "Choose which photos Looksy will edit"
                                        onActivated: function(index) {
                                            root.dialog.chooseAvailableScope(index)
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        objectName: "agentDetailsLookLabel"
                                        Layout.fillWidth: true
                                        text: "Look  " + root.dialog.selectedStyleName()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentDetailsTasteLabel"
                                        Layout.fillWidth: true
                                        text: "Taste  " + root.dialog.tasteMirrorLabel()
                                        color: root.themeObject.alpha(root.paper, 0.62)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentDetailsEditingFreedomLabel"
                                        Layout.fillWidth: true
                                        text: "Freedom  " + root.dialog.editingFreedomMode()
                                        color: root.themeObject.alpha(root.paper, 0.62)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        objectName: "agentDetailsModelLabel"
                                        Layout.fillWidth: true
                                        text: "Model  " + root.dialog.activeModelLabel()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentLiveMcpStatusLabel"
                                        Layout.fillWidth: true
                                        text: "Tools  " + root.dialog.localToolsLabel()
                                        color: root.themeObject.alpha(root.paper, 0.62)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                        Accessible.description: root.dialog.mcpEndpointLabel()
                                    }

                                    Label {
                                        objectName: "agentDetailsRetouchRuntimeLabel"
                                        Layout.fillWidth: true
                                        text: "Retouch  " + root.dialog.retouchRuntimeLabel()
                                        color: root.themeObject.alpha(root.paper, 0.62)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                visible: root.dialog.detailsExpanded &&
                                         (root.memoryDetailsExpanded || root.historyDetailsExpanded)
                                color: root.themeObject.alpha(root.paper, 0.10)
                            }

                            RowLayout {
                                id: runAuditPanel
                                objectName: "agentLiveRunAuditPanel"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: root.memoryDetailsExpanded || root.historyDetailsExpanded
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        objectName: "agentLiveMemoryVaultLabel"
                                        Layout.fillWidth: true
                                        visible: root.memoryDetailsExpanded
                                        text: "Memory  " + root.dialog.memoryVaultLabel()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentLiveStyleChangeSummaryLabel"
                                        Layout.fillWidth: true
                                        visible: root.memoryDetailsExpanded
                                        text: "Learned  " + root.dialog.latestStyleChangeSummary()
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentLiveRunProofPathLabel"
                                        Layout.fillWidth: true
                                        visible: root.historyDetailsExpanded
                                        text: "Run  " + root.dialog.runAuditSummary() + " · " +
                                              root.dialog.proofArtifactLabel()
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                        Accessible.description: root.dialog.latestToolCallSummary()
                                    }

                                    Label {
                                        objectName: "agentLiveRunWarningsLabel"
                                        Layout.fillWidth: true
                                        visible: root.historyDetailsExpanded &&
                                                 (root.dialog.runWarnings().length > 0 ||
                                                  root.dialog.detailsActionMessage.length > 0)
                                        text: root.dialog.detailsActionMessage.length > 0
                                              ? root.dialog.detailsActionMessage
                                              : root.dialog.runWarnings().join(" · ")
                                        color: root.dialog.runWarnings().length > 0
                                               ? root.themeObject.warning
                                               : root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }
                                }

                                SoftButton {
                                    objectName: "agentOpenMemoryVaultButton"
                                    visible: root.memoryDetailsExpanded
                                    text: "Open vault"
                                    onClicked: root.dialog.openMemoryVault()
                                }

                                SoftButton {
                                    objectName: "agentOpenMemoryVaultInObsidianButton"
                                    visible: root.memoryDetailsExpanded
                                    text: "Obsidian"
                                    onClicked: root.dialog.openMemoryVaultInObsidian()
                                }

                                SoftButton {
                                    objectName: "agentExportRedactedMemoryButton"
                                    visible: root.memoryDetailsExpanded
                                    text: "Export safe copy"
                                    onClicked: root.dialog.exportRedactedMemoryVault()
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    anchors.topMargin: 2
                    anchors.bottomMargin: 4
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: root.dialog.suggestionSummary
                            color: root.themeObject.alpha(root.paper, 0.54)
                            font.family: root.themeObject.fontUi
                            font.pixelSize: root.themeObject.typeMicro
                            font.letterSpacing: 0.8
                        }
                        SoftButton {
                            id: changePromptButton
                            objectName: "agentChangePromptButton"
                            text: "Change prompt"
                            accessibleLabel: "Change AI edit prompt"
                            tooltipText: "Return to the prompt and change the edit instructions."
                            compact: true
                            implicitWidth: 126
                            onClicked: root.dialog.setPage(0)
                        }
                    }

                    Label {
                        visible: root.dialog.styleStartMode !== "learn"
                        text: "Look"
                        color: root.themeObject.alpha(root.paper, 0.76)
                        font.family: root.themeObject.fontUi
                        font.pixelSize: root.themeObject.typeMicro
                        font.letterSpacing: 1.4
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        id: presetSuggestionList
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 140 : 0
                        visible: root.dialog.styleStartMode !== "learn"
                        orientation: ListView.Horizontal
                        spacing: 10
                        clip: true
                        model: root.presetSuggestions()
                        delegate: SuggestionCard {
                            required property var modelData
                            suggestion: modelData
                            width: 228
                            height: 140
                            onChosen: root.dialog.toggleSuggestion(suggestion)
                        }
                    }

                    Label {
                        visible: root.dialog.styleStartMode !== "built-in" && root.photoSuggestions().length > 0
                        text: "Optional photo references"
                        color: root.themeObject.alpha(root.paper, 0.76)
                        font.family: root.themeObject.fontUi
                        font.pixelSize: root.themeObject.typeMicro
                        font.letterSpacing: 1.4
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        id: photoSuggestionList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: visible ? 148 : 0
                        visible: root.dialog.styleStartMode !== "built-in" && root.photoSuggestions().length > 0
                        orientation: ListView.Horizontal
                        spacing: 10
                        clip: true
                        model: root.photoSuggestions()
                        delegate: SuggestionCard {
                            required property var modelData
                            suggestion: modelData
                            photo: true
                            width: 228
                            height: 186
                            onChosen: root.dialog.toggleSuggestion(suggestion)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            objectName: "agentStyleReferencePreflight"
                            Layout.fillWidth: true
                            text: root.dialog.styleStartMode === "learn"
                                  ? root.dialog.styleReferenceStatusText()
                                  : root.dialog.selectedInspirationPhotoIds().length > 0
                                  ? root.dialog.selectedInspirationPhotoIds().length + " reference photo" +
                                    (root.dialog.selectedInspirationPhotoIds().length === 1 ? "" : "s") + " selected"
                                  : "References are optional"
                            color: root.dialog.styleStartMode === "learn" &&
                                   root.dialog.selectedInspirationPhotoIds().length > 0 &&
                                   Boolean(root.dialog.styleReferencePreflight().warningRequired)
                                   ? root.themeObject.warning
                                   : root.themeObject.alpha(root.paper, 0.52)
                            font.family: root.uiFont
                            font.pixelSize: root.themeObject.typeMicro
                        }
                        SoftButton {
                            text: "Start editing " + root.dialog.scopeCount(root.dialog.scopeMode) +
                                  " photo" + (root.dialog.scopeCount(root.dialog.scopeMode) === 1 ? "" : "s")
                            primary: true
                            enabled: root.dialog.styleStartMode !== "learn" ||
                                     root.dialog.selectedInspirationPhotoIds().length > 0
                            onClicked: root.dialog.runAgent()
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18


                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            Label {
                                objectName: "agentLiveStageTitle"
                                Layout.fillWidth: true
                                text: root.dialog.agentRunning()
                                      ? root.dialog.activityTitle(root.dialog.activeActivityEvent())
                                      : "Finishing the full-resolution edit…"
                                color: root.paper
                                font.family: root.displayFont
                                font.pixelSize: root.themeObject.typePageTitle
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Label {
                                objectName: "agentLiveStageDetail"
                                Layout.fillWidth: true
                                text: {
                                    const event = root.dialog.activeActivityEvent()
                                    const detail = String(event.detail || "")
                                    return detail.length > 0 ? detail
                                                             : root.dialog.agentRunning()
                                                               ? root.dialog.activityDescription(event)
                                                               : "Your reversible draft is ready for review."
                                }
                                color: root.themeObject.alpha(root.paper, 0.62)
                                font.family: root.uiFont
                                font.pixelSize: root.themeObject.typeLabel
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.dialog.targetSummary() + "  ·  " +
                                      root.dialog.scopeCount(root.dialog.scopeMode) + " photo" +
                                      (root.dialog.scopeCount(root.dialog.scopeMode) === 1 ? "" : "s") +
                                      "  ·  " + root.dialog.runElapsedSeconds + " sec"
                                color: root.themeObject.alpha(root.paper, 0.46)
                                font.family: root.themeObject.fontUi
                                font.pixelSize: root.themeObject.typeMicro
                                elide: Text.ElideRight
                            }
                        }

                        SoftButton {
                            id: cancelRunButton
                            objectName: "agentCancelRunButton"
                            text: "Stop"
                            visible: root.dialog.agentRunning()
                            onClicked: root.dialog.agentEditController.cancelRun("user_requested")
                        }
                    }

                    Label {
                        objectName: "agentRunGoalLabel"
                        Layout.fillWidth: true
                        visible: root.dialog.agentRunning() && root.dialog.hasController()
                        text: root.dialog.hasController()
                              ? String(root.dialog.agentEditController.userInstructions || "") : ""
                        color: root.themeObject.alpha(root.paper, 0.54)
                        font.family: root.uiFont
                        font.pixelSize: root.themeObject.typeMicro
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        Accessible.name: "Your editing goal"
                    }

                    ProgressStrip {
                        objectName: "agentLiveProgressStrip"
                        Layout.fillWidth: true
                        progress: -1
                        accent: root.primaryAccent
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 14

                        Rectangle {
                            objectName: "agentLivePreviewPanel"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1.5
                            radius: 18
                            color: root.themeObject.ink950
                            border.width: 1
                            border.color: root.themeObject.alpha(root.paper, 0.12)
                            clip: true

                            Image {
                                id: liveAgentPreview
                                objectName: "agentLivePreviewImage"
                                anchors.fill: parent
                                anchors.margins: 1
                                source: root.dialog.livePreviewSource()
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                                visible: String(source).length > 0
                                Accessible.name: root.dialog.livePreviewLabel()
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: !liveAgentPreview.visible
                                text: "Your photo will appear here"
                                color: root.themeObject.alpha(root.paper, 0.46)
                                font.family: root.uiFont
                                font.pixelSize: root.themeObject.typeLabel
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 14
                                width: livePreviewBadge.implicitWidth + 20
                                height: 30
                                radius: 9
                                color: root.themeObject.alpha(root.themeObject.ink950, 0.80)

                                Label {
                                    id: livePreviewBadge
                                    anchors.centerIn: parent
                                    text: liveAgentPreview.status === Image.Ready ? "Live preview" : "Preview"
                                    color: root.themeObject.alpha(root.paper, 0.82)
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    font.letterSpacing: 1.0
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 54
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "transparent" }
                                    GradientStop { position: 1; color: root.themeObject.alpha(root.themeObject.ink950, 0.90) }
                                }

                                Label {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 14
                                    text: root.dialog.livePreviewLabel()
                                    color: root.paper
                                    font.family: root.uiFont
                                    font.pixelSize: root.themeObject.typeMicro
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: "Current step"
                                    color: root.themeObject.alpha(root.paper, 0.72)
                                    font.family: root.uiFont
                                    font.pixelSize: root.themeObject.typeCaption
                                }
                                SoftButton {
                                    objectName: "agentRunEventHistoryToggle"
                                    text: root.activityHistoryExpanded ? "Hide history" : "History"
                                    selected: root.activityHistoryExpanded
                                    Accessible.name: root.activityHistoryExpanded
                                                     ? "Hide recent activity history"
                                                     : "Show recent activity history"
                                    onClicked: root.activityHistoryExpanded = !root.activityHistoryExpanded
                                }
                            }

                            ListView {
                                objectName: "agentLiveActivityTimeline"
                                Layout.fillWidth: true
                                Layout.fillHeight: root.activityHistoryExpanded
                                Layout.preferredHeight: root.activityHistoryExpanded ? 220 : 62
                                Layout.minimumWidth: 0
                                clip: true
                                spacing: 8
                                model: root.dialog.runEvents()
                                       .slice(root.activityHistoryExpanded ? -8 : -1)
                                       .reverse()
                                delegate: Rectangle {
                                    id: activityEventRow
                                    required property var modelData
                                    width: ListView.view.width
                                    height: eventDetail.text.length > 0 ? 62 : 46
                                    color: "transparent"

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: root.themeObject.alpha(root.paper, 0.08)
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 2
                                        anchors.rightMargin: 2
                                        spacing: 12

                                        Rectangle {
                                            Layout.preferredWidth: 8
                                            Layout.preferredHeight: 8
                                            radius: 4
                                            color: String(activityEventRow.modelData.status || "").toLowerCase() === "running"
                                                   ? root.primaryAccent
                                                   : root.themeObject.alpha(root.paper, 0.36)
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Label {
                                                Layout.fillWidth: true
                                                text: String(activityEventRow.modelData.label || activityEventRow.modelData.stage || "Working")
                                                color: root.paper
                                                font.family: root.uiFont
                                                font.pixelSize: root.themeObject.typeCaption
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }
                                            Label {
                                                id: eventDetail
                                                Layout.fillWidth: true
                                                text: String(activityEventRow.modelData.detail || "")
                                                visible: text.length > 0
                                                color: root.themeObject.alpha(root.paper, 0.56)
                                                font.family: root.uiFont
                                                font.pixelSize: root.themeObject.typeMicro
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Label {
                                            text: String(activityEventRow.modelData.status || "")
                                            color: root.themeObject.alpha(root.paper, 0.42)
                                            font.family: root.themeObject.fontUi
                                            font.pixelSize: root.themeObject.typeMicro
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    id: reviewPage
                    objectName: "aiEditReviewPageColumn"
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    anchors.topMargin: 4
                    anchors.bottomMargin: 4
                    spacing: 12

                    Rectangle {
                        objectName: "aiEditPlanVerificationSummary"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 78
                        color: "transparent"
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: root.themeObject.alpha(root.paper, 0.11)
                        }
                        Accessible.name: "Edit plan and verification"
                        Accessible.description: root.dialog.planSummary() + ". " +
                                                root.dialog.validationLabel()

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 14

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    text: "Edit plan"
                                    color: root.themeObject.alpha(root.paper, 0.48)
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    font.letterSpacing: 1
                                }

                                Label {
                                    objectName: "aiEditPlanSummary"
                                    Layout.fillWidth: true
                                    text: root.dialog.planSummary()
                                    color: root.paper
                                    font.family: root.uiFont
                                    font.pixelSize: root.themeObject.typeMicro
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: root.reviewDetailsExpanded
                                    text: root.dialog.editIntentSummary()
                                    color: root.themeObject.alpha(root.paper, 0.50)
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 1
                                Layout.fillHeight: true
                                    color: root.themeObject.alpha(root.paper, 0.18)
                            }

                            ColumnLayout {
                                Layout.preferredWidth: 170
                                spacing: 4

                                Label {
                                    text: "Verification"
                                    color: root.themeObject.alpha(root.paper, 0.48)
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    font.letterSpacing: 1
                                }

                                Label {
                                    objectName: "aiEditVerificationStatus"
                                    Layout.fillWidth: true
                                    text: root.dialog.validationLabel()
                                    color: root.dialog.goalMatchPassed()
                                           ? root.themeObject.success : root.themeObject.warning
                                    font.family: root.uiFont
                                    font.pixelSize: root.themeObject.typeCaption
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    SoftButton {
                        objectName: "aiEditReviewDetailsToggle"
                        Layout.alignment: Qt.AlignRight
                        text: root.reviewDetailsExpanded ? "Hide review details" : "Review details"
                        selected: root.reviewDetailsExpanded
                        Accessible.name: root.reviewDetailsExpanded
                                         ? "Hide edit verification details"
                                         : "Show edit verification details"
                        onClicked: root.reviewDetailsExpanded = !root.reviewDetailsExpanded
                    }

                    Rectangle {
                        id: consistencyMapPanel
                        objectName: "aiEditConsistencyMap"
                        readonly property string consistencyContract:
                            "agent-professional-consistency-map-v1|basis:within-lighting-group-relative-render-response|metrics:tone-contrast-color-skin|ordering:stable|thresholds:fixed|digest:sha256"
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 188 : 0
                        visible: root.reviewDetailsExpanded &&
                                 !root.dialog.sampleReviewWaiting() &&
                                 Boolean(root.dialog.consistencyMap().available)
                        radius: 16
                        color: root.themeObject.alpha(root.paper, 0.075)
                        border.width: 1
                        border.color: String(root.dialog.consistencyMap().status || "") === "attention"
                                      ? root.themeObject.alpha(root.primaryAccent, 0.52)
                                      : root.themeObject.alpha(root.themeObject.success, 0.44)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 14

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 65
                                spacing: 2

                                Label {
                                    text: "Consistency map"
                                    color: root.themeObject.alpha(root.paper, 0.50)
                                    font.family: root.themeObject.fontUi
                                    font.pixelSize: root.themeObject.typeMicro
                                    font.letterSpacing: 1.1
                                }

                                Label {
                                    objectName: "aiEditConsistencyHeadline"
                                    Layout.fillWidth: true
                                    text: root.dialog.consistencyHeadline()
                                    color: root.paper
                                    font.family: root.displayFont
                                    font.pixelSize: root.themeObject.typeHero
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Label {
                                    objectName: "aiEditConsistencySummary"
                                    Layout.fillWidth: true
                                    text: root.dialog.consistencySummary()
                                    color: root.themeObject.alpha(root.paper, 0.62)
                                    font.family: root.uiFont
                                    font.pixelSize: root.themeObject.typeMicro
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                Layout.preferredHeight: 1
                                Layout.fillWidth: true
                                color: root.themeObject.alpha(root.paper, 0.10)
                            }

                            ListView {
                                id: consistencyGroupList
                                objectName: "aiEditConsistencyGroupList"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.fillHeight: true
                                orientation: ListView.Horizontal
                                layoutDirection: Qt.LeftToRight
                                spacing: 8
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                model: root.dialog.consistencyGroups()

                                delegate: Rectangle {
                                    id: consistencyGroupCard
                                    required property int index
                                    required property var modelData
                                    objectName: consistencyGroupCard.index === 0
                                                ? "aiEditConsistencyGroupFirst" : ""
                                    width: 154
                                    height: consistencyGroupList.height
                                    radius: 11
                                    color: root.themeObject.alpha(root.ink, 0.25)
                                    border.width: 1
                                    border.color: String(consistencyGroupCard.modelData.status || "") === "attention"
                                                  ? root.themeObject.alpha(root.primaryAccent, 0.54)
                                                  : root.themeObject.alpha(root.themeObject.success, 0.34)

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 9
                                        spacing: 3

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Rectangle {
                                                Layout.preferredWidth: 7
                                                Layout.preferredHeight: 7
                                                radius: 4
                                                color: String(consistencyGroupCard.modelData.status || "") === "attention"
                                                       ? root.primaryAccent : root.themeObject.success
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: String(consistencyGroupCard.modelData.label ||
                                                             "Lighting group " + (consistencyGroupCard.index + 1))
                                                color: root.paper
                                                font.family: root.uiFont
                                                font.pixelSize: root.themeObject.typeMicro
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Item { Layout.fillHeight: true }

                                        Label {
                                            objectName: consistencyGroupCard.index === 0
                                                        ? "aiEditConsistencyGroupFirstStatus" : ""
                                            Layout.fillWidth: true
                                            text: Number(consistencyGroupCard.modelData.driftCount || 0) > 0
                                                  ? Number(consistencyGroupCard.modelData.driftCount) +
                                                    (Number(consistencyGroupCard.modelData.driftCount) === 1
                                                     ? " needs your eye" : " need your eye")
                                                  : Number(consistencyGroupCard.modelData.photoCount || 0) + " aligned"
                                            color: root.themeObject.alpha(root.paper, 0.64)
                                            font.family: root.uiFont
                                            font.pixelSize: root.themeObject.typeMicro
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: exceptionReviewPanel
                        objectName: "aiEditExceptionReviewPanel"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 260
                        visible: root.dialog.reviewIssues().length > 0
                        radius: 18
                        color: root.themeObject.alpha(root.paper, 0.075)
                        border.width: 1
                        border.color: root.themeObject.alpha(root.primaryAccent, 0.42)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 9

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        objectName: "aiEditExceptionReviewHeadline"
                                        Layout.fillWidth: true
                                        text: root.dialog.exceptionReviewHeadline()
                                        color: root.paper
                                        font.family: root.displayFont
                                        font.pixelSize: root.themeObject.typePageTitle
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Exception-first review · verified photos stay out of your queue"
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                    }
                                }

                                SoftButton {
                                    objectName: "aiEditFixAllExceptionsButton"
                                    visible: Boolean(root.dialog.reviewSummary().fixAllOutliersAvailable)
                                    text: "Fix all safe outliers"
                                    onClicked: root.dialog.fixAllReviewIssues()
                                }
                            }

                            Rectangle {
                                objectName: "aiEditApprovedStoryDeliveryReceipt"
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? 68 : 0
                                visible: root.dialog.approvedStoryDirectionAvailable()
                                radius: 12
                                color: root.themeObject.alpha(root.primaryAccent, 0.11)
                                border.width: 1
                                border.color: root.themeObject.alpha(root.primaryAccent, 0.34)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 14

                                    ColumnLayout {
                                        Layout.preferredWidth: 330
                                        spacing: 2

                                        Label {
                                            objectName: "aiEditApprovedStoryEyebrow"
                                            Layout.fillWidth: true
                                            text: root.dialog.assignmentStoryEyebrow()
                                            color: root.primaryAccent
                                            font.family: root.themeObject.fontUi
                                            font.pixelSize: root.themeObject.typeMicro
                                            font.weight: Font.DemiBold
                                            font.letterSpacing: 0.7
                                        }

                                        Label {
                                            objectName: "aiEditApprovedStoryHeadline"
                                            Layout.fillWidth: true
                                            text: root.dialog.assignmentStoryHeadline()
                                            color: root.paper
                                            font.family: root.uiFont
                                            font.pixelSize: root.themeObject.typeCaption
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "aiEditApprovedStoryArc"
                                            Layout.fillWidth: true
                                            text: root.dialog.assignmentStoryArcText()
                                            color: root.themeObject.alpha(root.paper, 0.62)
                                            font.family: root.uiFont
                                            font.pixelSize: root.themeObject.typeMicro
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 1
                                        Layout.fillHeight: true
                                        color: root.themeObject.alpha(root.paper, 0.10)
                                    }

                                    Label {
                                        objectName: "aiEditApprovedStoryBoundary"
                                        Layout.fillWidth: true
                                        text: root.dialog.assignmentStorySummary() + "\n" +
                                              root.dialog.assignmentStoryReceiptText()
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.themeObject.fontUi
                                        font.pixelSize: root.themeObject.typeMicro
                                        font.letterSpacing: 0.45
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            ListView {
                                id: exceptionReviewList
                                objectName: "aiEditExceptionReviewList"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 8
                                model: root.dialog.reviewIssues()
                                boundsBehavior: Flickable.StopAtBounds

                                delegate: Rectangle {
                                    id: exceptionCard
                                    required property int index
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 136
                                    radius: 13
                                    color: root.themeObject.alpha(root.ink, 0.24)
                                    border.width: 1
                                    border.color: root.themeObject.alpha(root.paper, 0.10)

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 9
                                        spacing: 10

                                        Rectangle {
                                            Layout.preferredWidth: 92
                                            Layout.fillHeight: true
                                            radius: 9
                                            clip: true
                                            color: root.themeObject.alpha(root.ink, 0.32)

                                            Image {
                                                anchors.fill: parent
                                                source: root.dialog.normalizeImageSource(
                                                            String(exceptionCard.modelData.thumbnailPath ||
                                                                   exceptionCard.modelData.afterProofPath || ""))
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                            }

                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.top: parent.top
                                                anchors.margins: 6
                                                width: exceptionStatus.implicitWidth + 12
                                                height: 20
                                                radius: 10
                                                color: String(exceptionCard.modelData.status || "") === "failed"
                                                       ? root.themeObject.error : root.themeObject.warning

                                                Label {
                                                    id: exceptionStatus
                                                    anchors.centerIn: parent
                                                    text: String(exceptionCard.modelData.status || "") === "failed"
                                                          ? "Failed" : "Check"
                                                    color: root.paper
                                                    font.family: root.themeObject.fontUi
                                                    font.pixelSize: root.themeObject.typeMicro
                                                    font.weight: Font.Bold
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            spacing: 3

                                            Label {
                                                Layout.fillWidth: true
                                                text: root.dialog.reviewIssueTitle(exceptionCard.modelData)
                                                color: root.paper
                                                font.family: root.uiFont
                                                font.pixelSize: root.themeObject.typeLabel
                                                font.weight: Font.DemiBold
                                            }

                                            Label {
                                                objectName: exceptionCard.index === 0
                                                            ? "aiEditExceptionFirstSummary" : ""
                                                Layout.fillWidth: true
                                                text: root.dialog.reviewIssueSummary(exceptionCard.modelData)
                                                color: root.themeObject.alpha(root.paper, 0.70)
                                                font.family: root.uiFont
                                                font.pixelSize: root.themeObject.typeMicro
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                objectName: exceptionCard.index === 0
                                                            ? "aiEditExceptionFirstPreserved" : ""
                                                Layout.fillWidth: true
                                                visible: text.length > 0
                                                text: root.dialog.reviewIssuePreservedSummary(
                                                          exceptionCard.modelData)
                                                color: root.themeObject.alpha(root.paper, 0.48)
                                                font.family: root.uiFont
                                                font.pixelSize: root.themeObject.typeMicro
                                                elide: Text.ElideRight
                                            }

                                            Item { Layout.fillHeight: true }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 7

                                                SoftButton {
                                                    objectName: exceptionCard.index === 0
                                                                ? "aiEditExceptionFixButton" : ""
                                                    visible: root.dialog.reviewIssueAutoRepairAvailable(
                                                                 exceptionCard.modelData)
                                                    text: root.dialog.reviewIssueFixLabel(
                                                              exceptionCard.modelData)
                                                    primary: true
                                                    onClicked: root.dialog.resolveReviewIssue(
                                                                   String(exceptionCard.modelData.assetId || ""), "fix")
                                                }

                                                SoftButton {
                                                    objectName: exceptionCard.index === 0
                                                                ? "aiEditExceptionKeepButton" : ""
                                                    visible: String(exceptionCard.modelData.status || "") !== "failed"
                                                    text: "Keep anyway"
                                                    onClicked: root.dialog.resolveReviewIssue(
                                                                   String(exceptionCard.modelData.assetId || ""), "keep")
                                                }

                                                SoftButton {
                                                    objectName: exceptionCard.index === 0
                                                                ? "aiEditExceptionRevertButton" : ""
                                                    text: "Revert photo"
                                                    onClicked: root.dialog.resolveReviewIssue(
                                                                   String(exceptionCard.modelData.assetId || ""), "rollback")
                                                }

                                                Item { Layout.fillWidth: true }
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                objectName: "aiEditExceptionReviewFootnote"
                                Layout.fillWidth: true
                                text: root.dialog.exceptionReviewFootnote()
                                color: root.themeObject.alpha(root.paper, 0.58)
                                font.family: root.uiFont
                                font.pixelSize: root.themeObject.typeMicro
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Rectangle {
                        id: sampleReviewPanel
                        objectName: "aiEditSampleReviewPanel"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: root.dialog.assignmentStoryMapAvailable() ? 480 : 220
                        visible: root.dialog.reviewIssues().length <= 0
                        radius: 18
                        color: root.themeObject.alpha(root.paper, 0.075)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            Label {
                                objectName: "aiEditDirectionComparisonHeadline"
                                Layout.fillWidth: true
                                text: root.dialog.hasDirectionComparison()
                                      ? (root.dialog.directionComparisonFromCalibration()
                                         ? "Taste check · two real renders · you teach the choice"
                                         : "Two real directions · one reversible choice")
                                      : root.dialog.affectedPhotoSummary()
                                color: root.paper
                                font.family: root.displayFont
                                font.pixelSize: root.themeObject.typeHero
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Rectangle {
                                objectName: "aiEditAssignmentStoryMap"
                                Layout.fillWidth: true
                                visible: root.dialog.assignmentStoryMapAvailable()
                                implicitHeight: visible
                                                ? Math.max(132, storyMapContent.implicitHeight + 20)
                                                : 0
                                Layout.preferredHeight: implicitHeight
                                Layout.minimumHeight: implicitHeight
                                radius: 13
                                color: root.themeObject.alpha(root.primaryAccent, 0.11)
                                border.width: 1
                                border.color: root.themeObject.alpha(root.primaryAccent, 0.34)

                                ColumnLayout {
                                    id: storyMapContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 3

                                    Label {
                                        objectName: "aiEditAssignmentStoryEyebrow"
                                        Layout.fillWidth: true
                                        text: root.dialog.assignmentStoryEyebrow()
                                        color: root.primaryAccent
                                        font.family: root.themeObject.fontUi
                                        font.pixelSize: root.themeObject.typeMicro
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.8
                                    }

                                    Label {
                                        objectName: "aiEditAssignmentStoryHeadline"
                                        Layout.fillWidth: true
                                        text: root.dialog.assignmentStoryHeadline()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeLabel
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "aiEditAssignmentStoryArc"
                                        Layout.fillWidth: true
                                        text: root.dialog.assignmentStoryArcText()
                                        color: root.themeObject.alpha(root.paper, 0.78)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "aiEditAssignmentStorySummary"
                                        Layout.fillWidth: true
                                        text: root.dialog.assignmentStorySummary()
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        objectName: "aiEditStoryRoleEvidence"
                                        Layout.fillWidth: true
                                        visible: !root.dialog.approvedStoryDirectionAvailable()
                                        implicitHeight: 27
                                        Layout.preferredHeight: visible ? implicitHeight : 0
                                        Layout.minimumHeight: visible ? implicitHeight : 0
                                        Layout.maximumHeight: visible ? implicitHeight : 0
                                        radius: 7
                                        color: root.themeObject.alpha(root.ink, 0.19)

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            spacing: 8

                                            Label {
                                                objectName: "aiEditStoryRoleEvidenceTitle"
                                                text: root.dialog.shownStoryRoleEvidenceTitle()
                                                color: root.primaryAccent
                                                font.family: root.themeObject.fontUi
                                                font.pixelSize: root.themeObject.typeMicro
                                                font.weight: Font.DemiBold
                                                font.letterSpacing: 0.55
                                            }

                                            Label {
                                                objectName: "aiEditStoryRoleEvidenceReason"
                                                Layout.fillWidth: true
                                                text: root.dialog.shownStoryRoleReason()
                                                color: root.themeObject.alpha(root.paper, 0.66)
                                                font.family: root.uiFont
                                                font.pixelSize: root.themeObject.typeMicro
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                objectName: "aiEditStoryRoleEvidenceBoundary"
                                                text: "No scene claim"
                                                color: root.themeObject.alpha(root.paper, 0.34)
                                                font.family: root.themeObject.fontUi
                                                font.pixelSize: root.themeObject.typeMicro
                                                font.letterSpacing: 0.45
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Label {
                                            objectName: "aiEditAssignmentStoryReceipt"
                                            Layout.fillWidth: true
                                            text: root.dialog.assignmentStoryReceiptText()
                                            color: root.themeObject.alpha(root.paper, 0.42)
                                            font.family: root.themeObject.fontUi
                                            font.pixelSize: root.themeObject.typeMicro
                                            font.letterSpacing: 0.55
                                            elide: Text.ElideRight
                                        }

                                        Rectangle {
                                            objectName: "aiEditStorySampleNavigator"
                                            Layout.preferredWidth: 104
                                            Layout.preferredHeight: 32
                                            visible: root.dialog.sampleReviewWaiting() &&
                                                     root.dialog.reviewSamples().length > 1
                                            radius: 8
                                            color: root.themeObject.alpha(root.paper, 0.045)
                                            border.width: 1
                                            border.color: root.themeObject.alpha(root.paper, 0.13)

                                            RowLayout {
                                                anchors.fill: parent
                                                spacing: 0

                                                Components.ToolButton {
                                                    id: previousStorySampleButton
                                                    objectName: "aiEditStoryPreviousSample"
                                                    Layout.preferredWidth: 32
                                                    Layout.fillHeight: true
                                                    enabled: root.dialog.canShowPreviousSample()
                                                    label: "Previous sample"
                                                    iconName: "chevron-left"
                                                    accent: root.primaryAccent
                                                    compact: true
                                                    showLabel: false
                                                    onClicked: root.dialog.showPreviousSample()
                                                }

                                                Label {
                                                    objectName: "aiEditStorySamplePosition"
                                                    Layout.fillWidth: true
                                                    text: root.dialog.displayedSamplePositionText()
                                                    color: root.themeObject.alpha(root.paper, 0.64)
                                                    font.family: root.themeObject.fontUi
                                                    font.pixelSize: root.themeObject.typeMicro
                                                    font.weight: Font.DemiBold
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                }

                                                Components.ToolButton {
                                                    id: nextStorySampleButton
                                                    objectName: "aiEditStoryNextSample"
                                                    Layout.preferredWidth: 32
                                                    Layout.fillHeight: true
                                                    enabled: root.dialog.canShowNextSample()
                                                    label: "Next sample"
                                                    iconName: "chevron-right"
                                                    accent: root.primaryAccent
                                                    compact: true
                                                    showLabel: false
                                                    onClicked: root.dialog.showNextSample()
                                                }
                                            }
                                        }

                                        SoftButton {
                                            id: storyRoleButton
                                            objectName: "aiEditAssignmentStoryRoleButton"
                                            Layout.preferredHeight: 32
                                            Layout.preferredWidth: 152
                                            visible: root.dialog.canDirectShownStoryRole()
                                            text: "Shown · " + root.dialog.assignmentStoryRoleLabel(
                                                      root.dialog.shownStoryRole()) + "  ▾"
                                            accessibleLabel: "Choose shown story role"
                                            tooltipText: "Choose which reviewed image represents this story role."
                                            compact: true
                                            selected: storyRolePopup.visible
                                            onClicked: storyRolePopup.toggleTransient(storyRoleButton)
                                        }

                                        SoftButton {
                                            objectName: "aiEditStoryPreferenceAction"
                                            Layout.preferredWidth: 174
                                            Layout.preferredHeight: 29
                                            visible: root.dialog.canManageStoryPreference()
                                            text: root.dialog.storyPreferenceActionText()
                                            primary: !root.dialog.storyPreferenceRemembered() ||
                                                     root.dialog.storyPreferenceForgetArmed
                                            onClicked: root.dialog.manageStoryPreference()
                                        }

                                        Item {
                                            Layout.preferredWidth: 0
                                            Layout.preferredHeight: 0

                                        AppPopup {
                                            id: storyRolePopup
                                            objectName: "aiEditAssignmentStoryRolePopup"
                                            transientMenu: true
                                            parent: storyRoleButton
                                            x: storyRoleButton.width - width
                                            y: storyRoleButton.height + 6
                                            width: Math.min(popupAvailableWidth, Math.max(1, 260))
                                            height: Math.min(popupAvailableHeight, Math.max(1, storyRoleMenuColumn.implicitHeight + 12))
                                            padding: theme.space6
                                            modal: false
                                            focus: true
                                            closePolicy: Popup.CloseOnEscape |
                                                         Popup.CloseOnPressOutside

                                            background: PopupSurface {
                                                objectName: "aiEditAssignmentStoryRolePopupSurface"
                                            }

                                            contentItem: Column {
                                                id: storyRoleMenuColumn
                                                spacing: 3

                                                Repeater {
                                                    model: root.dialog.assignmentStoryRoleOptions()

                                                    delegate: ActionButton {
                                                        active: currentRole
                                                        id: storyRoleAction
                                                        required property var modelData
                                                        readonly property string roleValue:
                                                            String(modelData.role || "")
                                                        readonly property bool currentRole:
                                                            root.dialog.shownStoryRole() === roleValue
                                                        objectName: "aiEditStoryRole" +
                                                                    String(modelData.label || "") +
                                                                    "Action"
                                                        Accessible.role: Accessible.RadioButton
                                                        Accessible.name: String(modelData.label || roleValue)
                                                        Accessible.description: String(modelData.detail || "") +
                                                                               (currentRole
                                                                                ? " " + qsTr("Current selection.")
                                                                                : "")
                                                        Accessible.checked: currentRole
                                                        width: storyRoleMenuColumn.width
                                                        height: 43
                                                        enabled: root.dialog.canAssignShownStoryRole(
                                                                     roleValue)
                                                        onClicked: {
                                                            const requestedRole = roleValue
                                                            storyRolePopup.closeTransient(true)
                                                            root.dialog.assignShownStoryRole(
                                                                requestedRole)
                                                        }


                                                        contentItem: Item {
                                                            Label {
                                                                anchors.left: parent.left
                                                                anchors.right: currentRoleLabel.left
                                                                anchors.top: parent.top
                                                                text: String(storyRoleAction.modelData.label || "")
                                                                color: root.paper
                                                                font.family: root.uiFont
                                                                font.pixelSize: root.themeObject.typeMicro
                                                                font.weight: Font.DemiBold
                                                                elide: Text.ElideRight
                                                            }

                                                            Label {
                                                                anchors.left: parent.left
                                                                anchors.right: parent.right
                                                                anchors.bottom: parent.bottom
                                                                text: String(storyRoleAction.modelData.detail || "")
                                                                color: root.themeObject.alpha(root.paper, 0.54)
                                                                font.family: root.uiFont
                                                                font.pixelSize: root.themeObject.typeMicro
                                                                elide: Text.ElideRight
                                                            }

                                                            Label {
                                                                id: currentRoleLabel
                                                                anchors.right: parent.right
                                                                anchors.top: parent.top
                                                                visible: storyRoleAction.currentRole
                                                                text: "Current"
                                                                color: root.primaryAccent
                                                                font.family: root.themeObject.fontUi
                                                                font.pixelSize: root.themeObject.typeMicro
                                                                font.weight: Font.DemiBold
                                                                font.letterSpacing: 0.6
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        }
                                    }

                                    Label {
                                        objectName: "aiEditStoryPreferenceActionMessage"
                                        Layout.fillWidth: true
                                        visible: root.dialog.storyPreferenceActionMessage.length > 0
                                        text: root.dialog.storyPreferenceActionMessage
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: root.themeObject.typeMicro
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            Rectangle {
                                objectName: "aiEditCorrectionTransferPreview"
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.dialog.correctionTransferAvailable()
                                                        ? correctionTransferContent.implicitHeight + 20
                                                        : 0
                                visible: root.dialog.correctionTransferAvailable()
                                radius: 13
                                color: root.themeObject.alpha(root.primaryAccent, 0.12)
                                border.width: 1
                                border.color: root.themeObject.alpha(root.primaryAccent, 0.38)

                                RowLayout {
                                    id: correctionTransferContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 10

                                    Rectangle {
                                        Layout.preferredWidth: 30
                                        Layout.preferredHeight: 30
                                        radius: 15
                                        color: root.primaryAccent

                                        AppIcon {
                                            anchors.centerIn: parent
                                            name: "check"
                                            color: root.paper
                                            width: 15
                                            height: width
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            objectName: "aiEditCorrectionTransferHeadline"
                                            Layout.fillWidth: true
                                            text: root.dialog.correctionTransferHeadline()
                                            color: root.paper
                                            font.family: root.uiFont
                                            font.pixelSize: root.themeObject.typeLabel
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "aiEditCorrectionTransferLesson"
                                            Layout.fillWidth: true
                                            text: root.dialog.correctionTransferLesson()
                                            color: root.themeObject.alpha(root.paper, 0.78)
                                            font.family: root.uiFont
                                            font.pixelSize: root.themeObject.typeMicro
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "aiEditCorrectionTransferDetail"
                                            Layout.fillWidth: true
                                            text: root.dialog.correctionTransferDetail()
                                            color: root.themeObject.alpha(root.paper, 0.52)
                                            font.family: root.uiFont
                                            font.pixelSize: root.themeObject.typeMicro
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Label {
                                        text: "Memory after apply"
                                        color: root.themeObject.alpha(root.paper, 0.48)
                                        font.family: root.themeObject.fontUi
                                        font.pixelSize: root.themeObject.typeMicro
                                        font.letterSpacing: 0.8
                                    }
                                }
                            }

                            RowLayout {
                                objectName: "aiEditDirectionComparisonPanel"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 10

                                Rectangle {
                                    objectName: "aiEditDirectionCardA"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 12
                                    clip: true
                                    color: root.themeObject.alpha(root.ink, 0.24)
                                    border.width: root.dialog.directionChoiceKey() === "a" ? 2 : 0
                                    border.color: root.primaryAccent

                                    Image {
                                        objectName: "aiEditReviewBeforeImage"
                                        anchors.fill: parent
                                        source: root.dialog.reviewBeforeSource()
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                    }

                                    Label {
                                        objectName: "aiEditDirectionLabelA"
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.margins: 10
                                        text: root.dialog.hasDirectionComparison()
                                              ? root.dialog.directionDisplayLabel("a") : "Before"
                                        color: root.paper
                                        font.family: root.themeObject.fontUi
                                        font.pixelSize: root.themeObject.typeMicro
                                        font.weight: Font.DemiBold
                                    }

                                    SoftButton {
                                        objectName: "aiEditChooseDirectionA"
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 10
                                        visible: root.dialog.directionComparisonPending()
                                        text: "Choose A"
                                        onClicked: root.dialog.chooseDirection("a")
                                    }
                                }

                                Rectangle {
                                    objectName: "aiEditDirectionCardB"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 12
                                    clip: true
                                    color: root.themeObject.alpha(root.ink, 0.24)
                                    border.width: root.dialog.directionChoiceKey() === "b" ? 2 : 0
                                    border.color: root.primaryAccent

                                    Image {
                                        objectName: "aiEditReviewAfterImage"
                                        anchors.fill: parent
                                        source: root.dialog.reviewAfterSource()
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                    }

                                    Label {
                                        objectName: "aiEditDirectionLabelB"
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.margins: 10
                                        text: root.dialog.hasDirectionComparison()
                                              ? root.dialog.directionDisplayLabel("b") : "After"
                                        color: root.paper
                                        font.family: root.themeObject.fontUi
                                        font.pixelSize: root.themeObject.typeMicro
                                        font.weight: Font.DemiBold
                                    }

                                    SoftButton {
                                        objectName: "aiEditChooseDirectionB"
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 10
                                        visible: root.dialog.directionComparisonPending()
                                        text: "Choose B"
                                        primary: true
                                        onClicked: root.dialog.chooseDirection("b")
                                    }
                                }
                            }

                            Label {
                                objectName: "aiEditDirectionChoiceReceipt"
                                Layout.fillWidth: true
                                visible: root.dialog.directionChoiceKey().length > 0
                                text: "✓ " + root.dialog.directionChoiceLabel() +
                                      " selected · remembered only after commit edits"
                                color: root.themeObject.success
                                font.family: root.uiFont
                                font.pixelSize: root.themeObject.typeCaption
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Rectangle {
                        id: directionComparisonPrompt
                        objectName: "aiEditDirectionComparisonPrompt"
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.dialog.comparisonPromptOpen ? 92 : 0
                        visible: root.dialog.comparisonPromptOpen
                        radius: 16
                        color: root.themeObject.alpha(root.paper, 0.075)
                        border.width: alternativeDirectionEditor.activeFocus ? 1 : 0
                        border.color: root.primaryAccent

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            ToneTextArea {
                                id: alternativeDirectionEditor
                                objectName: "aiEditAlternativeDirectionEditor"
                                accessibleLabel: "Alternative edit direction"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: root.dialog.alternativeDirectionText
                                placeholderText: "Try a distinct direction — cooler, quieter, more editorial…"
                                placeholderTextColor: root.themeObject.alpha(root.paper, 0.38)
                                color: root.paper
                                selectionColor: root.primaryAccent
                                selectedTextColor: root.paper
                                font.family: root.uiFont
                                font.pixelSize: root.themeObject.typeLabel
                                wrapMode: TextEdit.Wrap
                                background: InputFieldSurface { inputControl: parent; inlineField: true }
                                onTextChanged: root.dialog.alternativeDirectionText = text
                            }

                            SoftButton {
                                objectName: "aiEditCreateComparisonButton"
                                text: "Create direction B"
                                primary: true
                                enabled: alternativeDirectionEditor.text.trim().length >= 4
                                onClicked: root.dialog.compareDirection()
                            }
                        }
                    }

                    Rectangle {
                        id: sampleRevisionPanel
                        objectName: "aiEditSampleRevisionPanel"
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.dialog.revisionExpanded ? 92 : 0
                        visible: root.dialog.revisionExpanded
                        radius: 16
                        color: root.themeObject.alpha(root.paper, 0.075)
                        border.width: sampleFeedbackEditor.activeFocus ? 1 : 0
                        border.color: root.primaryAccent

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            ToneTextArea {
                                id: sampleFeedbackEditor
                                objectName: "aiEditSampleFeedbackEditor"
                                accessibleLabel: "Feedback on reviewed samples"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: root.dialog.sampleFeedback
                                placeholderText: "Tell me what to change in these samples…"
                                placeholderTextColor: root.themeObject.alpha(root.paper, 0.38)
                                color: root.paper
                                selectionColor: root.primaryAccent
                                selectedTextColor: root.paper
                                font.family: root.uiFont
                                font.pixelSize: root.themeObject.typeLabel
                                wrapMode: TextEdit.Wrap
                                background: InputFieldSurface { inputControl: parent; inlineField: true }
                                onTextChanged: root.dialog.sampleFeedback = text
                            }

                            SoftButton {
                                objectName: "aiEditSubmitSampleRevisionButton"
                                text: "Make changes"
                                primary: true
                                enabled: sampleFeedbackEditor.text.trim().length > 0
                                onClicked: root.dialog.reviseSamples()
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            visible: !root.dialog.editCommitted
                            text: "Other review options"
                            color: root.themeObject.alpha(root.paper, 0.52)
                            font.family: root.uiFont
                            font.pixelSize: root.themeObject.typeCaption
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: root.width < 520 ? 1 : root.width < 900 ? 2 : 3
                            columnSpacing: 8
                            rowSpacing: 6

                            SoftButton {
                                objectName: "aiEditDiscardDraftButton"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                allowLabelWrap: true
                                visible: !root.dialog.editCommitted
                                text: root.dialog.sampleReviewWaiting() ? "Discard samples" : "Discard edit"
                                onClicked: root.dialog.rejectDraft()
                            }

                            SoftButton {
                                objectName: "aiEditAskForChangesButton"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                allowLabelWrap: true
                                visible: !root.dialog.editCommitted
                                text: root.dialog.sampleReviewWaiting()
                                      ? (root.dialog.revisionExpanded ? "Cancel changes" : "Make changes")
                                      : "Change direction"
                                onClicked: {
                                    if (root.dialog.sampleReviewWaiting())
                                        root.dialog.revisionExpanded = !root.dialog.revisionExpanded
                                    else
                                        root.dialog.reviseDraft()
                                }
                            }

                            SoftButton {
                                objectName: "aiEditCompareDirectionButton"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                allowLabelWrap: true
                                visible: !root.dialog.editCommitted &&
                                         !root.dialog.hasDirectionComparison() &&
                                         !root.dialog.revisionExpanded &&
                                         root.dialog.reviewIssues().length <= 0
                                text: root.dialog.photographerTasteCheckRecommended()
                                      ? "Compare restrained version"
                                      : root.dialog.comparisonPromptOpen
                                        ? "Cancel compare" : "Show me two directions"
                                onClicked: {
                                    if (root.dialog.photographerTasteCheckRecommended()) {
                                        root.dialog.startPhotographerTasteCheck()
                                        return
                                    }
                                    root.dialog.comparisonPromptOpen =
                                            !root.dialog.comparisonPromptOpen
                                }
                            }
                        }

                        SoftButton {
                            objectName: "aiEditPrimaryReviewButton"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            allowLabelWrap: true
                            visible: !root.dialog.revisionExpanded &&
                                     !root.dialog.comparisonPromptOpen &&
                                     !root.dialog.directionComparisonPending()
                            text: root.dialog.editCommitted
                                  ? "Done"
                                  : root.dialog.reviewIssues().length > 0
                                  ? "Resolve " + root.dialog.reviewIssues().length + " first"
                                  : root.dialog.sampleReviewWaiting()
                                  ? "Apply to All"
                                  : "Commit edits"
                            primary: true
                            enabled: root.dialog.editCommitted ||
                                     (root.dialog.canReviewDraft() &&
                                      root.dialog.reviewIssues().length <= 0)
                            disabledReason: root.dialog.reviewIssues().length > 0
                                             ? "Resolve the listed review issues before applying this draft."
                                             : "The draft must finish its checks before it can be applied."
                            tooltipText: root.dialog.editCommitted
                                         ? "Close the finished edit review."
                                         : root.dialog.sampleReviewWaiting()
                                           ? "Apply the reviewed look to every selected photo."
                                           : "Apply this checked edit to the photo."
                            onClicked: {
                                if (root.dialog.editCommitted)
                                    root.dialog.close()
                                else
                                    root.dialog.acceptDraft()
                            }
                        }
                    }
                }
            }
        }
    }

    AgentSessionTerminal {
        id: sessionTerminal
        anchors.fill: parent
        visible: root.sessionFlowAvailable
        dialog: root.dialog
        themeObject: root.themeObject
        sessionController: root.sessionController
    }
}
