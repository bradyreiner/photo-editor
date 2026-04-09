import QtQuick
import QtQuick.Controls
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
            spacing: theme.space10

            ColumnLayout {
                spacing: 2

                Label {
                    text: "Sequence"
                    color: theme.textStrong
                    font.family: theme.fontDisplay
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }

                Label {
                    text: root.mode === "timeline" ? "Edit timing, tracks, and playback" : "Arrange boards, text, and media layouts"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 11
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                radius: theme.radiusPanel
                color: theme.alpha(theme.ink950, 0.5)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.18)
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
