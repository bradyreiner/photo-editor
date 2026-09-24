import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: emptyState

    required property var studioTheme
    required property color hairline
    required property color accentColor
    required property color dimText
    property string iconName: "library"
    property string title: ""
    property string message: ""
    property bool error: false

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumHeight: 220
    color: studioTheme.alpha(studioTheme.ink950, 0.62)
    border.width: studioTheme.chromeBorderWidth
    border.color: error ? studioTheme.alpha(studioTheme.error, 0.72) : hairline
    radius: 4

    Column {
        anchors.centerIn: parent
        width: Math.min(420, Math.max(220, parent.width - 48))
        spacing: 12

        AppIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: emptyState.iconName
            width: 30
            height: 30
            color: emptyState.error ? emptyState.studioTheme.error : emptyState.accentColor
        }

        Label {
            width: parent.width
            text: emptyState.title
            color: emptyState.studioTheme.textStrong
            horizontalAlignment: Text.AlignHCenter
            font.family: emptyState.studioTheme.fontUi
            font.pixelSize: 14
            font.weight: Font.DemiBold
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }

        Label {
            width: parent.width
            text: emptyState.message
            color: emptyState.error ? emptyState.studioTheme.error : emptyState.dimText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.family: emptyState.studioTheme.fontUi
            font.pixelSize: 11
        }
    }
}
