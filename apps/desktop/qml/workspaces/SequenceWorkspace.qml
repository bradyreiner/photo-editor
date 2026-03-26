import QtQuick
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    color: "transparent"

    property string mode: "carousel"
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
                        text: "Carousel"
                        active: root.mode === "carousel"
                        onClicked: root.modeSelected("carousel")
                    }

                    ActionButton {
                        text: "Timeline"
                        active: root.mode === "timeline"
                        onClicked: root.modeSelected("timeline")
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: root.mode === "timeline" ? timelineMode : carouselMode
        }
    }

    Component {
        id: carouselMode

        CarouselView {}
    }

    Component {
        id: timelineMode

        TimelineView {}
    }
}
