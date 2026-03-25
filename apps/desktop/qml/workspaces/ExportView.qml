import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    color: "transparent"

    RowLayout {
        anchors.fill: parent
        spacing: 14

        BrandPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Export Queue"
            subtitle: "Operational delivery surface."

            Repeater {
                model: [
                    { name: "Client delivery set", preset: "Full Resolution", status: "Queued" },
                    { name: "Proof gallery", preset: "Web Proof", status: "Draft" },
                    { name: "Social selects", preset: "Quick Web", status: "Waiting" }
                ]

                delegate: Rectangle {
                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: 68
                    radius: 10
                    color: "#1a1d21"
                    border.width: 1
                    border.color: "#2d3339"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                text: modelData.name
                                color: "#f2f4f6"
                                font.family: "Avenir Next"
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }

                            Label {
                                text: modelData.preset
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                            }
                        }

                        Rectangle {
                            radius: 8
                            color: "#171a1e"
                            border.width: 1
                            border.color: "#2d3339"
                            implicitWidth: 72
                            implicitHeight: 26

                            Label {
                                anchors.centerIn: parent
                                text: modelData.status
                                color: "#d9dee3"
                                font.family: "Menlo"
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        BrandPanel {
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            title: "Job Setup"
            subtitle: "Preset and destination staging."

            InspectorSection {
                Layout.fillWidth: true
                title: "Preset"

                ToneComboBox {
                    Layout.fillWidth: true
                    model: ["Full Resolution", "Web Proof", "Client Preview", "Quick Web"]
                }

                ToneTextField {
                    Layout.fillWidth: true
                    placeholderText: "/Deliveries/ClientName"
                }
            }

            InspectorSection {
                Layout.fillWidth: true
                title: "Selection Context"

                Label {
                    text: catalogAssets.selectedAssetId.length > 0 ? catalogAssets.selectedCameraLabel : "No current selection"
                    color: "#f2f4f6"
                    font.family: "Avenir Next"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Label {
                    text: catalogAssets.selectedCaptureDate.length > 0 ? catalogAssets.selectedCaptureDate : "Select an asset in Library to anchor the export."
                    color: "#8f98a2"
                    font.family: "Avenir Next"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Item { Layout.fillHeight: true }

            ActionButton {
                Layout.fillWidth: true
                text: "Queue Export"
                tone: "primary"
                enabled: false
            }
        }
    }
}
