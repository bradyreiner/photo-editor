import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property string title: ""
    property string detail: ""
    property real progressValue: -1
    property bool cancelable: false
    property bool retryable: false
    property bool failed: false
    property bool dismissible: false
    property string actionText: ""
    property string statusIcon: ""
    property color accent: theme.accentWarm400

    signal cancelRequested()
    signal retryRequested()
    signal actionRequested()
    signal dismissRequested()

    readonly property string summaryText: title.length === 0 ? detail
                                            : detail.length === 0 || detail === title ? title
                                            : title + " · " + detail
    readonly property string activityStatusContract: "activity-status-bar:v2|placement:under-navigation|surface:integrated|states:loading-error-recovery|progress:additive-rgb|actions:cancel-retry-review-dismiss|no-modal:true"

    Theme { id: theme }

    implicitHeight: visible ? 34 : 0
    Layout.preferredHeight: implicitHeight
    Layout.fillWidth: true
    Accessible.name: title
    Accessible.description: detail
    Accessible.role: Accessible.StatusBar

    Rectangle {
        anchors.fill: parent
        color: root.failed ? theme.alpha(theme.error, 0.055) : theme.alpha(theme.surfaceTop, 0.38)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 14
        spacing: 9

        AppIcon {
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
            name: root.statusIcon.length > 0 ? root.statusIcon : root.failed ? "info" : "clock"
            color: root.failed ? theme.error : root.statusIcon === "check" ? theme.success : theme.textMuted
        }

        Text {
            Layout.preferredWidth: Math.min(190, implicitWidth)
            text: root.title
            color: root.failed ? theme.error : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 11
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            maximumLineCount: 1
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 12
            color: theme.alpha(theme.lineSoft, 0.62)
            visible: root.detail.length > 0 && root.title.length > 0
        }

        Text {
            Layout.fillWidth: true
            text: root.detail
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
            elide: Text.ElideRight
            maximumLineCount: 1
            verticalAlignment: Text.AlignVCenter
        }

        ActionButton {
            objectName: "activityCancelButton"
            visible: root.cancelable || root.retryable || root.actionText.length > 0
            text: root.actionText.length > 0 ? root.actionText : root.retryable ? "Retry" : "Cancel"
            compact: true
            embedded: true
            accent: root.failed ? theme.error : root.accent
            tooltipText: text
            onClicked: {
                if (root.actionText.length > 0)
                    root.actionRequested()
                else if (root.retryable)
                    root.retryRequested()
                else
                    root.cancelRequested()
            }
        }

        ActionButton {
            objectName: "activityDismissButton"
            visible: root.dismissible
            iconName: "close"
            showText: false
            compact: true
            embedded: true
            tone: "tertiary"
            tooltipText: "Dismiss"
            Accessible.name: "Dismiss status"
            onClicked: root.dismissRequested()
        }
    }

    ProgressStrip {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        progress: root.progressValue
        failed: root.failed
        accent: root.failed ? theme.error : root.accent
        trackColor: "transparent"
    }
}
