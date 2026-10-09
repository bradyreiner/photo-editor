pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    objectName: "searchEditDialog"
    property var workflow: null
    property var modelConfig: ({})
    property var chosenIds: []
    readonly property bool confirming: workflow && workflow.phase === "confirm"
    readonly property bool searching: workflow && workflow.search.running
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    Timer {
        id: initialFocusTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (root.opened && !root.confirming)
                instructionInput.forceActiveFocus(Qt.PopupFocusReason)
        }
    }
    width: Math.min(780, parent ? parent.width - 48 : 780)
    height: Math.min(confirming ? Math.min(720, 400 + workflow.search.results.length * 100) : 420,
                     parent ? parent.height - 48 : 720)
    padding: theme.panelPaddingStandard
    Theme { id: theme }
    background: Rectangle {
        color: theme.ink900
        radius: theme.radiusPanel
        border.color: theme.lineSoft
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Search & Edit")
        Accessible.description: qsTr("Find photos, review matching targets, and create an edit draft.")
        Accessible.ignored: false
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220 }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 280; easing.type: Easing.OutCubic }
        }
    }
    Connections {
        target: root.workflow
        function onChanged() {
            if (root.confirming && root.workflow.confirmationToken !== tokenLabel.text) {
                root.chosenIds = root.workflow.search.resultIds.slice()
                editInput.text = String(root.workflow.search.searchPlan.editInstructions || "")
                tokenLabel.text = root.workflow.confirmationToken
                Qt.callLater(function() { root.focusMatchSelection() })
            }
        }
    }
    onOpened: {
        if (confirming)
            Qt.callLater(function() { root.focusMatchSelection() })
        else
            initialFocusTimer.start()
    }
    // Bind the approval gesture to exactly the result generation displayed here.
    Item { id: tokenLabel; property string text: "" }
    function toggleTarget(id) {
        const next = root.chosenIds.slice()
        const index = next.indexOf(id)
        if (index >= 0) next.splice(index, 1)
        else next.push(id)
        root.chosenIds = next
    }
    function matchDisplayName(asset) {
        const item = asset || ({})
        const explicitName = String(item.displayLabel || item.fileName || item.name || "").trim()
        if (explicitName.length > 0)
            return explicitName
        const path = String(item.filePath || item.sourcePath || "").trim()
        const pathParts = path.split(/[\\/]/)
        const fileName = pathParts.length > 0 ? String(pathParts[pathParts.length - 1]).trim() : ""
        return fileName.length > 0 ? fileName : String(item.assetId || "matched photo")
    }
    function firstPreviewReady() {
        const row = matches.itemAtIndex(0) as MatchRow
        return row !== null && row.previewReady
    }
    function focusMatchSelection() {
        const row = matches.itemAtIndex(0) as MatchRow
        if (root.opened && row)
            row.focusSelection()
    }
    component MatchRow: Rectangle {
        id: matchRow
        required property var modelData
        readonly property bool previewReady: matchImage.status === Image.Ready
        width: ListView.view.width
        height: theme.scaled(92)
        radius: theme.radiusControl
        color: theme.ink950
        RowLayout {
            anchors.fill: parent; anchors.margins: theme.space8; spacing: theme.space10
            ToneCheckBox {
                id: includePhoto
                objectName: "searchEditIncludePhoto_" + String(matchRow.modelData.assetId)
                Layout.preferredWidth: theme.iconButtonSizeCompact
                checked: root.chosenIds.indexOf(String(matchRow.modelData.assetId)) >= 0
                accessibleLabel: "Include " + root.matchDisplayName(matchRow.modelData)
                accessibleDescription: String(matchRow.modelData.reason || "Include this matching photo in the edit.")
                onClicked: root.toggleTarget(String(matchRow.modelData.assetId))
            }
            Image {
                id: matchImage
                objectName: "searchEditMatchPreview"
                Layout.preferredWidth: theme.scaled(90); Layout.preferredHeight: theme.scaled(72)
                source: matchRow.modelData.thumbnailSource || matchRow.modelData.filePath || ""
                asynchronous: true; fillMode: Image.PreserveAspectFit
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space4
                Label {
                    id: matchName
                    objectName: "searchEditMatchName_" + String(matchRow.modelData.assetId)
                    Layout.fillWidth: true
                    text: root.matchDisplayName(matchRow.modelData)
                    textFormat: Text.PlainText
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                    HoverHandler { id: nameHover }
                    ToolTip.visible: nameHover.hovered
                    ToolTip.text: matchName.text
                }
                Label {
                    Layout.fillWidth: true
                    text: String(matchRow.modelData.reason || "")
                    textFormat: Text.PlainText
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.Wrap
                    maximumLineCount: 3; elide: Text.ElideRight
                }
            }
        }
        function focusSelection() {
            includePhoto.forceActiveFocus(Qt.TabFocusReason)
        }
    }
    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: root.confirming ? "Review matching photos" : "Search & Edit"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeDialogTitle
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            ActionButton {
                objectName: "searchEditClose"
                text: "Close"; showText: true; compact: true
                onClicked: { if (root.workflow) root.workflow.cancel(); root.close() }
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.confirming ? "Only checked photos will be edited. Review the interpreted direction below."
                                 : "Describe which photos to find and what to change. Search stays within the current filtered library view."
            color: theme.textMuted; wrapMode: Text.Wrap
        }
        ScrollView {
            visible: !root.confirming
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            ToneTextArea {
                id: instructionInput
                objectName: "searchEditInstruction"
                accessibleLabel: "Photos to find and edit instructions"
                enabled: !root.searching
                placeholderText: "Find my outdoor portraits, warm the background, and keep the people unchanged."
                wrapMode: TextEdit.Wrap
                color: theme.textStrong
                selectByMouse: true
            }
        }
        Label {
            visible: root.confirming
            Layout.fillWidth: true
            text: "Finding: " + String(root.workflow ? root.workflow.search.searchPlan.query || "" : "")
            textFormat: Text.PlainText
            color: theme.textMuted; wrapMode: Text.Wrap
        }
        ListView {
            id: matches
            objectName: "searchEditMatches"
            visible: root.confirming
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.confirming ? 100 : 0
            clip: true
            spacing: 8
            model: root.workflow ? root.workflow.search.results : []
            onCountChanged: {
                if (root.confirming)
                    Qt.callLater(function() { root.focusMatchSelection() })
            }
            onVisibleChanged: {
                if (visible && root.confirming)
                    Qt.callLater(function() { root.focusMatchSelection() })
            }
            ScrollBar.vertical: ScrollBar {}
            delegate: MatchRow {}
        }
        ScrollView {
            visible: root.confirming
            Layout.fillWidth: true
            Layout.preferredHeight: root.confirming ? 90 : 0
            ToneTextArea {
                id: editInput
                objectName: "searchEditDirection"
                accessibleLabel: "Review and correct the edit instructions"
                wrapMode: TextEdit.Wrap
                color: theme.textStrong; selectByMouse: true
            }
        }
        Label {
            objectName: "searchEditStatus"
            Layout.fillWidth: true
            text: root.workflow ? root.workflow.status : "Unavailable"
            textFormat: Text.PlainText
            color: theme.textMuted; wrapMode: Text.Wrap
            font.pixelSize: 12
        }
        Label {
            Layout.fillWidth: true
            text: "Local model: " + String(root.workflow && root.workflow.search.modelName.length > 0
                                           ? root.workflow.search.modelName : root.modelConfig.name || "Choose a vision model in AI Edit")
            textFormat: Text.PlainText
            color: theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                visible: root.confirming && root.workflow.search.hasMore
                text: "Scan next 32"; showText: true; compact: true
                onClicked: root.workflow.scanMore()
            }
            ActionButton {
                visible: root.confirming
                text: "Start over"; showText: true; compact: true
                onClicked: root.workflow.cancel()
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "searchEditFind"
                visible: !root.confirming
                text: root.searching ? "Searching…" : "Find photos"
                tone: "primary"
                showText: true
                enabled: root.workflow && !root.searching && instructionInput.text.trim().length > 0
                onClicked: root.workflow.start(instructionInput.text, root.modelConfig)
            }
            ActionButton {
                objectName: "searchEditConfirm"
                visible: root.confirming
                text: "Create draft on " + root.chosenIds.length + (root.chosenIds.length === 1 ? " photo" : " photos")
                tone: "primary"
                showText: true
                enabled: root.chosenIds.length > 0 && editInput.text.trim().length > 0
                onClicked: {
                    const result = root.workflow.confirmTargets(tokenLabel.text, root.chosenIds, editInput.text)
                    if (result.accepted) root.close()
                }
            }
        }
    }
}
