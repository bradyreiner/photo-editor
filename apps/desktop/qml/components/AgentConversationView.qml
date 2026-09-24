pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

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
            return dialog.scopeCount(dialog.scopeMode) >= 3
                    ? "I have the photos from your workspace. Tell me how they should feel; I’ll show samples before touching the rest."
                    : "I have the photo from your workspace. Tell me how it should feel; you can undo the edit until you apply it."
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

    component SoftButton: Button {
        id: button
        property bool primary: false
        property bool selected: false
        implicitHeight: 44
        implicitWidth: Math.max(118, buttonLabel.implicitWidth + 34)
        leftPadding: 17
        rightPadding: 17

        background: Rectangle {
            radius: 12
            color: button.primary
                   ? (button.down ? Qt.darker(root.primaryAccent, 1.08) : root.primaryAccent)
                   : button.selected
                     ? root.themeObject.alpha(root.primaryAccent, button.hovered ? 0.30 : 0.22)
                   : (button.hovered ? root.themeObject.alpha(root.paper, 0.16)
                                     : root.themeObject.alpha(root.paper, 0.09))
            border.width: button.selected ? 1 : 0
            border.color: root.themeObject.alpha(root.primaryAccent, 0.72)
            opacity: button.enabled ? 1 : 0.42
            Behavior on color { ColorAnimation { duration: 120 } }
        }

        contentItem: Label {
            id: buttonLabel
            text: button.text
            color: root.paper
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        HoverHandler { cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor }
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
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    width: parent.width
                    text: card.photo ? "Reference photo" : String(card.suggestion.detail || "")
                    color: root.themeObject.alpha(root.paper, 0.62)
                    font.family: root.uiFont
                    font.pixelSize: 10
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
        anchors.fill: parent
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 212
            Layout.minimumHeight: 212
            Layout.maximumHeight: 212
            spacing: 18

            LooksyAgentCharacter {
                objectName: "agentCharacterDirection"
                Layout.preferredWidth: 204
                Layout.preferredHeight: 204
                Layout.alignment: Qt.AlignVCenter
                mode: root.dialog.agentVisualMode()
                tool: root.dialog.agentVisualTool()
                talking: root.dialog.visible
                celebrating: root.dialog.editCommitted
                accentColor: root.primaryAccent
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"

                ColumnLayout {
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
                        font.pixelSize: 25
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.pageDetail()
                        color: root.themeObject.alpha(root.paper, 0.62)
                        font.family: root.uiFont
                        font.pixelSize: 13
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
                    anchors.leftMargin: 204
                    anchors.rightMargin: 4
                    anchors.topMargin: 8
                    anchors.bottomMargin: 4
                    spacing: 14

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
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
                                    text: "EDITING"
                                    color: root.themeObject.alpha(root.paper, 0.50)
                                    font.family: root.themeObject.fontMono
                                    font.pixelSize: 9
                                    font.letterSpacing: 1.2
                                }
                                Label {
                                    objectName: "agentTargetSummary"
                                    Layout.fillWidth: true
                                    text: root.dialog.targetSummary()
                                    color: root.paper
                                    font.family: root.uiFont
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }

                            Label {
                                text: root.dialog.scopeCount(root.dialog.scopeMode) +
                                      " photo" + (root.dialog.scopeCount(root.dialog.scopeMode) === 1 ? "" : "s")
                                color: root.themeObject.alpha(root.paper, 0.54)
                                font.family: root.themeObject.fontMono
                                font.pixelSize: 10
                            }
                        }
                    }

                    Rectangle {
                        id: styleStartCard
                        objectName: "agentStyleStartCard"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        color: "transparent"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            anchors.topMargin: 10
                            anchors.bottomMargin: 10
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    Label {
                                        text: "YOUR LOOK"
                                        color: root.themeObject.alpha(root.paper, 0.50)
                                        font.family: root.themeObject.fontMono
                                        font.pixelSize: 9
                                        font.letterSpacing: 1.2
                                    }

                                    Label {
                                        objectName: "agentCurrentProjectLookLabel"
                                        Layout.fillWidth: true
                                        text: root.dialog.selectedStyleName()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: 13
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
                                    font.pixelSize: 10
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
                                    text: "Learn from edits"
                                    onClicked: root.dialog.showEditedLooks()
                                }

                                SoftButton {
                                    objectName: "agentDescribeLookButton"
                                    Layout.fillWidth: true
                                    text: "Make this my look"
                                    enabled: promptEditor.text.trim().length >= 4
                                    onClicked: root.dialog.saveWordsAsLook()
                                }

                                SoftButton {
                                    objectName: "agentBuiltInLooksButton"
                                    Layout.fillWidth: true
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
                                    text: "EDITING FREEDOM"
                                    color: root.themeObject.alpha(root.paper, 0.50)
                                    font.family: root.themeObject.fontMono
                                    font.pixelSize: 9
                                    font.letterSpacing: 1.1
                                }

                                Label {
                                    objectName: "agentEditingFreedomSummary"
                                    Layout.fillWidth: true
                                    text: root.dialog.editingFreedomSummary()
                                    color: root.themeObject.alpha(root.paper, 0.64)
                                    font.family: root.uiFont
                                    font.pixelSize: 10
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
                        Layout.preferredHeight: root.dialog.detailsExpanded ? 210 : 0
                        visible: root.dialog.detailsExpanded
                        radius: 16
                        color: root.themeObject.alpha(root.paper, 0.065)
                        clip: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 82
                                spacing: 18

                                ColumnLayout {
                                    Layout.preferredWidth: 184
                                    spacing: 5

                                    Label {
                                        text: "SCOPE"
                                        color: root.themeObject.alpha(root.paper, 0.46)
                                        font.family: root.themeObject.fontMono
                                        font.pixelSize: 9
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
                                        font.pixelSize: 11
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
                                        text: "LOOK  " + root.dialog.selectedStyleName()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentDetailsTasteLabel"
                                        Layout.fillWidth: true
                                        text: "TASTE  " + root.dialog.tasteMirrorLabel()
                                        color: root.themeObject.alpha(root.paper, 0.62)
                                        font.family: root.uiFont
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentDetailsEditingFreedomLabel"
                                        Layout.fillWidth: true
                                        text: "FREEDOM  " + root.dialog.editingFreedomMode().toUpperCase()
                                        color: root.themeObject.alpha(root.paper, 0.62)
                                        font.family: root.uiFont
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        objectName: "agentDetailsModelLabel"
                                        Layout.fillWidth: true
                                        text: "MODEL  " + root.dialog.activeModelLabel()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentLiveMcpStatusLabel"
                                        Layout.fillWidth: true
                                        text: "TOOLS  " + root.dialog.localToolsLabel()
                                        color: root.themeObject.alpha(root.paper, 0.62)
                                        font.family: root.uiFont
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Accessible.description: root.dialog.mcpEndpointLabel()
                                    }

                                    Label {
                                        objectName: "agentDetailsRetouchRuntimeLabel"
                                        Layout.fillWidth: true
                                        text: "RETOUCH  " + root.dialog.retouchRuntimeLabel()
                                        color: root.themeObject.alpha(root.paper, 0.62)
                                        font.family: root.uiFont
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: root.themeObject.alpha(root.paper, 0.10)
                            }

                            RowLayout {
                                id: runAuditPanel
                                objectName: "agentLiveRunAuditPanel"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        objectName: "agentLiveMemoryVaultLabel"
                                        Layout.fillWidth: true
                                        text: "MEMORY  " + root.dialog.memoryVaultLabel()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentLiveStyleChangeSummaryLabel"
                                        Layout.fillWidth: true
                                        text: "LEARNED  " + root.dialog.latestStyleChangeSummary()
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentLiveRunProofPathLabel"
                                        Layout.fillWidth: true
                                        text: "RUN  " + root.dialog.runAuditSummary() + " · " +
                                              root.dialog.proofArtifactLabel()
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                        Accessible.description: root.dialog.latestToolCallSummary()
                                    }

                                    Label {
                                        objectName: "agentLiveRunWarningsLabel"
                                        Layout.fillWidth: true
                                        visible: root.dialog.runWarnings().length > 0 ||
                                                 root.dialog.detailsActionMessage.length > 0
                                        text: root.dialog.detailsActionMessage.length > 0
                                              ? root.dialog.detailsActionMessage
                                              : root.dialog.runWarnings().join(" · ")
                                        color: root.dialog.runWarnings().length > 0
                                               ? root.themeObject.warning
                                               : root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }

                                SoftButton {
                                    objectName: "agentOpenMemoryVaultButton"
                                    text: "Open vault"
                                    onClicked: root.dialog.openMemoryVault()
                                }

                                SoftButton {
                                    objectName: "agentOpenMemoryVaultInObsidianButton"
                                    text: "Obsidian"
                                    onClicked: root.dialog.openMemoryVaultInObsidian()
                                }

                                SoftButton {
                                    objectName: "agentExportRedactedMemoryButton"
                                    text: "Export safe copy"
                                    onClicked: root.dialog.exportRedactedMemoryVault()
                                }
                            }
                        }
                    }

                    Label {
                        text: "YOUR GOAL"
                        color: root.themeObject.alpha(root.paper, 0.54)
                        font.family: root.themeObject.fontMono
                        font.pixelSize: 10
                        font.letterSpacing: 1.5
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 20
                        color: root.themeObject.alpha(root.paper, 0.075)
                        border.width: promptEditor.activeFocus || root.dialog.directionError.length > 0 ? 1 : 0
                        border.color: root.dialog.directionError.length > 0 ? root.themeObject.error
                                                                                : root.themeObject.alpha(root.paper, 0.36)

                        Label {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 18
                            visible: promptEditor.text.length === 0
                            text: "Make the afternoon feel quiet and cinematic. Keep the skin natural, lift the shadows, and crop for a stronger portrait…"
                            color: root.themeObject.alpha(root.paper, 0.34)
                            font.family: root.uiFont
                            font.pixelSize: 17
                            lineHeight: 1.25
                            wrapMode: Text.WordWrap
                        }

                        TextArea {
                            id: promptEditor
                            objectName: "agentPromptEditor"
                            anchors.fill: parent
                            anchors.margins: 18
                            text: root.dialog.hasController()
                                  ? String(root.dialog.agentEditController.userInstructions || "") : ""
                            placeholderText: "Make the afternoon feel quiet and cinematic. Keep the skin natural, lift the shadows, and crop for a stronger portrait…"
                            placeholderTextColor: root.themeObject.alpha(root.paper, 0.34)
                            color: root.paper
                            selectionColor: root.primaryAccent
                            selectedTextColor: root.paper
                            font.family: root.uiFont
                            font.pixelSize: 17
                            wrapMode: TextEdit.Wrap
                            background: Item {}
                            onTextChanged: {
                                if (root.dialog.hasController())
                                    root.dialog.agentEditController.userInstructions = text
                                root.dialog.directionError = ""
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
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Accessible.description: root.dialog.batchForecastExplanation()
                        }

                        SoftButton {
                            id: detailsButton
                            objectName: "agentDetailsToggle"
                            text: root.dialog.detailsExpanded ? "Hide Details" : "Details"
                            onClicked: root.dialog.setDetailsExpanded(!root.dialog.detailsExpanded)
                        }

                        SoftButton {
                            objectName: "agentAddInspirationButton"
                            visible: root.dialog.detailsExpanded
                            text: "Add inspiration"
                            enabled: promptEditor.text.trim().length > 0
                            onClicked: root.dialog.preparePromptSuggestions()
                        }

                        SoftButton {
                            objectName: "agentPrimaryCreateDraftButton"
                            text: root.dialog.runButtonLabel()
                            primary: true
                            enabled: root.dialog.directionReady()
                            onClicked: root.dialog.triggerPrimaryDirectionAction()
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 32
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
                            font.family: root.themeObject.fontMono
                            font.pixelSize: 10
                            font.letterSpacing: 0.8
                        }
                        Button {
                            id: changePromptButton
                            text: "Change prompt"
                            flat: true
                            onClicked: root.dialog.setPage(0)
                            contentItem: Label {
                                text: changePromptButton.text
                                color: root.themeObject.alpha(root.paper, 0.74)
                                font.family: root.uiFont
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Label {
                        visible: root.dialog.styleStartMode !== "learn"
                        text: "LOOK"
                        color: root.themeObject.alpha(root.paper, 0.76)
                        font.family: root.themeObject.fontMono
                        font.pixelSize: 10
                        font.letterSpacing: 1.4
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 172 : 0
                        visible: root.dialog.styleStartMode !== "learn"
                        orientation: ListView.Horizontal
                        spacing: 10
                        clip: true
                        model: root.presetSuggestions()
                        delegate: SuggestionCard {
                            required property var modelData
                            suggestion: modelData
                            width: 228
                            height: 172
                            onChosen: root.dialog.toggleSuggestion(suggestion)
                        }
                    }

                    Label {
                        visible: root.dialog.styleStartMode !== "built-in"
                        text: "OPTIONAL PHOTO REFERENCES"
                        color: root.themeObject.alpha(root.paper, 0.76)
                        font.family: root.themeObject.fontMono
                        font.pixelSize: 10
                        font.letterSpacing: 1.4
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: visible ? 148 : 0
                        visible: root.dialog.styleStartMode !== "built-in"
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
                            font.pixelSize: 11
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
                    anchors.leftMargin: 32
                    anchors.rightMargin: 32
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18

                        LooksyAgentCharacter {
                            objectName: "agentCharacterWorking"
                            Layout.preferredWidth: 176
                            Layout.preferredHeight: 176
                            mode: root.dialog.agentVisualMode()
                            tool: root.dialog.agentVisualTool()
                            talking: root.dialog.agentRunning()
                            celebrating: root.dialog.editCommitted
                            accentColor: root.primaryAccent
                        }

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
                                font.pixelSize: 25
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
                                font.pixelSize: 13
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
                                font.family: root.themeObject.fontMono
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        SoftButton {
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
                        font.pixelSize: 11
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
                                font.pixelSize: 13
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
                                    text: liveAgentPreview.status === Image.Ready ? "LIVE PREVIEW" : "PREVIEW"
                                    color: root.themeObject.alpha(root.paper, 0.82)
                                    font.family: root.themeObject.fontMono
                                    font.pixelSize: 9
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
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            spacing: 8

                            Label {
                                text: "WHAT I’M DOING"
                                color: root.themeObject.alpha(root.paper, 0.52)
                                font.family: root.themeObject.fontMono
                                font.pixelSize: 10
                                font.letterSpacing: 1.2
                            }

                            ListView {
                                objectName: "agentLiveActivityTimeline"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumWidth: 310
                                clip: true
                                spacing: 8
                                model: root.dialog.runEvents().slice(-8).reverse()
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
                                                font.pixelSize: 12
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
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Label {
                                            text: String(activityEventRow.modelData.status || "")
                                            color: root.themeObject.alpha(root.paper, 0.42)
                                            font.family: root.themeObject.fontMono
                                            font.pixelSize: 9
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
                    anchors.leftMargin: 32
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
                                    text: "EDIT PLAN"
                                    color: root.themeObject.alpha(root.paper, 0.48)
                                    font.family: root.themeObject.fontMono
                                    font.pixelSize: 9
                                    font.letterSpacing: 1
                                }

                                Label {
                                    objectName: "aiEditPlanSummary"
                                    Layout.fillWidth: true
                                    text: root.dialog.planSummary()
                                    color: root.paper
                                    font.family: root.uiFont
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: root.dialog.editIntentSummary()
                                    color: root.themeObject.alpha(root.paper, 0.50)
                                    font.family: root.themeObject.fontMono
                                    font.pixelSize: 9
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
                                    text: "VERIFICATION"
                                    color: root.themeObject.alpha(root.paper, 0.48)
                                    font.family: root.themeObject.fontMono
                                    font.pixelSize: 9
                                    font.letterSpacing: 1
                                }

                                Label {
                                    objectName: "aiEditVerificationStatus"
                                    Layout.fillWidth: true
                                    text: root.dialog.validationLabel()
                                    color: root.dialog.goalMatchPassed()
                                           ? root.themeObject.success : root.themeObject.warning
                                    font.family: root.uiFont
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: consistencyMapPanel
                        objectName: "aiEditConsistencyMap"
                        readonly property string consistencyContract:
                            "agent-professional-consistency-map-v1|basis:within-lighting-group-relative-render-response|metrics:tone-contrast-color-skin|ordering:stable|thresholds:fixed|digest:sha256"
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 96 : 0
                        visible: !root.dialog.sampleReviewWaiting() &&
                                 Boolean(root.dialog.consistencyMap().available)
                        radius: 16
                        color: root.themeObject.alpha(root.paper, 0.075)
                        border.width: 1
                        border.color: String(root.dialog.consistencyMap().status || "") === "attention"
                                      ? root.themeObject.alpha(root.primaryAccent, 0.52)
                                      : root.themeObject.alpha(root.themeObject.success, 0.44)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 14

                            ColumnLayout {
                                Layout.preferredWidth: 285
                                Layout.minimumWidth: 285
                                Layout.maximumWidth: 285
                                Layout.fillHeight: true
                                spacing: 2

                                Label {
                                    text: "CONSISTENCY MAP"
                                    color: root.themeObject.alpha(root.paper, 0.50)
                                    font.family: root.themeObject.fontMono
                                    font.pixelSize: 9
                                    font.letterSpacing: 1.1
                                }

                                Label {
                                    objectName: "aiEditConsistencyHeadline"
                                    Layout.fillWidth: true
                                    text: root.dialog.consistencyHeadline()
                                    color: root.paper
                                    font.family: root.displayFont
                                    font.pixelSize: 17
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Label {
                                    objectName: "aiEditConsistencySummary"
                                    Layout.fillWidth: true
                                    text: root.dialog.consistencySummary()
                                    color: root.themeObject.alpha(root.paper, 0.62)
                                    font.family: root.uiFont
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 1
                                Layout.fillHeight: true
                                color: root.themeObject.alpha(root.paper, 0.10)
                            }

                            ListView {
                                id: consistencyGroupList
                                objectName: "aiEditConsistencyGroupList"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 500
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
                                                font.pixelSize: 11
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
                                            font.pixelSize: 10
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
                                        font.pixelSize: 21
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Exception-first review · verified photos stay out of your queue"
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: 11
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
                                            font.family: root.themeObject.fontMono
                                            font.pixelSize: 8
                                            font.weight: Font.DemiBold
                                            font.letterSpacing: 0.7
                                        }

                                        Label {
                                            objectName: "aiEditApprovedStoryHeadline"
                                            Layout.fillWidth: true
                                            text: root.dialog.assignmentStoryHeadline()
                                            color: root.paper
                                            font.family: root.uiFont
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "aiEditApprovedStoryArc"
                                            Layout.fillWidth: true
                                            text: root.dialog.assignmentStoryArcText()
                                            color: root.themeObject.alpha(root.paper, 0.62)
                                            font.family: root.uiFont
                                            font.pixelSize: 9
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
                                        font.family: root.themeObject.fontMono
                                        font.pixelSize: 8
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
                                                          ? "FAILED" : "CHECK"
                                                    color: root.paper
                                                    font.family: root.themeObject.fontMono
                                                    font.pixelSize: 9
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
                                                font.pixelSize: 13
                                                font.weight: Font.DemiBold
                                            }

                                            Label {
                                                objectName: exceptionCard.index === 0
                                                            ? "aiEditExceptionFirstSummary" : ""
                                                Layout.fillWidth: true
                                                text: root.dialog.reviewIssueSummary(exceptionCard.modelData)
                                                color: root.themeObject.alpha(root.paper, 0.70)
                                                font.family: root.uiFont
                                                font.pixelSize: 11
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
                                                font.pixelSize: 10
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
                                font.pixelSize: 11
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
                                font.pixelSize: 20
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
                                        font.family: root.themeObject.fontMono
                                        font.pixelSize: 9
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.8
                                    }

                                    Label {
                                        objectName: "aiEditAssignmentStoryHeadline"
                                        Layout.fillWidth: true
                                        text: root.dialog.assignmentStoryHeadline()
                                        color: root.paper
                                        font.family: root.uiFont
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "aiEditAssignmentStoryArc"
                                        Layout.fillWidth: true
                                        text: root.dialog.assignmentStoryArcText()
                                        color: root.themeObject.alpha(root.paper, 0.78)
                                        font.family: root.uiFont
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "aiEditAssignmentStorySummary"
                                        Layout.fillWidth: true
                                        text: root.dialog.assignmentStorySummary()
                                        color: root.themeObject.alpha(root.paper, 0.58)
                                        font.family: root.uiFont
                                        font.pixelSize: 10
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
                                                font.family: root.themeObject.fontMono
                                                font.pixelSize: 8
                                                font.weight: Font.DemiBold
                                                font.letterSpacing: 0.55
                                            }

                                            Label {
                                                objectName: "aiEditStoryRoleEvidenceReason"
                                                Layout.fillWidth: true
                                                text: root.dialog.shownStoryRoleReason()
                                                color: root.themeObject.alpha(root.paper, 0.66)
                                                font.family: root.uiFont
                                                font.pixelSize: 9
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                objectName: "aiEditStoryRoleEvidenceBoundary"
                                                text: "NO SCENE CLAIM"
                                                color: root.themeObject.alpha(root.paper, 0.34)
                                                font.family: root.themeObject.fontMono
                                                font.pixelSize: 7
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
                                            font.family: root.themeObject.fontMono
                                            font.pixelSize: 8
                                            font.letterSpacing: 0.55
                                            elide: Text.ElideRight
                                        }

                                        Rectangle {
                                            objectName: "aiEditStorySampleNavigator"
                                            Layout.preferredWidth: 94
                                            Layout.preferredHeight: 29
                                            visible: root.dialog.sampleReviewWaiting() &&
                                                     root.dialog.reviewSamples().length > 1
                                            radius: 8
                                            color: root.themeObject.alpha(root.paper, 0.045)
                                            border.width: 1
                                            border.color: root.themeObject.alpha(root.paper, 0.13)

                                            RowLayout {
                                                anchors.fill: parent
                                                spacing: 0

                                                Button {
                                                    id: previousStorySampleButton
                                                    objectName: "aiEditStoryPreviousSample"
                                                    Layout.preferredWidth: 28
                                                    Layout.fillHeight: true
                                                    enabled: root.dialog.canShowPreviousSample()
                                                    text: "‹"
                                                    onClicked: root.dialog.showPreviousSample()

                                                    background: Rectangle {
                                                        radius: 7
                                                        color: previousStorySampleButton.down
                                                               ? root.themeObject.alpha(root.paper, 0.12)
                                                               : "transparent"
                                                    }

                                                    contentItem: Label {
                                                        text: previousStorySampleButton.text
                                                        color: previousStorySampleButton.enabled
                                                               ? root.paper
                                                               : root.themeObject.alpha(root.paper, 0.24)
                                                        font.family: root.uiFont
                                                        font.pixelSize: 17
                                                        horizontalAlignment: Text.AlignHCenter
                                                        verticalAlignment: Text.AlignVCenter
                                                    }
                                                }

                                                Label {
                                                    objectName: "aiEditStorySamplePosition"
                                                    Layout.fillWidth: true
                                                    text: root.dialog.displayedSamplePositionText()
                                                    color: root.themeObject.alpha(root.paper, 0.64)
                                                    font.family: root.themeObject.fontMono
                                                    font.pixelSize: 8
                                                    font.weight: Font.DemiBold
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                }

                                                Button {
                                                    id: nextStorySampleButton
                                                    objectName: "aiEditStoryNextSample"
                                                    Layout.preferredWidth: 28
                                                    Layout.fillHeight: true
                                                    enabled: root.dialog.canShowNextSample()
                                                    text: "›"
                                                    onClicked: root.dialog.showNextSample()

                                                    background: Rectangle {
                                                        radius: 7
                                                        color: nextStorySampleButton.down
                                                               ? root.themeObject.alpha(root.paper, 0.12)
                                                               : "transparent"
                                                    }

                                                    contentItem: Label {
                                                        text: nextStorySampleButton.text
                                                        color: nextStorySampleButton.enabled
                                                               ? root.paper
                                                               : root.themeObject.alpha(root.paper, 0.24)
                                                        font.family: root.uiFont
                                                        font.pixelSize: 17
                                                        horizontalAlignment: Text.AlignHCenter
                                                        verticalAlignment: Text.AlignVCenter
                                                    }
                                                }
                                            }
                                        }

                                        Button {
                                            id: storyRoleButton
                                            objectName: "aiEditAssignmentStoryRoleButton"
                                            Layout.preferredHeight: 29
                                            Layout.preferredWidth: 152
                                            visible: root.dialog.canDirectShownStoryRole()
                                            text: "Shown · " + root.dialog.assignmentStoryRoleLabel(
                                                      root.dialog.shownStoryRole()) + "  ▾"
                                            font.family: root.uiFont
                                            font.pixelSize: 10
                                            font.weight: Font.DemiBold
                                            onClicked: storyRolePopup.open()

                                            background: Rectangle {
                                                radius: 8
                                                color: storyRoleButton.down || storyRolePopup.visible
                                                       ? root.themeObject.alpha(root.primaryAccent, 0.38)
                                                       : root.themeObject.alpha(root.primaryAccent, 0.22)
                                                border.width: 1
                                                border.color: root.themeObject.alpha(root.primaryAccent, 0.58)
                                            }

                                            contentItem: Label {
                                                text: storyRoleButton.text
                                                color: root.paper
                                                font: storyRoleButton.font
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }
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

                                        Popup {
                                            id: storyRolePopup
                                            objectName: "aiEditAssignmentStoryRolePopup"
                                            parent: storyRoleButton
                                            x: storyRoleButton.width - width
                                            y: storyRoleButton.height + 6
                                            width: 260
                                            height: storyRoleMenuColumn.implicitHeight + 12
                                            padding: 6
                                            modal: false
                                            focus: true
                                            closePolicy: Popup.CloseOnEscape |
                                                         Popup.CloseOnPressOutside

                                            background: Rectangle {
                                                objectName: "aiEditAssignmentStoryRolePopupSurface"
                                                radius: 12
                                                color: root.themeObject.alpha(root.ink, 0.98)
                                                border.width: 1
                                                border.color: root.themeObject.alpha(root.primaryAccent, 0.48)
                                            }

                                            contentItem: Column {
                                                id: storyRoleMenuColumn
                                                spacing: 3

                                                Repeater {
                                                    model: root.dialog.assignmentStoryRoleOptions()

                                                    delegate: Button {
                                                        id: storyRoleAction
                                                        required property var modelData
                                                        readonly property string roleValue:
                                                            String(modelData.role || "")
                                                        readonly property bool currentRole:
                                                            root.dialog.shownStoryRole() === roleValue
                                                        objectName: "aiEditStoryRole" +
                                                                    String(modelData.label || "") +
                                                                    "Action"
                                                        width: storyRoleMenuColumn.width
                                                        height: 43
                                                        enabled: root.dialog.canAssignShownStoryRole(
                                                                     roleValue)
                                                        onClicked: {
                                                            const requestedRole = roleValue
                                                            storyRolePopup.close()
                                                            root.dialog.assignShownStoryRole(
                                                                requestedRole)
                                                        }

                                                        background: Rectangle {
                                                            radius: 8
                                                            color: storyRoleAction.currentRole
                                                                   ? root.themeObject.alpha(root.primaryAccent, 0.18)
                                                                   : storyRoleAction.hovered ||
                                                                     storyRoleAction.down
                                                                     ? root.themeObject.alpha(root.paper, 0.10)
                                                                     : "transparent"
                                                            border.width: storyRoleAction.currentRole ? 1 : 0
                                                            border.color: root.themeObject.alpha(
                                                                              root.primaryAccent, 0.38)
                                                        }

                                                        contentItem: Item {
                                                            Label {
                                                                anchors.left: parent.left
                                                                anchors.right: currentRoleLabel.left
                                                                anchors.top: parent.top
                                                                text: String(storyRoleAction.modelData.label || "")
                                                                color: root.paper
                                                                font.family: root.uiFont
                                                                font.pixelSize: 11
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
                                                                font.pixelSize: 9
                                                                elide: Text.ElideRight
                                                            }

                                                            Label {
                                                                id: currentRoleLabel
                                                                anchors.right: parent.right
                                                                anchors.top: parent.top
                                                                visible: storyRoleAction.currentRole
                                                                text: "CURRENT"
                                                                color: root.primaryAccent
                                                                font.family: root.themeObject.fontMono
                                                                font.pixelSize: 8
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
                                        font.pixelSize: 9
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

                                        Label {
                                            anchors.centerIn: parent
                                            text: "✓"
                                            color: root.paper
                                            font.family: root.uiFont
                                            font.pixelSize: 15
                                            font.weight: Font.Bold
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
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "aiEditCorrectionTransferLesson"
                                            Layout.fillWidth: true
                                            text: root.dialog.correctionTransferLesson()
                                            color: root.themeObject.alpha(root.paper, 0.78)
                                            font.family: root.uiFont
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "aiEditCorrectionTransferDetail"
                                            Layout.fillWidth: true
                                            text: root.dialog.correctionTransferDetail()
                                            color: root.themeObject.alpha(root.paper, 0.52)
                                            font.family: root.uiFont
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Label {
                                        text: "MEMORY AFTER APPLY"
                                        color: root.themeObject.alpha(root.paper, 0.48)
                                        font.family: root.themeObject.fontMono
                                        font.pixelSize: 9
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
                                              ? root.dialog.directionDisplayLabel("a") : "BEFORE"
                                        color: root.paper
                                        font.family: root.themeObject.fontMono
                                        font.pixelSize: 10
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
                                              ? root.dialog.directionDisplayLabel("b") : "AFTER"
                                        color: root.paper
                                        font.family: root.themeObject.fontMono
                                        font.pixelSize: 10
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
                                      " selected · remembered only after Commit Edits"
                                color: root.themeObject.success
                                font.family: root.uiFont
                                font.pixelSize: 12
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

                            TextArea {
                                id: alternativeDirectionEditor
                                objectName: "aiEditAlternativeDirectionEditor"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: root.dialog.alternativeDirectionText
                                placeholderText: "Try a distinct direction — cooler, quieter, more editorial…"
                                placeholderTextColor: root.themeObject.alpha(root.paper, 0.38)
                                color: root.paper
                                selectionColor: root.primaryAccent
                                selectedTextColor: root.paper
                                font.family: root.uiFont
                                font.pixelSize: 13
                                wrapMode: TextEdit.Wrap
                                background: Item {}
                                onTextChanged: root.dialog.alternativeDirectionText = text
                            }

                            SoftButton {
                                objectName: "aiEditCreateComparisonButton"
                                text: "Create Direction B"
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

                            TextArea {
                                id: sampleFeedbackEditor
                                objectName: "aiEditSampleFeedbackEditor"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: root.dialog.sampleFeedback
                                placeholderText: "Tell me what to change in these samples…"
                                placeholderTextColor: root.themeObject.alpha(root.paper, 0.38)
                                color: root.paper
                                selectionColor: root.primaryAccent
                                selectedTextColor: root.paper
                                font.family: root.uiFont
                                font.pixelSize: 13
                                wrapMode: TextEdit.Wrap
                                background: Item {}
                                onTextChanged: root.dialog.sampleFeedback = text
                            }

                            SoftButton {
                                objectName: "aiEditSubmitSampleRevisionButton"
                                text: "Make Changes"
                                primary: true
                                enabled: sampleFeedbackEditor.text.trim().length > 0
                                onClicked: root.dialog.reviseSamples()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        SoftButton {
                            objectName: "aiEditDiscardDraftButton"
                            visible: !root.dialog.editCommitted
                            text: root.dialog.sampleReviewWaiting() ? "Discard samples" : "Discard edit"
                            onClicked: root.dialog.rejectDraft()
                        }

                        SoftButton {
                            objectName: "aiEditAskForChangesButton"
                            visible: !root.dialog.editCommitted
                            text: root.dialog.sampleReviewWaiting()
                                  ? (root.dialog.revisionExpanded ? "Cancel changes" : "Make Changes")
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

                        Item { Layout.fillWidth: true }

                        SoftButton {
                            objectName: "aiEditPrimaryReviewButton"
                            visible: !root.dialog.revisionExpanded &&
                                     !root.dialog.comparisonPromptOpen &&
                                     !root.dialog.directionComparisonPending()
                            text: root.dialog.editCommitted
                                  ? "Done"
                                  : root.dialog.reviewIssues().length > 0
                                  ? "Resolve " + root.dialog.reviewIssues().length + " first"
                                  : root.dialog.sampleReviewWaiting()
                                  ? "Apply to All"
                                  : "Commit Edits"
                            primary: true
                            enabled: root.dialog.editCommitted ||
                                     (root.dialog.canReviewDraft() &&
                                      root.dialog.reviewIssues().length <= 0)
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
}
