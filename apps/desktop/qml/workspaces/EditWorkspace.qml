import QtQuick

Rectangle {
    id: root

    color: "transparent"
    property string mode: "adjust"
    signal modeSelected(string mode)

    EditView {
        anchors.fill: parent
    }
}
