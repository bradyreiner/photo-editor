pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppDialog {
    id: root
    objectName: "recipeRecoveryReviewDialog"
    property string summary: ""
    property var details: []
    property string recoveryDetail: ""
    property var recoveryEntries: []
    property var archiveRecoveryHandler: null
    property string workspaceActionText: ""
    signal openWorkRequested()

    Theme { id: theme }

    function openReview() {
        errorText = ""
        archiveUrl = ""
        open()
    }
    parent: Overlay.overlay
    title: "Review saved edits"
    width: Math.min(popupAvailableWidth, theme.scaled(520))
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    standardButtons: Dialog.Close
    property string errorText: ""
    property string archiveUrl: ""
    onOpened: contentItem.forceActiveFocus(Qt.PopupFocusReason)

    contentItem: ColumnLayout {
        spacing: theme.space12
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(theme.scaled(360), reviewBody.implicitHeight)
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: reviewBody
                width: parent.width
                spacing: theme.space16
                Label {
                    Layout.fillWidth: true
                    visible: root.summary.length > 0
                    text: root.summary
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    color: theme.textMain
                }
                Repeater {
                    model: root.details
                    delegate: Label {
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        color: theme.textMuted
                    }
                }
                Repeater {
                    model: root.recoveryEntries
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: theme.space8
                        Label {
                            Layout.fillWidth: true
                            text: modelData.fileName
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            color: theme.textStrong
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.detail
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            color: theme.textMain
                        }
                        ActionButton {
                            objectName: "archiveRecipeRecoveryButton"
                            text: "Keep saved version"
                            visible: modelData.conflict && root.archiveRecoveryHandler !== null
                            enabled: modelData.canArchive
                            disabledReason: "Wait for this photo's pending save to finish."
                            onClicked: {
                                const assetId = String(modelData.assetId)
                                // Resolving changes this repeater's model. Leave its
                                // delegate's handler before changing controller state.
                                Qt.callLater(function() {
                                    const result = root.archiveRecoveryHandler(assetId)
                                    root.errorText = result.ok ? "" : String(result.error || "Could not archive this edit copy.")
                                    if (result.ok) root.archiveUrl = String(result.archiveUrl || "")
                                })
                            }
                        }
                    }
                }
            }
        }
        ActionButton {
            objectName: "openRecoveredWorkspaceButton"
            visible: root.workspaceActionText.length > 0
            text: root.workspaceActionText
            onClicked: {
                root.close()
                root.openWorkRequested()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.recoveryDetail.length > 0 && root.recoveryEntries.length === 0 && root.archiveUrl.length === 0
            text: root.recoveryDetail
            textFormat: Text.PlainText
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            color: theme.textMain
        }
        Label {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            textFormat: Text.PlainText
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            color: theme.error
        }
        Label {
            Layout.fillWidth: true
            visible: root.archiveUrl.length > 0
            text: "The complete older edit copy was archived locally."
            wrapMode: Text.Wrap
            color: theme.textMain
        }
        ActionButton {
            visible: root.archiveUrl.length > 0
            text: "Show backup"
            onClicked: Qt.openUrlExternally(root.archiveUrl)
        }
    }
}
