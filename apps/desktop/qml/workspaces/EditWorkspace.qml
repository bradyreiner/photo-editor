import QtQuick
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    color: "transparent"

    property string mode: "adjust"
    signal modeSelected(string mode)

    Theme { id: theme }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Item { Layout.fillWidth: true }

            Rectangle {
                radius: theme.radius8
                color: theme.alpha(theme.ink950, 0.58)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.34)
                implicitHeight: 30
                implicitWidth: modeRow.implicitWidth + 6

                RowLayout {
                    id: modeRow
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 3

                    ActionButton {
                        text: "Adjust"
                        active: root.mode === "adjust"
                        onClicked: root.modeSelected("adjust")
                    }

                    ActionButton {
                        text: "Mask"
                        active: root.mode === "mask"
                        accent: theme.accentCool500
                        onClicked: root.modeSelected("mask")
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: root.mode === "mask" ? maskMode : adjustMode
        }
    }

    Component {
        id: adjustMode

        EditView {}
    }

    Component {
        id: maskMode

        MaskView {}
    }
}
