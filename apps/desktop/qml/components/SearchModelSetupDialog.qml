import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

AppPopup {
    id: root
    objectName: "searchModelSetupDialog"
    property var controller: null
    modal: true
    focus: true
    anchors.centerIn: parent
    width: Math.min(popupAvailableWidth, theme.scaled(480))
    height: Math.min(popupAvailableHeight, contentItem.implicitHeight + padding * 2)
    padding: standardPopupPadding
    closePolicy: Popup.CloseOnEscape
    Theme { id: theme }

    FolderDialog {
        id: importFolder
        title: "Choose the SigLIP 2 model folder"
        onAccepted: if (root.controller) root.controller.importFolder(selectedFolder)
    }

    contentItem: ColumnLayout {
        spacing: theme.space12
        Label {
            text: "Local photo search"
            color: theme.textStrong
            font.pixelSize: theme.typeSection
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            text: "SigLIP 2 matches your description to photo content. The 1.54 GB model runs on this Mac."
            color: theme.textMuted
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "searchModelSetupStatus"
            Layout.fillWidth: true
            text: root.controller ? root.controller.status : "The search runtime is unavailable in this build."
            textFormat: Text.PlainText
            color: theme.textStrong
            wrapMode: Text.Wrap
        }
        ProgressBar {
            objectName: "searchModelSetupProgress"
            Layout.fillWidth: true
            visible: root.controller && root.controller.running
            value: root.controller ? root.controller.progress : 0
            indeterminate: visible && value <= 0
        }
        Label {
            Layout.fillWidth: true
            text: root.controller ? root.controller.location : ""
            color: theme.textMuted
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WrapAnywhere
        }
        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: theme.space8
            ActionButton {
                objectName: "searchModelDownloadButton"
                text: "Download model · 1.54 GB"
                showText: true
                enabled: root.controller && root.controller.available && !root.controller.running
                onClicked: root.controller.download(true)
            }
            ActionButton {
                objectName: "searchModelImportButton"
                text: "Use local copy…"
                showText: true
                enabled: root.controller && root.controller.available && !root.controller.running
                onClicked: importFolder.open()
            }
            ActionButton {
                objectName: "searchModelVerifyButton"
                text: "Check files"
                showText: true
                enabled: root.controller && root.controller.available && !root.controller.running
                onClicked: root.controller.verify()
            }
            ActionButton {
                objectName: "searchModelCancelButton"
                text: "Pause setup"
                showText: true
                visible: root.controller && root.controller.running
                onClicked: root.controller.cancel()
            }
            ActionButton {
                objectName: "searchModelCloseButton"
                text: "Close"
                showText: true
                onClicked: root.close()
            }
        }
    }
}
