import QtQuick
import QtQuick.Layouts

GlassSurface {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: theme.accentWarm400
    property bool headerVisible: true
    property int bodyMargin: root.headerVisible ? theme.panelPadding : theme.panelPaddingCompact
    property int bodySpacing: root.headerVisible ? theme.gapPanel : theme.gapControl
    default property alias contentData: body.data

    Theme { id: theme }

    radius: theme.radiusPanel
    accentRail: false

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: root.bodyMargin
        spacing: root.bodySpacing

        ColumnLayout {
            visible: root.headerVisible
            spacing: theme.space4 + 1

            SectionHeader {
                Layout.fillWidth: true
                title: root.title
                subtitle: root.subtitle
                accent: root.accent
            }
        }
    }
}
