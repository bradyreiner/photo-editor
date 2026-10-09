pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    property var controller: null
    property var availableFonts: []
    property string currentFamily: ""
    property string triggerObjectName: ""
    property string searchText: ""
    property var fontFamilies: []
    readonly property var installedFamilies: root.fontFamilies
    readonly property var filteredFamilies: {
        const query = root.searchText.trim().toLowerCase()
        const families = root.fontFamilies || []
        if (!query.length) return families
        const rank = family => {
            const name = String(family).toLowerCase()
            return name === query ? 0 : name.startsWith(query) ? 1 : 2
        }
        return families.filter(family => String(family).toLowerCase().includes(query))
            .sort((left, right) => rank(left) - rank(right) || String(left).localeCompare(String(right)))
    }
    signal familySelected(string family)

    function loadFamilies() {
        let choices = root.availableFonts || []
        if (choices.length === 0 && root.controller && typeof root.controller.availablePhotoTextFonts === "function")
            choices = root.controller.availablePhotoTextFonts()
        if ((!choices || choices.length === 0) && root.currentFamily)
            choices = [root.currentFamily]
        root.fontFamilies = Array.from(choices || []).sort((left, right) =>
            String(left).toLowerCase().localeCompare(String(right).toLowerCase()))
        return root.fontFamilies
    }

    function choose(family) {
        const selected = String(family || "")
        if (!root.enabled || !selected.length)
            return
        root.familySelected(selected)
        fontPopup.close()
    }

    Theme { id: theme }

    implicitWidth: theme.menuMinWidth
    implicitHeight: theme.controlHeight

    RowLayout {
        anchors.fill: parent
        spacing: theme.space4

        Components.MiniButton {
            objectName: root.triggerObjectName.length > 0
                ? root.triggerObjectName
                : root.objectName + "Button"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.fillHeight: true
            text: root.currentFamily || qsTr("Choose font")
            iconName: "chevron-down"
            role: "secondary"
            enabled: root.enabled
            Accessible.name: qsTr("Text font family")
            tooltip: qsTr("Search installed fonts and preview each family.")
            onClicked: {
                root.searchText = ""
                fontPopup.open()
            }
        }
        Label {
            objectName: root.objectName + "Preview"
            Layout.preferredWidth: theme.scaled(42)
            Layout.alignment: Qt.AlignVCenter
            text: "Aa"
            color: theme.textStrong
            font.family: root.currentFamily || theme.fontUi
            font.pixelSize: theme.scaled(22)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Accessible.name: qsTr("Font sample")
        }
    }

    Popup {
        id: fontPopup
        objectName: root.objectName + "Popup"
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: theme.space12
        width: Math.min(theme.scaled(480), Math.max(0, parent ? parent.width - theme.space24 : theme.scaled(480)))
        height: Math.min(theme.scaled(520), Math.max(0, parent ? parent.height - theme.scaled(32) : theme.scaled(520)))
        onOpened: {
            root.loadFamilies()
            fontList.currentIndex = root.filteredFamilies.length > 0
                ? Math.max(0, root.filteredFamilies.findIndex(family =>
                    String(family).toLowerCase() === String(root.currentFamily).toLowerCase()))
                : -1
            Qt.callLater(() => searchField.forceActiveFocus())
        }
        onClosed: root.searchText = ""

        contentItem: ColumnLayout {
            spacing: theme.space8
            Label {
                Layout.fillWidth: true
                text: qsTr("Choose a font")
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeSection
                font.weight: theme.weightSection
            }
            Components.ToneTextField {
                id: searchField
                objectName: root.objectName + "Search"
                Layout.fillWidth: true
                placeholderText: qsTr("Search installed fonts")
                accessibleLabel: qsTr("Search installed fonts")
                text: root.searchText
                onTextEdited: root.searchText = text
                onAccepted: {
                    const families = root.filteredFamilies
                    const index = fontList.currentIndex >= 0 && fontList.currentIndex < families.length
                        ? fontList.currentIndex : (families.length > 0 ? 0 : -1)
                    if (index >= 0)
                        root.choose(String(families[index]))
                }
                Keys.onDownPressed: function(event) {
                    fontList.forceActiveFocus()
                    if (fontList.count > 0)
                        fontList.currentIndex = 0
                    event.accepted = true
                }
            }
            Label {
                objectName: root.objectName + "MatchStatus"
                Layout.fillWidth: true
                text: root.filteredFamilies.length > 0
                    ? qsTr("%1 font families").arg(root.filteredFamilies.length)
                    : qsTr("No fonts match this search.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            ListView {
                id: fontList
                objectName: root.objectName + "List"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.filteredFamilies
                currentIndex: -1
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                Accessible.name: qsTr("Installed font families")
                Keys.onReturnPressed: function(event) {
                    if (currentIndex >= 0 && currentIndex < root.filteredFamilies.length)
                        root.choose(String(root.filteredFamilies[currentIndex]))
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    if (currentIndex >= 0 && currentIndex < root.filteredFamilies.length)
                        root.choose(String(root.filteredFamilies[currentIndex]))
                    event.accepted = true
                }
                delegate: ItemDelegate {
                    id: fontOption
                    required property var modelData
                    required property int index
                    objectName: root.objectName + "Option" + String(index)
                    width: fontList.width
                    implicitHeight: theme.controlHeightTall
                    highlighted: fontList.currentIndex === index
                    Accessible.name: String(fontOption.modelData)
                    onClicked: root.choose(String(fontOption.modelData))
                    contentItem: RowLayout {
                        spacing: theme.space8
                        Label {
                            Layout.fillWidth: true
                            text: String(fontOption.modelData)
                            color: fontOption.highlighted ? theme.textStrong : theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeControl
                            elide: Text.ElideRight
                        }
                        Label {
                            text: "Ag 123"
                            color: theme.textMuted
                            font.family: String(fontOption.modelData)
                            font.pixelSize: theme.typeControl
                        }
                    }
                    background: Rectangle {
                        radius: theme.radiusControl
                        color: fontOption.highlighted ? theme.opticalSelectedFill
                               : fontOption.hovered ? theme.opticalHoverFill : "transparent"
                    }
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
            Components.MiniButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Cancel")
                role: "secondary"
                onClicked: fontPopup.close()
            }
        }
        background: Components.PopupSurface {
            radius: theme.radiusControl
            fillColor: theme.popupFill
            borderColor: theme.alpha(theme.controlEdge, 0.82)
        }
    }

    onSearchTextChanged: {
        if (searchField.text !== root.searchText)
            searchField.text = root.searchText
        if (fontPopup.visible)
            fontList.currentIndex = root.filteredFamilies.length > 0 ? 0 : -1
    }
    onEnabledChanged: if (!root.enabled && fontPopup.visible) fontPopup.close()
}
