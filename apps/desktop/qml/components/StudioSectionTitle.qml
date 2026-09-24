import QtQuick
import QtQuick.Controls.Basic

Label {
    property string label: ""

    Theme { id: theme }

    text: label
    color: theme.textStrong
    font.family: theme.fontUi
    font.pixelSize: 11
    font.weight: Font.DemiBold
    font.letterSpacing: 0
}
