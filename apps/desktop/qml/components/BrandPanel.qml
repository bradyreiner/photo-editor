import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: "#c99a52"
    property bool headerVisible: true
    property int bodyMargin: root.headerVisible ? theme.panelPadding : theme.panelPaddingCompact
    property int bodySpacing: root.headerVisible ? theme.gapPanel : theme.gapControl
    default property alias contentData: body.data

    Theme { id: theme }

    radius: theme.radiusPanel
    color: theme.alpha(theme.ink850, 0.94)
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.14)

    gradient: Gradient {
        GradientStop { position: 0.0; color: theme.alpha(theme.ink700, 0.025) }
        GradientStop { position: 0.12; color: theme.alpha(theme.ink850, 0.98) }
        GradientStop { position: 1.0; color: theme.ink900 }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(1, parent.radius - 1)
        color: "transparent"
        border.width: 1
        border.color: theme.alpha(theme.ink950, 0.08)
    }

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
