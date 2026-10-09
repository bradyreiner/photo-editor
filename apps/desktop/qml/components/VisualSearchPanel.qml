import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    property var controller: null
    property bool similarityAvailable: false
    property string similarityStatus: ""
    property bool similarityMode: false
    signal similarityModeRequested(bool enabled)
    property var modelConfig: ({})
    property int selectedCount: 0
    property string selectedAssetId: ""
    property bool filterActive: false
    property bool agentAvailable: false
    signal searchRequested(string query)
    signal showMatchesRequested()
    signal clearRequested()
    signal editRequested()
    signal searchEditRequested()
    signal settingsRequested()
    signal forgetIndexRequested()
    signal modeRequested(bool useIndex)
    implicitHeight: content.implicitHeight + 24
    color: theme.alpha(theme.ink950, 0.65)
    radius: 14
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.3)
    Theme { id: theme }

    function selectedEvidence() {
        if (!root.controller) return ""
        const results = root.controller.results
        for (let i = 0; i < results.length; ++i) {
            if (String(results[i].assetId) === root.selectedAssetId)
                return (results[i].evidenceSource === "saved-description" ? "From saved description: "
                      : results[i].evidenceSource === "image-verification" ? "From photo detail check: "
                      : results[i].evidenceSource === "visual-similarity" ? "Visual similarity: "
                      : "Why this matched: ") + String(results[i].reason)
        }
        return ""
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 8

        Label {
            Layout.fillWidth: true
            visible: !root.similarityAvailable && root.similarityStatus.length > 0
            text: root.similarityStatus
            textFormat: Text.PlainText
            color: theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.similarityAvailable
            ToolOptionToggle {
                objectName: "visualSearchSimilarityToggle"
                label: "Similarity search · SigLIP 2"
                helperText: "Ranks similar photos locally. Specific details are not verified."
                checked: root.similarityMode
                toggleEnabled: root.controller && !root.controller.running
                onToggled: function(nextChecked) { root.similarityModeRequested(nextChecked) }
            }
            Label {
                Layout.fillWidth: true
                text: root.similarityMode ? "Ranks similar photos locally. Specific details are not verified."
                                         : "Detail search checks whether the photo satisfies your description."
                color: theme.textMuted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.similarityMode
            ToolOptionToggle {
                objectName: "visualSearchIndexToggle"
                label: "Reuse saved image descriptions"
                helperText: "Reuse descriptions saved on this computer, then check missing details against local previews."
                checked: Boolean(!root.similarityMode && root.controller && root.controller.useIndex)
                toggleEnabled: root.controller && !root.controller.running
                onToggled: function(nextChecked) { root.modeRequested(nextChecked) }
            }
            Label {
                Layout.fillWidth: true
                text: !root.similarityMode && root.controller && root.controller.useIndex
                      ? "Saved locally. Missing query details are checked against local previews; these extra checks can take longer."
                      : "Direct inspection checks each photo against your query."
                color: theme.textMuted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            ActionButton {
                objectName: "visualSearchForgetIndexButton"
                text: "Forget index"
                showText: true
                compact: true
                enabled: root.controller && !root.controller.running
                tooltipText: "Remove saved AI descriptions only. Photos and edits are unchanged; descriptions can be rebuilt."
                onClicked: root.forgetIndexRequested()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ToneTextField {
                id: queryInput
                objectName: "visualSearchQueryInput"
                Layout.fillWidth: true
                placeholderText: "Describe a photo — e.g. a person outdoors with warm backlight"
                enabled: root.controller && !root.controller.running
                Accessible.name: "Visual search description"
                onAccepted: if (text.trim().length > 0) root.searchRequested(text)
            }
            ActionButton {
                objectName: "visualSearchStartButton"
                text: "Find photos"
                showText: true
                compact: true
                enabled: root.controller && !root.controller.running && queryInput.text.trim().length > 0
                onClicked: root.searchRequested(queryInput.text)
            }
            ActionButton {
                objectName: "visualSearchCancelButton"
                visible: root.controller && root.controller.running
                text: "Stop"
                showText: true
                compact: true
                onClicked: root.controller.cancel()
            }
        }
        Label {
            objectName: "visualSearchStatus"
            Layout.fillWidth: true
            text: root.controller ? root.controller.status : "Local visual search is unavailable in this runtime."
            textFormat: Text.PlainText
            color: theme.textStrong
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
        }
        Label {
            Layout.fillWidth: true
            visible: root.controller && root.controller.indexWarning.length > 0
            text: root.controller ? root.controller.indexWarning : ""
            color: theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: "Local previews only · " + String((root.controller && root.controller.modelName) || root.modelConfig.name || "Choose a vision model in AI Edit")
                  + (root.controller && root.controller.query.length > 0 ? " · Query: “" + root.controller.query + "”" : " · Searches the current filtered view")
            textFormat: Text.PlainText
            color: theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                objectName: "visualSearchMoreButton"
                visible: root.controller && root.controller.hasMore
                text: "Scan next 32"
                showText: true
                compact: true
                onClicked: root.controller.searchMore()
            }
            ActionButton {
                objectName: "visualSearchShowButton"
                visible: root.controller && !root.controller.running && root.controller.inspected > 0
                text: (root.similarityMode ? "Show similar (" : "Show matches (") + (root.controller ? root.controller.results.length : 0) + ")"
                showText: true
                compact: true
                onClicked: root.showMatchesRequested()
            }
            ActionButton {
                objectName: "visualSearchClearButton"
                visible: root.filterActive
                text: "Show all in view"
                showText: true
                compact: true
                enabled: root.controller && !root.controller.running
                onClicked: root.clearRequested()
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "visualSearchWorkflowButton"
                text: "Find & edit…"
                showText: true
                compact: true
                enabled: root.agentAvailable && Boolean(root.modelConfig.vision)
                onClicked: root.searchEditRequested()
            }
            ActionButton {
                objectName: "visualSearchSettingsButton"
                visible: !root.modelConfig.vision
                text: "AI Edit settings"
                showText: true
                compact: true
                enabled: root.agentAvailable && root.controller && !root.controller.running
                onClicked: root.settingsRequested()
            }
            ActionButton {
                objectName: "visualSearchEditButton"
                text: "Edit selected (" + root.selectedCount + ")"
                showText: true
                compact: true
                enabled: root.agentAvailable && root.filterActive && root.selectedCount > 0 && root.controller && !root.controller.running
                tooltipText: "Choose an edit instruction, then review the editable draft before keeping changes"
                onClicked: root.editRequested()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.filterActive && text.length > 0
            text: root.selectedEvidence()
            textFormat: Text.PlainText
            color: theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }
}
