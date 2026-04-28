import QtQuick
import QtQuick.Controls
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

    function activeModeItem() {
        return modeLoader.item ? modeLoader.item : null
    }

    function canSelectAll() {
        const item = activeModeItem()
        return item && item.canSelectAll ? item.canSelectAll() : false
    }

    function selectAll() {
        const item = activeModeItem()
        if (item && item.selectAll)
            item.selectAll()
    }

    function canDeselect() {
        const item = activeModeItem()
        return item && item.canDeselect ? item.canDeselect() : false
    }

    function deselectAll() {
        const item = activeModeItem()
        if (item && item.deselectAll)
            item.deselectAll()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

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
                        text: "Gallery"
                        iconName: "grid"
                        showText: true
                        compact: true
                        tooltipText: "Gallery"
                        active: root.mode === "catalog"
                        onClicked: root.modeSelected("catalog")
                    }

                    ActionButton {
                        text: "Import"
                        iconName: "source"
                        showText: true
                        compact: true
                        tooltipText: "Import"
                        active: root.mode === "import"
                        onClicked: root.modeSelected("import")
                    }
                }
            }
        }

        Loader {
            id: modeLoader
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
