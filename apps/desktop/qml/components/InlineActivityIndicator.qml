import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property string title
    property string detail: ""
    property real progressValue: -1
    property color accent: theme.accentWarm400
    property bool cancelable: false
    property bool retryable: false
    signal cancelRequested()
    signal retryRequested()

    readonly property string summaryText: {
        const titleText = title ? String(title) : ""
        const detailText = detail ? String(detail) : ""
        if (titleText.length === 0)
            return detailText
        if (detailText.length === 0 || detailText === titleText)
            return titleText
        return titleText + " · " + detailText
    }

    Theme { id: theme }

    radius: theme.radiusControl
    color: theme.ink900
    border.width: theme.hairlineWidth
    border.color: theme.alpha(theme.lineSoft, 0.72)

    implicitWidth: contentRow.implicitWidth + 16
    implicitHeight: Math.max(theme.controlHeightDense, contentRow.implicitHeight + theme.space8)

    RowLayout {
        id: contentRow
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        spacing: theme.gapDense

        OpticalActivityIndicator {
            visible: !root.retryable && root.progressValue < 1
            active: visible
            color: root.accent
            progress: root.progressValue
        }

        AppIcon {
            visible: root.retryable || root.progressValue >= 1
            name: root.retryable ? "error" : "check"
            color: root.retryable ? theme.error : theme.success
            Layout.preferredWidth: theme.scaled(16)
            Layout.preferredHeight: theme.scaled(16)
        }

        Label {
            Layout.fillWidth: true
            text: root.summaryText
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeControl
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
            verticalAlignment: Text.AlignVCenter
        }

        ProgressStrip {
            visible: !root.retryable
            Layout.preferredWidth: 54
            Layout.preferredHeight: 3
            progress: root.progressValue
            accent: root.accent
        }

        ActionButton {
            id: actionButton
            objectName: "activityCancelButton"
            visible: root.cancelable || root.retryable
            text: root.retryable ? "Retry" : "Cancel"
            iconName: root.retryable ? "retry" : "close"
            compact: true
            embedded: true
            accent: root.accent
            tooltipText: text
            onClicked: root.retryable ? root.retryRequested() : root.cancelRequested()
        }
    }
}
