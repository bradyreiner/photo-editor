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
    required property var carouselCopyProofDialog
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: "Copy proof"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 21
                font.weight: Font.Bold
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: "Reviews spelling, repeated words, and spacing. It never changes authored copy."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }

        StatusChip {
            text: carouselDocument.copyProofIssues.length > 0
                  ? carouselDocument.copyProofIssues.length + " open"
                  : "Clear"
            tone: carouselDocument.copyProofIssues.length > 0 ? "warning" : "success"
            compact: true
            detailText: "Review only; no automatic rewriting."
        }

        ActionButton {
            text: "Close"
            compact: true
            onClicked: carouselCopyProofDialog.close()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Label {
            text: "Language"
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
        }

        Repeater {
            model: carouselDocument.copyProofLanguageOptions

            delegate: FilterChip {
                required property string modelData
                text: modelData
                active: carouselDocument.copyProofLanguage === modelData
                onClicked: carouselDocument.setCopyProofLanguage(modelData)
            }
        }

        Item { Layout.fillWidth: true }

        ActionButton {
            objectName: "carouselNextCopyProofIssueButton"
            text: "Next issue"
            compact: true
            enabled: carouselDocument.copyProofIssues.length > 0
            disabledReason: "No copy proof issues remain."
            onClicked: carouselDocument.nextCopyProofIssue()
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: theme.radius8
        color: theme.alpha(theme.ink950, 0.52)
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.34)

        ListView {
            id: carouselCopyProofIssueList
            objectName: "carouselCopyProofIssueList"
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            spacing: 6
            model: carouselDocument.copyProofIssues
            reuseItems: true

            delegate: Rectangle {
                id: copyProofIssueRow
                required property var modelData
                width: ListView.view ? ListView.view.width : 600
                height: copyProofIssueContent.implicitHeight + 18
                radius: theme.radius6
                color: theme.alpha(theme.ink850, 0.8)
                border.width: 1
                border.color: theme.alpha(theme.warning, 0.36)

                ColumnLayout {
                    id: copyProofIssueContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 9
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7

                        StatusChip {
                            text: String(copyProofIssueRow.modelData.code || "copy")
                            tone: "warning"
                            compact: true
                            detailText: "Board " + (Number(copyProofIssueRow.modelData.boardIndex || 0) + 1) +
                                        " · " + String(copyProofIssueRow.modelData.layerLabel || "Text layer")
                        }

                        Label {
                            Layout.fillWidth: true
                            text: String(copyProofIssueRow.modelData.message || "Review this copy.")
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: (copyProofIssueRow.modelData.suggestions || []).length > 0
                        text: "Suggestion: " + (copyProofIssueRow.modelData.suggestions || []).join(", ")
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        ActionButton {
                            text: "Select layer"
                            compact: true
                            onClicked: carouselDocument.focusPreflightIssue(
                                           Number(copyProofIssueRow.modelData.boardIndex || 0),
                                           String(copyProofIssueRow.modelData.itemId || ""))
                        }

                        Item { Layout.fillWidth: true }

                        ActionButton {
                            visible: Boolean(copyProofIssueRow.modelData.canAddToDictionary)
                            text: "Add word"
                            compact: true
                            onClicked: carouselDocument.addCopyProofWord(
                                           String(copyProofIssueRow.modelData.word || ""))
                        }

                        ActionButton {
                            text: "Ignore"
                            compact: true
                            onClicked: carouselDocument.ignoreCopyProofIssue(
                                           String(copyProofIssueRow.modelData.issueId || ""))
                        }
                    }
                }

                TapHandler {
                    onTapped: carouselDocument.focusPreflightIssue(
                                  Number(copyProofIssueRow.modelData.boardIndex || 0),
                                  String(copyProofIssueRow.modelData.itemId || ""))
                }
            }

            Label {
                anchors.centerIn: parent
                width: Math.max(120, parent.width - 40)
                visible: carouselCopyProofIssueList.count === 0
                text: "No copy proof issues in visible text layers."
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }
    }

    Label {
        Layout.fillWidth: true
        text: "Ignored issues stay with this document. Added words persist in your Carousel dictionary."
        color: theme.textFaint
        font.family: theme.fontMono
        font.pixelSize: 9
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
    }
}
