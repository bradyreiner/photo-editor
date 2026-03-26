import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: "#c99a52"
    property bool headerVisible: true
    property int bodyMargin: root.headerVisible ? 18 : 12
    property int bodySpacing: root.headerVisible ? 12 : 10
    default property alias contentData: body.data

    Theme { id: theme }

    radius: theme.radius12
    color: theme.ink850
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.4)

    gradient: Gradient {
        GradientStop { position: 0.0; color: theme.alpha(theme.ink700, 0.06) }
        GradientStop { position: 0.12; color: theme.ink850 }
        GradientStop { position: 1.0; color: theme.ink900 }
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: root.bodyMargin
        spacing: root.bodySpacing

        ColumnLayout {
            visible: root.headerVisible
            spacing: 5

            SectionHeader {
                Layout.fillWidth: true
                title: root.title
                subtitle: root.subtitle
                accent: root.accent
            }
        }
    }
}
