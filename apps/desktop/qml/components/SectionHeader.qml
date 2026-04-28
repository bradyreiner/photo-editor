import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: theme.accentWarm400
    property bool showAccentMarker: true
    property string titleFontFamily: theme.fontDisplay
    property int titlePixelSize: 15
    property int titleWeight: Font.DemiBold

    implicitHeight: columnLayout.implicitHeight

    Theme { id: theme }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: theme.space4

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.gapDense

            Rectangle {
                visible: root.showAccentMarker
                Layout.preferredWidth: root.showAccentMarker ? 16 : 0
                Layout.preferredHeight: 2
                radius: 1
                color: root.accent
            }

            Label {
                text: root.title
                color: theme.textStrong
                font.family: root.titleFontFamily
                font.pixelSize: root.titlePixelSize
                font.weight: root.titleWeight
                font.letterSpacing: 0.2
            }

            Item { Layout.fillWidth: true }
        }

        Label {
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
