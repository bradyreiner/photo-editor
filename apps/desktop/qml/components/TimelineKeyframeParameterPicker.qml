pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    // Each option has a stable parameter id, a display label, and a key count.
    property var options: []
    property string currentParameter: ""
    property string searchText: ""
    property bool animatedOnly: false
    property string selectedGroup: "All"
    readonly property bool currentParameterAvailable: (options || []).some(
        option => option && String(option.parameter || "") === currentParameter)
    readonly property var groups: ["All", "Motion", "Effects", "Color", "Audio", "Text", "Clip"]
    readonly property var filteredOptions: {
        const words = searchText.trim().toLowerCase().split(/\s+/).filter(word => word.length > 0)
        const matching = (options || []).filter(option => {
            const channel = option || ({})
            const group = root.groupForParameter(channel.parameter)
            return words.every(word =>
                (String(channel.label || "") + " " + String(channel.parameter || "") + " "
                 + group).toLowerCase().includes(word))
                && (!animatedOnly || Number(channel.keyframeCount || 0) > 0)
                && (selectedGroup === "All" || group === selectedGroup)
        })
        return matching.map(function(option, index) {
            const channel = option || ({})
            const group = root.groupForParameter(channel.parameter)
            const previous = index > 0 ? matching[index - 1] : null
            const row = Object.assign({}, channel)
            row.optionGroup = group
            row.groupHeadingVisible = !previous
                    || root.groupForParameter(previous.parameter) !== group
            return row
        })
    }
    readonly property string currentLabel: {
        for (const option of options || []) {
            const channel = option || ({})
            if (channel.parameter === currentParameter) return channel.label
        }
        return qsTr("Choose parameter")
    }
    signal parameterSelected(string parameter)
    readonly property real naturalTriggerWidth: parameterTrigger.implicitWidth
    implicitWidth: theme.scaled(220)
    implicitHeight: theme.controlHeight
    Theme { id: theme }

    function groupForParameter(parameter) {
        const value = String(parameter || "")
        if (value.indexOf("effect:") === 0) return "Effects"
        if (value.indexOf("grade:") === 0) return "Color"
        if (value === "audioGainDb" || value.indexOf("audio") === 0) return "Audio"
        if (value.indexOf("title") === 0 || value.indexOf("caption") === 0) return "Text"
        if (value.indexOf("frame") === 0 || value === "sourceTime") return "Motion"
        return "Clip"
    }

    function choose(index) {
        if (index < 0 || index >= filteredOptions.length) return
        const option = filteredOptions[index]
        if (!option) return
        parameterSelected(String(option.parameter || ""))
        pickerPopup.close()
    }

    ActionButton {
        id: parameterTrigger
        objectName: root.objectName + "Trigger"
        anchors.fill: parent
        text: root.currentLabel
        iconName: "chevron-down"
        compact: true
        enabled: root.enabled && root.options.length > 0
        Accessible.name: qsTr("Animation parameter: %1").arg(root.currentLabel)
        tooltipText: qsTr("Search and group clip controls, effect amounts, and adjustment layers to animate.")
        onClicked: pickerPopup.open()
    }

    AppPopup {
        id: pickerPopup
        objectName: root.objectName + "Popup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        focus: true
        padding: standardPopupPadding
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(460), Math.max(1, parent ? parent.width - theme.space24 : 460))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(500), Math.max(1, parent ? parent.height - theme.space24 : 500))))
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            root.searchText = ""
            parameterList.currentIndex = root.filteredOptions.findIndex(
                option => option && option.parameter === root.currentParameter)
            Qt.callLater(() => searchField.forceActiveFocus())
        }
        background: PopupSurface {}
        contentItem: ColumnLayout {
            spacing: theme.space8
            Label {
                text: qsTr("Animate a channel")
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeSection
                font.weight: Font.DemiBold
            }
            ToneTextField {
                id: searchField
                objectName: root.objectName + "Search"
                Layout.fillWidth: true
                placeholderText: qsTr("Search motion, color, effects, or clip controls")
                accessibleLabel: qsTr("Search animation channels")
                text: root.searchText
                onTextEdited: {
                    root.searchText = text
                    parameterList.currentIndex = root.filteredOptions.length ? 0 : -1
                }
                onAccepted: root.choose(parameterList.currentIndex)
                Keys.onDownPressed: function(event) {
                    parameterList.forceActiveFocus()
                    event.accepted = true
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6
                FilterChip {
                    objectName: root.objectName + "AnimatedOnly"
                    text: qsTr("Animated only")
                    active: root.animatedOnly
                    Accessible.name: qsTr("Show channels with keyframes only")
                    tooltipText: qsTr("Filter to channels that already contain keyframes.")
                    onClicked: {
                        root.animatedOnly = !root.animatedOnly
                        parameterList.currentIndex = root.filteredOptions.length ? 0 : -1
                    }
                }
                ToneComboBox {
                    objectName: root.objectName + "GroupFilter"
                    Layout.fillWidth: true
                    accessibleLabel: qsTr("Animation channel group")
                    model: root.groups
                    currentIndex: Math.max(0, root.groups.indexOf(root.selectedGroup))
                    onActivated: function(index) {
                        root.selectedGroup = String(root.groups[index] || "All")
                        parameterList.currentIndex = root.filteredOptions.length ? 0 : -1
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: root.filteredOptions.length
                      ? qsTr("%1 channels · keyframe counts at right").arg(root.filteredOptions.length)
                      : qsTr("No matching channels")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }
            ListView {
                id: parameterList
                objectName: root.objectName + "List"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.filteredOptions
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar {}
                Keys.onReturnPressed: root.choose(currentIndex)
                Keys.onEnterPressed: root.choose(currentIndex)
                delegate: ItemDelegate {
                    id: optionRow
                    required property int index
                    required property var modelData
                    readonly property var optionData: modelData || ({})
                    readonly property string optionGroup: String(optionData.optionGroup
                                                                  || root.groupForParameter(optionData.parameter))
                    readonly property bool groupHeadingVisible: optionData.groupHeadingVisible === true
                    objectName: root.objectName + "Option" + index
                    width: parameterList.width
                    height: theme.controlHeight + (groupHeadingVisible ? theme.scaled(17) : 0)
                    highlighted: parameterList.currentIndex === index
                    Accessible.name: optionGroup + ", " + String(optionData.label || qsTr("Animation channel")) + ", "
                            + Number(optionData.keyframeCount || 0) + " keyframes"
                    contentItem: ColumnLayout {
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            visible: optionRow.groupHeadingVisible
                            text: optionRow.optionGroup.toUpperCase()
                            color: theme.textFaint
                            font.family: theme.fontNumeric
                            font.features: theme.numericFontFeatures
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6
                            Label {
                                Layout.fillWidth: true
                                text: String(optionRow.optionData.label || qsTr("Animation channel"))
                                elide: Text.ElideRight
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeControl
                            }
                            Label {
                                text: Number(optionRow.optionData.keyframeCount || 0) > 0
                                      ? String(optionRow.optionData.keyframeCount) + " ◆" : ""
                                color: theme.accentWarm400
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: theme.typeCaption
                            }
                        }
                    }
                    background: Rectangle {
                        color: optionRow.highlighted ? theme.alpha(theme.accentWarm400, 0.12) : "transparent"
                    }
                    onClicked: root.choose(index)
                }
            }
        }
    }
    onEnabledChanged: if (!enabled) pickerPopup.close()
}
