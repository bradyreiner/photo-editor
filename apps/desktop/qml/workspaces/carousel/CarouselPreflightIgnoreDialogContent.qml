pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtQml
import "../../components"

ColumnLayout {
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselPreflightIgnoreDialog
    anchors.fill: parent
    anchors.margins: 18
    spacing: 12
    Label {
        text: "Ignore preflight warning"
        color: theme.textStrong
        font.family: theme.fontDisplay
        font.pixelSize: 20
        font.weight: Font.Bold
    }
    Label {
        Layout.fillWidth: true
        text: String(root.pendingPreflightIgnoreIssue.message || "Warning")
        color: theme.warning
        font.family: theme.fontUi
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    ToneTextField {
        id: carouselPreflightIgnoreReasonField
        objectName: "carouselPreflightIgnoreReasonField"
        Layout.fillWidth: true
        placeholderText: "Required reason for this exception"
        text: root.preflightIgnoreReason
        onTextEdited: root.preflightIgnoreReason = text
    }
    Label {
        Layout.fillWidth: true
        text: "The reason is saved with this carousel. Artwork is not changed."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 10
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Item { Layout.fillHeight: true }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        ActionButton {
            compact: true
            text: "Cancel"
            onClicked: carouselPreflightIgnoreDialog.close()
        }
        ActionButton {
            compact: true
            tone: "warning"
            text: "Ignore with Reason"
            enabled: root.preflightIgnoreReason.trim().length > 0
            disabledReason: "A reason is required for an ignored delivery warning."
            onClicked: {
                if (carouselDocument.ignorePreflightIssue(
                            String(root.pendingPreflightIgnoreIssue.issueId || ""),
                            root.preflightIgnoreReason))
                    carouselPreflightIgnoreDialog.close()
            }
        }
    }
}
