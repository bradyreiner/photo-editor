import QtQuick
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    color: "transparent"

    property string mode: "catalog"
    signal modeSelected(string mode)
    signal assetActivated(string destination)
    signal projectActivated(string destination, string projectName)
    signal localAssetActivated(string filePath, string previewSource, string displayName)

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
                        text: "Catalog"
                        active: root.mode === "catalog"
                        onClicked: root.modeSelected("catalog")
                    }

                    ActionButton {
                        text: "Import"
                        active: root.mode === "import"
                        onClicked: root.modeSelected("import")
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: root.mode === "import" ? importMode : catalogMode
        }
    }

    Component {
        id: catalogMode

        LibraryView {
            onAssetActivated: function(destination) {
                root.assetActivated(destination)
            }
            onProjectActivated: function(destination, projectName) {
                root.projectActivated(destination, projectName)
            }
            onLocalAssetActivated: function(filePath, previewSource, displayName) {
                root.localAssetActivated(filePath, previewSource, displayName)
            }
        }
    }

    Component {
        id: importMode

        ImportView {}
    }
}
