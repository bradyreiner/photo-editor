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

    radius: theme.radius10
    color: theme.alpha(theme.glassBase, 0.68)
    border.width: 1
    border.color: theme.alpha(root.accent, 0.38)

    implicitWidth: contentRow.implicitWidth + 16
    implicitHeight: 30

    RowLayout {
        id: contentRow
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        Rectangle {
            implicitWidth: 6
            implicitHeight: 6
            radius: width / 2
            color: root.accent
            opacity: 0.9
        }

        Label {
            Layout.fillWidth: true
            text: root.summaryText
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: 10
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
            verticalAlignment: Text.AlignVCenter
        }

        ProgressStrip {
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
            compact: true
            embedded: true
            accent: root.accent
            tooltipText: text
            onClicked: root.retryable ? root.retryRequested() : root.cancelRequested()
        }
    }
}
