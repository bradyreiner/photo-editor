import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: panel

    default property alias content: panelContent.data
    property string title: ""
    property string subhead: ""
    property bool headerVisible: title.length > 0
    property bool framed: true

    Theme { id: theme }

    color: theme.ink900
    border.width: framed ? theme.hairlineWidth : 0
    border.color: theme.alpha(theme.controlEdge, 0.7)
    radius: theme.radius6
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.space12
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            visible: panel.headerVisible
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: panel.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeSection
                font.weight: Font.DemiBold
                font.letterSpacing: 0
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Rectangle {
                visible: panel.subhead.length > 0
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                color: theme.accentWarm400
                radius: 4
            }
        }

        Item {
            id: panelContent
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
