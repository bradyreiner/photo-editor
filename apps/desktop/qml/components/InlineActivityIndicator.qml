import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string title
    property string detail: ""
    property real progressValue: -1
    property color accent: theme.accentWarm400
    property bool cancelable: false
    signal cancelRequested()

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
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        ProgressStrip {
            Layout.preferredWidth: 54
            Layout.preferredHeight: 3
            progress: root.progressValue
            accent: root.accent
        }

        Button {
            id: cancelButton
            objectName: "activityCancelButton"
            visible: root.cancelable
            text: "Cancel"
            flat: true
            padding: 4
            leftPadding: 6
            rightPadding: 6
            font.family: theme.fontUi
            font.pixelSize: 10
            onClicked: root.cancelRequested()
        }
    }
}
