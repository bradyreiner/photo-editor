import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    objectName: "commandPaletteDialog"
    modal: true
    dim: true
    width: 480
    padding: 0
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.22)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Theme { id: theme }

    property var commands: []
    property var requiredCommandLabels: []
    readonly property int commandCount: commands.length
    readonly property int requiredCommandCount: requiredCommandLabels.length
    readonly property string dialogVisibilityContract: "dialog-visibility:v1|command-palette|always:search-results-count-shortcuts|contextual:filtered-results-disabled-state-empty-state|one-expansion:all-commands-through-search|hover-focus:row-current-selection|diagnostic:missing-required-commands|preserve:functionality"
    readonly property string dialogSurfaceContract: "dialog-surface:v1|command-palette|search:always|results:dominant|footer:keyboard-help|rows:shared-readable|no-elide:true|no-overlap:true"
    readonly property string functionPreservationMap: "function-preservation:v1|command-search:always|command-run:keyboard-and-row|disabled-reasons:row-state|shortcuts:footer-and-row|missing-required:diagnostic-signature"

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

    function commandLabels() {
        const items = buildCommands()
        let labels = []
        for (let i = 0; i < items.length; ++i)
            labels.push(items[i].label)
        return labels
    }

    function missingRequiredCommandLabels() {
        const labels = commandLabels()
        let missing = []
        for (let i = 0; i < requiredCommandLabels.length; ++i) {
            const label = String(requiredCommandLabels[i] || "")
            if (label.length > 0 && labels.indexOf(label) < 0)
                missing.push(label)
        }
        return missing
    }

    function commandCoverageSignature() {
        return "commands:" + commandLabels().length
                + "|required:" + requiredCommandLabels.length
                + "|missing:" + missingRequiredCommandLabels().join(",")
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

    background: PopupSurface {
        accent: theme.accentWarm400
        accentRail: false
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
                    placeholderText: "Type a command"
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
                opacity: 0.55
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

            delegate: CommandPaletteRow {
                required property int index
                required property string label
                required property string shortcutHint
                required property string category
                required property bool itemEnabled

                rowIndex: index
                current: ListView.isCurrentItem
                onSelected: function(rowIndex) {
                    commandList.currentIndex = rowIndex
                }
                onActivated: function(rowIndex) {
                    commandList.currentIndex = rowIndex
                    searchField.executeSelected()
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
                opacity: 0.55
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
