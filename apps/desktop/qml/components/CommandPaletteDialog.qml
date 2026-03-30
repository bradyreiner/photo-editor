import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    dim: true
    width: 480
    padding: 0
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.22)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Theme { id: theme }

    property var commands: []

    function buildCommands() {
        let items = []
        for (let i = 0; i < commands.length; ++i) {
            const cmd = commands[i]
            if (cmd.action && cmd.action.text && cmd.action.text.length > 0) {
                items.push({
                    label: cmd.label || cmd.action.text,
                    shortcutHint: cmd.shortcutHint || "",
                    category: cmd.category || "",
                    action: cmd.action,
                    enabled: cmd.action.enabled !== false
                })
            }
        }
        return items
    }

    function fuzzyMatch(text, query) {
        const lowerText = text.toLowerCase()
        const lowerQuery = query.toLowerCase()
        if (lowerQuery.length === 0) return true
        let qi = 0
        for (let ti = 0; ti < lowerText.length && qi < lowerQuery.length; ++ti) {
            if (lowerText[ti] === lowerQuery[qi]) qi++
        }
        return qi === lowerQuery.length
    }

    function filteredCommands() {
        const all = buildCommands()
        const query = searchField.text.trim()
        if (query.length === 0) return all
        let result = []
        for (let i = 0; i < all.length; ++i) {
            const searchable = (all[i].category ? all[i].category + " " : "") + all[i].label
            if (fuzzyMatch(searchable, query)) {
                result.push(all[i])
            }
        }
        return result
    }

    onOpened: {
        searchField.text = ""
        searchField.forceActiveFocus()
        listModel.reload()
    }

    ListModel {
        id: listModel

        function reload() {
            clear()
            const items = root.filteredCommands()
            for (let i = 0; i < items.length; ++i) {
                append({
                    label: items[i].label,
                    shortcutHint: items[i].shortcutHint,
                    category: items[i].category,
                    itemEnabled: items[i].enabled,
                    commandIndex: i
                })
            }
        }
    }

    background: Rectangle {
        radius: theme.radius16
        color: theme.ink900
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.85)

        layer.enabled: true
        layer.effect: Item {}
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Label {
                    text: ">"
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: "Type a command..."
                    placeholderTextColor: theme.textFaint
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 14
                    background: null
                    selectByMouse: true

                    onTextChanged: listModel.reload()

                    Keys.onDownPressed: {
                        if (commandList.count > 0) {
                            commandList.currentIndex = Math.min(commandList.currentIndex + 1, commandList.count - 1)
                        }
                    }

                    Keys.onUpPressed: {
                        if (commandList.count > 0) {
                            commandList.currentIndex = Math.max(commandList.currentIndex - 1, 0)
                        }
                    }

                    Keys.onReturnPressed: executeSelected()
                    Keys.onEnterPressed: executeSelected()

                    function executeSelected() {
                        if (commandList.currentIndex >= 0 && commandList.currentIndex < commandList.count) {
                            const item = listModel.get(commandList.currentIndex)
                            if (item && item.itemEnabled) {
                                const commands = root.filteredCommands()
                                if (item.commandIndex < commands.length) {
                                    root.close()
                                    commands[item.commandIndex].action.trigger()
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.alpha(theme.lineSoft, 0.5)
            }
        }

        ListView {
            id: commandList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 320)
            clip: true
            model: listModel
            currentIndex: 0
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: commandDelegate
                width: commandList.width
                height: 36
                color: ListView.isCurrentItem ? theme.alpha(theme.accentCool400, 0.15) : mouseArea.containsMouse ? theme.alpha(theme.textFaint, 0.08) : "transparent"

                required property int index
                required property string label
                required property string shortcutHint
                required property string category
                required property bool itemEnabled

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true

                    onClicked: {
                        if (commandDelegate.itemEnabled) {
                            commandList.currentIndex = commandDelegate.index
                            searchField.executeSelected()
                        }
                    }

                    onEntered: commandList.currentIndex = commandDelegate.index
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 8

                    Label {
                        visible: commandDelegate.category.length > 0
                        text: commandDelegate.category
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: 11
                    }

                    Label {
                        Layout.fillWidth: true
                        text: commandDelegate.label
                        color: commandDelegate.itemEnabled ? theme.textMain : theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    Label {
                        visible: commandDelegate.shortcutHint.length > 0
                        text: commandDelegate.shortcutHint
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: 11
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: "transparent"

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: theme.alpha(theme.lineSoft, 0.5)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14

                Label {
                    text: commandList.count + " commands"
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: "↑↓ Navigate  ↵ Run  Esc Close"
                    color: theme.textFaint
                    font.family: theme.fontMono
                    font.pixelSize: 10
                }
            }
        }
    }
}
