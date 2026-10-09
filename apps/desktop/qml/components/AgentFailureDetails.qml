pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    objectName: "agentFailureDetails"
    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(680, parent ? parent.width - 32 : 680)
    height: Math.min(520, parent ? parent.height - 32 : 520)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 20
    property string detailsText: ""
    property var returnFocusItem: null
    Theme { id: theme }

    function openFor(report, summary, opener) {
        const saved = report || ({})
        const lines = []
        const seen = ({})
        function append(value) {
            const text = String(value || "").trim()
            if (text.length > 0 && !seen[text]) {
                seen[text] = true
                lines.push(text)
            }
        }
        append(saved.failureReason || summary)
        for (const error of saved.errors || []) append(error)
        if (saved.runId) lines.push("\nRun: " + saved.runId)
        const model = saved.model || ({})
        if (model.name || model.id) lines.push("Model: " + (model.name || model.id))
        const calls = saved.toolCalls || []
        if (calls.length > 0) {
            lines.push("\nTool results")
            for (const call of calls) {
                const outcome = call.outcome || ({})
                lines.push(String(call.status || "unknown") + " · " + String(call.name || outcome.toolName || "Tool"))
                append(outcome.error)
                if (call.status === "failed" && outcome.arguments) {
                    const argumentsText = JSON.stringify(outcome.arguments, null, 2)
                    lines.push("Arguments: " + argumentsText.slice(0, 8192) +
                               (argumentsText.length > 8192 ? "\n[Remaining arguments omitted]" : ""))
                }
            }
        }
        for (const asset of saved.assetReports || []) {
            if (asset.status !== "failed") continue
            lines.push("\nPhoto: " + String(asset.assetId || ""))
            append(asset.failureReason)
            const validation = asset.validationReport || ({})
            for (const error of validation.errors || []) append(error)
            for (const issue of (validation.pixelIntentAlignment || ({})).issues || []) append(issue)
            for (const warning of validation.warnings || []) append(warning)
        }
        if (lines.length === 0) append(summary || "No further error details were recorded.")
        root.detailsText = lines.join("\n")
        root.returnFocusItem = opener || null
        root.open()
    }

    onOpened: detailText.forceActiveFocus(Qt.PopupFocusReason)
    onClosed: {
        const opener = root.returnFocusItem
        if (opener && opener.visible)
            Qt.callLater(function() { opener.forceActiveFocus(Qt.PopupFocusReason) })
    }

    background: PopupSurface { fillColor: theme.ink900 }
    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "Why this edit stopped"
                color: theme.textStrong
                font.pixelSize: 18
                font.bold: true
            }
            ActionButton {
                objectName: "agentFailureDetailsClose"
                text: "Close"
                compact: true
                tone: "quiet"
                onClicked: root.close()
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            TextArea {
                id: detailText
                objectName: "agentFailureDetailsText"
                text: root.detailsText
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: theme.textMain
                selectionColor: theme.primaryAccent
                font.family: theme.fontMono
                font.pixelSize: 12
                background: null
                Accessible.name: "Full agent failure details"
            }
        }
        ActionButton {
            objectName: "agentFailureDetailsCopy"
            text: "Copy details"
            compact: true
            onClicked: { detailText.selectAll(); detailText.copy() }
        }
    }
}
