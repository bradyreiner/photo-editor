import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: row

    property string label: ""
    property string value: ""
    property bool active: false
    property string iconName: ""
    signal clicked()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: theme.sidebarRowHeightCompact
    radius: theme.radiusControl
    color: active ? theme.editorSelectionFill
                  : (mouse.containsMouse ? theme.editorControlHover : "transparent")
    border.width: row.activeFocus ? theme.focusBorderWidth : 0
    border.color: theme.focusRing
    activeFocusOnTab: true
    Accessible.name: row.label
    Accessible.description: row.value
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: row.clicked()
    Keys.onEnterPressed: row.clicked()
    Keys.onSpacePressed: row.clicked()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        spacing: theme.gapControl

        AppIcon {
            visible: row.iconName.length > 0
            name: row.iconName
            color: row.active ? theme.interaction300 : theme.textMuted
            Layout.preferredWidth: theme.scaled(13)
            Layout.preferredHeight: theme.scaled(13)
        }

        Label {
            Layout.fillWidth: true
            text: row.label
            color: row.active ? theme.interaction300
                              : (mouse.containsMouse ? theme.textMain : theme.textMuted)
            font.family: theme.fontUi
            font.pixelSize: theme.typeControl
            font.weight: row.active ? theme.weightSection : theme.weightControl
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
        }

        Label {
            visible: row.value.length > 0
            text: row.value
            color: row.active ? theme.textMain : theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: theme.typeCaption
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            row.forceActiveFocus()
            row.clicked()
        }
    }
}
