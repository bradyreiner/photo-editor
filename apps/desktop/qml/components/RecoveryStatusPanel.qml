pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    property bool catalogFailure: false
    property bool jobFailure: false
    property bool repairPending: false
    property bool expanded: false
    property string summary: ""
    property var details: []
    signal retryRequested()
    signal backupRequested()
    signal repairRequested()
    signal confirmRepairRequested()
    signal cancelRepairRequested()
    signal reviewRequested()
    signal dismissRequested()

    Theme { id: theme }
    readonly property string recoveryStatusContract: "recovery-status:v1|placement:under-navigation|surface:flat|primary:retry|maintenance:disclosed|repair:confirmed"
    implicitHeight: content.implicitHeight + theme.space8 * 2
    color: theme.ink900
    Accessible.role: Accessible.StatusBar
    Accessible.name: title.text
    Accessible.description: summary

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: theme.hairlineWidth
        color: theme.lineSoft
    }
    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: theme.space12
        anchors.topMargin: theme.space8
        spacing: theme.space10

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8
            AppIcon {
                name: root.catalogFailure || root.jobFailure ? "warning" : "check"
                color: theme.textMuted
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                Accessible.ignored: true
            }
            Label {
                id: title
                objectName: "recoverySummaryTitle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: root.width > 760 ? implicitWidth : -1
                text: root.catalogFailure ? "Library unavailable" : root.jobFailure ? "Background jobs paused" : "Work recovered"
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: root.width > 760
                text: root.summary
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                elide: Text.ElideRight
            }
            ActionButton {
                objectName: "catalogRetryButton"
                visible: root.catalogFailure && !root.repairPending
                text: "Retry"
                compact: true
                tone: "secondary"
                Accessible.name: "Retry opening the catalog"
                onClicked: root.retryRequested()
            }
            ActionButton {
                objectName: "recoveryReviewWorkButton"
                visible: !root.catalogFailure && !root.repairPending
                text: root.jobFailure ? "Review jobs" : "Review work"
                compact: true
                tone: "secondary"
                onClicked: root.reviewRequested()
            }
            ActionButton {
                objectName: "recoveryDetailsButton"
                text: root.expanded ? "Hide details" : "Details"
                compact: true
                tone: "tertiary"
                Accessible.name: text
                Accessible.description: "Show recovery details and catalog maintenance actions"
                onClicked: root.expanded = !root.expanded
            }
            ActionButton {
                objectName: "recoveryDoneButton"
                visible: !root.catalogFailure && !root.jobFailure && !root.repairPending
                iconName: "close"
                showText: false
                compact: true
                tone: "tertiary"
                Accessible.name: "Dismiss recovery summary"
                onClicked: root.dismissRequested()
            }
        }
        ColumnLayout {
            objectName: "recoveryDetailsContent"
            Layout.fillWidth: true
            visible: root.expanded || root.repairPending
            spacing: theme.space8
            Label {
                Layout.fillWidth: true
                text: root.summary
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
            Repeater {
                model: root.details
                delegate: Label {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    textFormat: Text.PlainText
                }
            }
            Flow {
                objectName: "catalogRecoveryActions"
                Layout.fillWidth: true
                visible: root.catalogFailure || root.repairPending
                spacing: theme.space8
                ActionButton {
                    objectName: "catalogBackupButton"
                    visible: root.catalogFailure && !root.repairPending
                    text: "Create backup"
                    compact: true
                    tone: "tertiary"
                    Accessible.name: "Create a catalog backup"
                    onClicked: root.backupRequested()
                }
                ActionButton {
                    objectName: "catalogRepairButton"
                    visible: root.catalogFailure && !root.repairPending
                    text: "Repair from backup"
                    compact: true
                    tone: "tertiary"
                    Accessible.name: "Review catalog repair"
                    onClicked: root.repairRequested()
                }
                ActionButton {
                    objectName: "catalogRepairConfirmButton"
                    visible: root.repairPending
                    text: "Confirm repair"
                    compact: true
                    tone: "primary"
                    Accessible.name: "Confirm catalog repair from the latest backup"
                    onClicked: root.confirmRepairRequested()
                }
                ActionButton {
                    objectName: "catalogRepairCancelButton"
                    visible: root.repairPending
                    text: "Cancel"
                    compact: true
                    tone: "tertiary"
                    Accessible.name: "Cancel catalog repair"
                    onClicked: root.cancelRepairRequested()
                }
            }
        }
    }
}
