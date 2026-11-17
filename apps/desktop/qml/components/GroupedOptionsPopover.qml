import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root

    property string title: "Options"
    property var groups: []
    property string currentGroup: groups.length > 0 ? String(groups[0].key) : ""
    property string settingsLabel: ""
    readonly property var currentItems: {
        for (let group of groups) {
            if (String(group.key) === currentGroup)
                return group.items || []
        }
        return groups.length > 0 ? groups[0].items || [] : []
    }
    signal itemTriggered(string key)
    signal settingsRequested()

    Theme { id: theme }

    width: Math.min(popupAvailableWidth, theme.scaled(480))
    focus: true
    transientMenu: true

    function focusOption(index, direction) {
        for (let offset = 1; offset <= currentItems.length; ++offset) {
            const next = (index + direction * offset + currentItems.length) % currentItems.length
            const item = optionRepeater.itemAt(next)
            if (item && item.enabled) {
                item.forceActiveFocus(Qt.TabFocusReason)
                if (item.y < optionsScroll.contentItem.contentY)
                    optionsScroll.contentItem.contentY = item.y
                else if (item.y + item.height > optionsScroll.contentItem.contentY + optionsScroll.availableHeight)
                    optionsScroll.contentItem.contentY = Math.max(0, item.y + item.height - optionsScroll.availableHeight)
                return
            }
        }
    }

    onOpened: {
        const selected = groups.findIndex(function(group) { return String(group.key) === root.currentGroup })
        const tab = groupRepeater.itemAt(Math.max(0, selected))
        if (tab)
            tab.forceActiveFocus(Qt.TabFocusReason)
    }
    onCurrentGroupChanged: if (optionsScroll.contentItem) optionsScroll.contentItem.contentY = 0

    contentItem: ColumnLayout {
        spacing: theme.gapControl

        RowLayout {
            id: heading
            Layout.fillWidth: true
            BodyLabel {
                Layout.fillWidth: true
                text: root.title
            }
            ActionButton {
                iconName: "close"
                showText: false
                compact: true
                tooltipText: "Close options"
                onClicked: root.closeTransient(true)
            }
        }

        Flow {
            id: groupFlow
            Layout.fillWidth: true
            spacing: theme.space4
            Repeater {
                id: groupRepeater
                model: root.groups
                delegate: FilterChip {
                    required property var modelData
                    objectName: "optionsGroup_" + modelData.key
                    text: modelData.label
                    active: root.currentGroup === String(modelData.key)
                    onClicked: root.currentGroup = String(modelData.key)
                }
            }
        }

        ScrollView {
            id: optionsScroll
            objectName: "groupedOptionsScroll"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(optionList.implicitHeight, Math.max(0,
                root.popupAvailableHeight - root.topPadding - root.bottomPadding
                - heading.implicitHeight - groupFlow.implicitHeight
                - (settingsButton.visible ? settingsButton.implicitHeight : 0) - theme.gapControl * 3))
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: optionList
                width: optionsScroll.availableWidth
                spacing: theme.space2
                Repeater {
                    id: optionRepeater
                    model: root.currentItems
                    delegate: PopupMenuRow {
                        onDismissRequested: root.closeTransient(true)
                        previousItem: index > 0 ? root.currentItems[index - 1] : ({})
                        objectNamePrefix: "groupedOptions"
                        onActivated: function(key) {
                            root.closeTransient(false)
                            root.itemTriggered(key)
                        }
                        onNavigateRequested: function(direction) { root.focusOption(index, direction) }
                        onFirstRequested: root.focusOption(-1, 1)
                        onLastRequested: root.focusOption(0, -1)
                    }
                }
            }
        }

        ActionButton {
            id: settingsButton
            visible: root.settingsLabel.length > 0
            text: root.settingsLabel
            iconName: "settings"
            onClicked: {
                root.closeTransient(false)
                root.settingsRequested()
            }
        }
    }
}
