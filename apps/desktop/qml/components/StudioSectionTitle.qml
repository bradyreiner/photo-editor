import QtQuick
import QtQuick.Controls.Basic

Label {
    property string label: ""

    Theme { id: theme }

    text: label
    color: theme.textStrong
    font.family: theme.fontUi
    font.pixelSize: theme.typeLabel
    font.weight: theme.weightSection
    font.letterSpacing: 0
}
