import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: row
    required property string label
    required property string value
    property real labelWidth: theme.scaled(74)
    Layout.fillWidth: true
    spacing: theme.gapControl

    Theme { id: theme }

    Label {
        Layout.preferredWidth: row.labelWidth
        Layout.alignment: Qt.AlignTop
        topPadding: theme.space2
        text: row.label
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
    }
    TextEdit {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.minimumHeight: theme.controlHeightDense
        topPadding: theme.space2
        text: row.value
        color: theme.textMain
        font.family: theme.fontUi
        font.pixelSize: theme.typeControl
        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
        readOnly: true
        selectByMouse: true
        selectionColor: theme.interaction500
        selectedTextColor: theme.textStrong
        Accessible.name: row.label + ": " + row.value
    }
}
