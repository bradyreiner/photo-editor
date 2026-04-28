import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GlassSurface {
    id: root

    required property string label
    required property string value
    property string detail: ""
    property color accent: theme.accentWarm400

    Theme { id: theme }

    implicitWidth: 168
    implicitHeight: metricColumn.implicitHeight + (theme.panelPaddingCompact * 2)
    radius: theme.radiusSurface

    ColumnLayout {
        id: metricColumn
        anchors.fill: parent
        anchors.margins: theme.panelPaddingCompact
        spacing: theme.gapDense

        Rectangle {
            Layout.preferredWidth: 14
            Layout.preferredHeight: 2
            radius: 0
            color: root.accent
        }

        Label {
            text: root.label
            color: theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: 10
            font.letterSpacing: 0.8
        }

        Label {
            text: root.value
            color: theme.textStrong
            font.family: theme.fontDisplay
            font.pixelSize: 18
            font.weight: Font.Medium
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            visible: root.detail.length > 0
            text: root.detail
            color: theme.textMuted
            wrapMode: Text.WordWrap
            font.family: theme.fontUi
            font.pixelSize: 11
            maximumLineCount: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
