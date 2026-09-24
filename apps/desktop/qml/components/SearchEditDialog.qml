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
    closePolicy: Popup.NoAutoClose
    width: Math.min(780, parent ? parent.width - 48 : 780)
    height: Math.min(confirming ? Math.min(720, 400 + workflow.search.results.length * 100) : 420,
                     parent ? parent.height - 48 : 720)
    padding: 24
    Theme { id: theme }
    background: Rectangle { color: theme.ink900; radius: 24; border.color: theme.lineSoft }
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
            }
        }
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
    function firstPreviewReady() {
        const row = matches.itemAtIndex(0) as MatchRow
        return row !== null && row.previewReady
    }
    component MatchRow: Rectangle {
        id: matchRow
        required property var modelData
        readonly property bool previewReady: matchImage.status === Image.Ready
        width: ListView.view.width
        height: 92
        radius: 12
        color: theme.ink950
        RowLayout {
            anchors.fill: parent; anchors.margins: 8; spacing: 10
            CheckBox {
                checked: root.chosenIds.indexOf(String(matchRow.modelData.assetId)) >= 0
                Accessible.name: "Include " + String(matchRow.modelData.assetId)
                onClicked: root.toggleTarget(String(matchRow.modelData.assetId))
            }
            Image {
                id: matchImage
                objectName: "searchEditMatchPreview"
                Layout.preferredWidth: 90; Layout.preferredHeight: 72
                source: matchRow.modelData.thumbnailSource || matchRow.modelData.filePath || ""
                asynchronous: true; fillMode: Image.PreserveAspectFit
            }
            Label {
                Layout.fillWidth: true
                text: String(matchRow.modelData.reason || "")
                textFormat: Text.PlainText
                color: theme.textStrong; wrapMode: Text.Wrap
                maximumLineCount: 3; elide: Text.ElideRight
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: root.confirming ? "Are these the photos?" : "Find photos. Make your edit."
                color: theme.textStrong
                font.pixelSize: 24
                font.bold: true
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
            TextArea {
                id: instructionInput
                objectName: "searchEditInstruction"
                enabled: !root.searching
                placeholderText: "Find my outdoor portraits, warm the background, and keep the people unchanged."
                wrapMode: TextEdit.Wrap
                color: theme.textStrong
                selectByMouse: true
                Accessible.name: "Photos to find and edits to make"
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
            ScrollBar.vertical: ScrollBar {}
            delegate: MatchRow {}
        }
        ScrollView {
            visible: root.confirming
            Layout.fillWidth: true
            Layout.preferredHeight: root.confirming ? 90 : 0
            TextArea {
                id: editInput
                objectName: "searchEditDirection"
                wrapMode: TextEdit.Wrap
                color: theme.textStrong; selectByMouse: true
                Accessible.name: "Review and correct the edit instructions"
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
                showText: true
                enabled: root.workflow && !root.searching && instructionInput.text.trim().length > 0
                onClicked: root.workflow.start(instructionInput.text, root.modelConfig)
            }
            ActionButton {
                objectName: "searchEditConfirm"
                visible: root.confirming
                text: "Create draft on " + root.chosenIds.length + (root.chosenIds.length === 1 ? " photo" : " photos")
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
