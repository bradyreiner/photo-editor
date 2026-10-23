import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string assetLabel: "Asset"
    property string detail: ""
    property string suggestedAction: ""
    property string detailsText: ""
    property string statusText: "pending"
    property string thumbnailSource: ""
    property color statusColor: theme.textMuted
    property bool detailsExpanded: false
    property bool actionsVisible: false
    property bool actionEnabled: false

    signal fixRequested()
    signal acceptRequested()
    signal rollbackRequested()
    signal detailsToggled()

    Theme { id: theme }

    readonly property string agentBatchStatusRowContract: "agent-batch-status-row:v1|thumbnail:true|status-badge:true|actions:true|details-toggle:true|label-wrap:true|detail-wrap:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|asset-wrap:true|detail-wrap:true|suggested-wrap:true|details-wrap:true|badge-fit:true|tooltip-full:true|accessibility-full:true"

    Layout.fillWidth: true
    implicitHeight: Math.max(56, rowContent.implicitHeight + 10)
    radius: theme.radiusControl
    color: rowHover.hovered || activeFocus
           ? theme.alpha(theme.ink800, 0.36)
           : theme.alpha(theme.ink900, 0.32)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? theme.focusRing : theme.alpha(root.statusColor, 0.38)
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: root.assetLabel
    Accessible.description: root.detail

    HoverHandler {
        id: rowHover
    }

    RowLayout {
        id: rowContent
        anchors.fill: parent
        anchors.margins: 5
        spacing: 8

        Rectangle {
            objectName: "agentReviewIssueThumbnail"
            Layout.preferredWidth: 46
            Layout.preferredHeight: 46
            Layout.alignment: Qt.AlignTop
            radius: theme.radiusControl
            color: theme.alpha(theme.ink950, 0.42)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.26)
            clip: true

            Image {
                objectName: "agentReviewIssueThumbnailImage"
                anchors.fill: parent
                source: root.thumbnailSource
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false
                visible: status === Image.Ready
            }

            AppIcon {
                anchors.centerIn: parent
                name: "image"
                color: theme.textFaint
                implicitWidth: 15
                implicitHeight: 15
                visible: root.thumbnailSource.length === 0
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: root.assetLabel
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 10
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Text {
                Layout.fillWidth: true
                text: root.detail
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 9
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Text {
                objectName: "agentReviewIssueSuggestedActionLabel"
                Layout.fillWidth: true
                visible: root.suggestedAction.length > 0
                text: root.suggestedAction
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: 9
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Text {
                objectName: "agentReviewIssueDetailsLabel"
                Layout.fillWidth: true
                visible: root.detailsExpanded && root.detailsText.length > 0
                text: root.detailsText
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: 8
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 3
                elide: Text.ElideNone
            }
        }

        ColumnLayout {
            Layout.preferredWidth: Math.min(220, Math.max(168, root.width * 0.34))
            spacing: 4

            Rectangle {
                Layout.alignment: Qt.AlignRight
                radius: theme.radiusControl
                color: theme.alpha(root.statusColor, 0.14)
                border.width: 1
                border.color: theme.alpha(root.statusColor, 0.44)
                implicitWidth: Math.max(64, statusLabel.implicitWidth + 14)
                implicitHeight: 24

                Text {
                    id: statusLabel
                    anchors.centerIn: parent
                    text: root.statusText
                    color: root.statusColor
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    horizontalAlignment: Text.AlignHCenter
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
                }
            }

            RowLayout {
                objectName: "agentReviewIssueActions"
                Layout.fillWidth: true
                visible: root.actionsVisible
                spacing: 4

                ActionButton {
                    objectName: "agentFixReviewIssueButton"
                    Layout.fillWidth: true
                    text: "Fix"
                    iconName: "adjust"
                    compact: true
                    enabled: root.actionEnabled
                    onClicked: root.fixRequested()
                }

                ActionButton {
                    objectName: "agentAcceptReviewIssueButton"
                    Layout.fillWidth: true
                    text: "Accept"
                    iconName: "check"
                    compact: true
                    enabled: root.actionEnabled
                    onClicked: root.acceptRequested()
                }

                ConfirmActionButton {
                    objectName: "agentRollbackReviewIssueButton"
                    Layout.fillWidth: true
                    text: "Rollback"
                    confirmText: "Confirm"
                    compact: true
                    enabled: root.actionEnabled
                    helperText: "Restore this photo's pre-agent recipe"
                    onConfirmed: root.rollbackRequested()
                }
            }

            ActionButton {
                objectName: "agentReviewIssueDetailsButton"
                Layout.fillWidth: true
                text: root.detailsExpanded ? "Hide Details" : "Details"
                iconName: "menu"
                compact: true
                enabled: root.detailsText.length > 0
                onClicked: root.detailsToggled()
            }
        }
    }

    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && (rowHover.hovered)
        delay: 350
        timeout: 3600
        text: root.detail.length > 0 ? root.assetLabel + "\n" + root.detail : root.assetLabel
    }
}
