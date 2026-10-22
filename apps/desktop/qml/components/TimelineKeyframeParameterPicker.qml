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
    readonly property var filteredOptions: {
        const words = searchText.trim().toLowerCase().split(/\s+/).filter(word => word.length > 0)
        return (options || []).filter(option => words.every(word =>
            (String(option.label || "") + " " + String(option.parameter || ""))
                .toLowerCase().includes(word)))
    }
    readonly property string currentLabel: {
        for (const option of options || []) {
            if (option.parameter === currentParameter) return option.label
        }
        return qsTr("Choose parameter")
    }
    signal parameterSelected(string parameter)
    implicitWidth: theme.scaled(220)
    implicitHeight: theme.controlHeight
    Theme { id: theme }

    function choose(index) {
        if (index < 0 || index >= filteredOptions.length) return
        parameterSelected(String(filteredOptions[index].parameter))
        pickerPopup.close()
    }

    ActionButton {
        objectName: root.objectName + "Trigger"
        anchors.fill: parent
        text: root.currentLabel
        iconName: "chevron-down"
        compact: true
        enabled: root.enabled && root.options.length > 0
        Accessible.name: qsTr("Animation parameter: %1").arg(root.currentLabel)
        tooltipText: qsTr("Search clip controls, effect amounts, and adjustment layers to animate.")
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
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(440), Math.max(1, parent ? parent.width - theme.space24 : 440))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(440), Math.max(1, parent ? parent.height - theme.space24 : 440))))
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            root.searchText = ""
            parameterList.currentIndex = Math.max(0, root.filteredOptions.findIndex(
                option => option.parameter === root.currentParameter))
            Qt.callLater(() => searchField.forceActiveFocus())
        }
        background: PopupSurface {}
        contentItem: ColumnLayout {
            spacing: theme.space8
            Label {
                text: qsTr("Animate a parameter")
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeSection
                font.weight: Font.DemiBold
            }
            ToneTextField {
                id: searchField
                objectName: root.objectName + "Search"
                Layout.fillWidth: true
                placeholderText: qsTr("Search controls, effects, or layer names")
                accessibleLabel: qsTr("Search animation parameters")
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
            Label {
                Layout.fillWidth: true
                text: root.filteredOptions.length ? qsTr("%1 parameters · key counts at right").arg(root.filteredOptions.length)
                                                  : qsTr("No matching parameters")
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
                    objectName: root.objectName + "Option" + index
                    width: parameterList.width
                    height: theme.controlHeight
                    highlighted: parameterList.currentIndex === index
                    Accessible.name: String(modelData.label) + ", " + Number(modelData.keyframeCount || 0) + " keyframes"
                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: String(optionRow.modelData.label)
                            elide: Text.ElideRight
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeControl
                        }
                        Label {
                            text: Number(optionRow.modelData.keyframeCount || 0) > 0
                                  ? String(optionRow.modelData.keyframeCount) + " ◆" : ""
                            color: theme.accentWarm400
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeCaption
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
