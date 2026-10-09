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
    radius: theme.radiusPanel
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.panelPadding
        spacing: theme.gapPanel

        RowLayout {
            Layout.fillWidth: true
            visible: panel.headerVisible
            spacing: theme.gapControl

            Label {
                Layout.fillWidth: true
                text: panel.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeSection
                font.weight: theme.weightSection
                font.letterSpacing: 0
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Label {
                id: subheadLabel
                visible: panel.subhead.length > 0
                Layout.maximumWidth: Math.max(0, panel.width * 0.48)
                Layout.alignment: Qt.AlignVCenter
                text: panel.subhead
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: theme.weightControl
                horizontalAlignment: Text.AlignRight
                maximumLineCount: 1
                elide: Text.ElideRight
                Accessible.name: panel.subhead

                MouseArea {
                    id: subheadHover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }

                AppToolTip {
                    targetItem: subheadLabel
                    visible: subheadHover.containsMouse && subheadLabel.truncated
                    text: panel.subhead
                }
            }
        }

        Item {
            id: panelContent
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
