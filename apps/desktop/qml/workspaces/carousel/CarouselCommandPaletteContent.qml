pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtQml
import "../../components"

ColumnLayout {
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselCommandPalette
    property alias searchField: carouselCommandSearchField
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10
    Accessible.name: "Carousel command palette"
    Accessible.description: "Search and run the same Carousel commands used by menus and keyboard shortcuts."
    Accessible.role: Accessible.Pane

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: "Commands"
            color: theme.textStrong
            font.family: theme.fontDisplay
            font.pixelSize: 22
            font.weight: Font.Bold
            elide: Text.ElideNone
        }

        Label {
            text: "Ctrl/Meta+K"
            color: theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: 10
            elide: Text.ElideNone
        }

        ActionButton {
            compact: true
            showText: false
            iconName: "close"
            tooltipText: "Close command palette"
            onClicked: carouselCommandPalette.close()
        }
    }

    ToneTextField {
        id: carouselCommandSearchField
        objectName: "carouselCommandSearchField"
        Layout.fillWidth: true
        placeholderText: "Search add, transform, arrange, board, template, preflight, save, or export"
        text: root.carouselCommandSearchText
        Accessible.name: "Search Carousel commands"
        onTextEdited: {
            root.carouselCommandSearchText = text
            root.carouselCommandIndex = 0
            carouselCommandResultList.positionViewAtBeginning()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Down) {
                root.carouselCommandIndex = Math.min(carouselCommandResultList.count - 1,
                                                     root.carouselCommandIndex + 1)
                carouselCommandResultList.positionViewAtIndex(root.carouselCommandIndex,
                                                              ListView.Contain)
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                root.carouselCommandIndex = Math.max(0, root.carouselCommandIndex - 1)
                carouselCommandResultList.positionViewAtIndex(root.carouselCommandIndex,
                                                              ListView.Contain)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                const matches = root.filteredCarouselCommands()
                if (root.carouselCommandIndex >= 0 && root.carouselCommandIndex < matches.length)
                    root.runCarouselCommand(String(matches[root.carouselCommandIndex].commandId))
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                carouselCommandPalette.close()
                event.accepted = true
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.alpha(theme.lineSoft, 0.36)
    }

    ListView {
        id: carouselCommandResultList
        objectName: "carouselCommandResultList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 4
        model: root.filteredCarouselCommands()
        currentIndex: root.carouselCommandIndex
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: ItemDelegate {
            id: commandResultDelegate
            required property var modelData
            required property int index
            width: ListView.view.width
            height: modelData.enabled ? 58 : 72
            hoverEnabled: true
            enabled: Boolean(modelData.enabled)
            Accessible.name: String(modelData.label)
            Accessible.description: modelData.enabled
                                    ? String(modelData.category) + " command"
                                    : String(modelData.disabledReason)
            onClicked: root.runCarouselCommand(String(modelData.commandId))

            background: Rectangle {
                radius: theme.radius8
                color: commandResultDelegate.index === root.carouselCommandIndex
                       ? theme.alpha(theme.accentWarm400, commandResultDelegate.enabled ? 0.17 : 0.09)
                       : commandResultDelegate.hovered
                         ? theme.alpha(theme.glassBase, 0.28)
                         : "transparent"
                border.width: commandResultDelegate.index === root.carouselCommandIndex ? 1 : 0
                border.color: theme.alpha(theme.accentWarm400, commandResultDelegate.enabled ? 0.62 : 0.28)
            }

            contentItem: RowLayout {
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: String(commandResultDelegate.modelData.label)
                        color: commandResultDelegate.enabled ? theme.textStrong : theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }

                    Label {
                        Layout.fillWidth: true
                        text: commandResultDelegate.enabled
                              ? String(commandResultDelegate.modelData.category)
                              : String(commandResultDelegate.modelData.category) + " · " +
                                String(commandResultDelegate.modelData.disabledReason)
                        color: commandResultDelegate.enabled ? theme.textMuted : theme.warning
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                }

                Label {
                    visible: String(commandResultDelegate.modelData.shortcut || "").length > 0
                    text: String(commandResultDelegate.modelData.shortcut || "")
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    elide: Text.ElideNone
                }
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: carouselCommandResultList.count === 0
        text: "No commands match this search."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideNone
    }

    Label {
        Layout.fillWidth: true
        text: carouselCommandResultList.count + " command" +
              (carouselCommandResultList.count === 1 ? "" : "s") +
              " · Up/Down to navigate · Enter to run · Escape to close"
        color: theme.textFaint
        font.family: theme.fontMono
        font.pixelSize: 9
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
    }
}
