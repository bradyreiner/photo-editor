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
    property string previewText: ""
    property string triggerObjectName: ""
    property string searchText: ""
    property var fontFamilies: []
    property var normalizedFontFamilies: []
    property bool familiesLoaded: false
    readonly property var installedFamilies: root.fontFamilies
    readonly property string resolvedPreviewText: {
        const supplied = String(root.previewText || "").replace(/\s+/g, " ").trim()
        return supplied.length > 0 ? supplied : qsTr("The quick brown fox jumps over the lazy dog 0123456789")
    }
    readonly property string previewFamily: fontList.currentIndex >= 0 && fontList.currentIndex < root.filteredFamilies.length
        ? String(root.filteredFamilies[fontList.currentIndex])
        : (root.currentFamily || theme.fontUi)
    readonly property var filteredFamilies: {
        const query = root.searchText.trim().toLowerCase()
        const exact = []
        const prefix = []
        const partial = []
        for (const entry of root.normalizedFontFamilies || []) {
            if (!query.length || entry.folded === query) {
                exact.push(entry.family)
            } else if (entry.folded.startsWith(query)) {
                prefix.push(entry.family)
            } else if (entry.folded.includes(query)) {
                partial.push(entry.family)
            }
        }
        return exact.concat(prefix, partial)
    }
    signal familySelected(string family)

    function loadFamilies(forceRefresh) {
        if (root.familiesLoaded && forceRefresh !== true)
            return root.fontFamilies

        let choices = root.availableFonts || []
        if (choices.length === 0 && root.controller && typeof root.controller.availablePhotoTextFonts === "function")
            choices = root.controller.availablePhotoTextFonts()
        if ((!choices || choices.length === 0) && root.currentFamily)
            choices = [root.currentFamily]
        root.normalizedFontFamilies = Array.from(choices || [])
            .map(family => ({family: String(family), folded: String(family).toLowerCase()}))
            .filter(entry => entry.family.length > 0)
            .sort((left, right) => left.folded.localeCompare(right.folded) || left.family.localeCompare(right.family))
        root.fontFamilies = root.normalizedFontFamilies.map(entry => entry.family)
        root.familiesLoaded = true
        return root.fontFamilies
    }

    function choose(family) {
        const selected = String(family || "")
        if (!root.enabled || !selected.length)
            return
        root.familySelected(selected)
        fontPopup.closeTransient(true)
    }

    function invalidateFontFamilies() {
        root.familiesLoaded = false
        if (fontPopup.visible)
            root.loadFamilies(true)
    }

    function selectedFamilyIndex() {
        const current = String(root.currentFamily).toLowerCase()
        return root.filteredFamilies.findIndex(family => String(family).toLowerCase() === current)
    }

    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onPhotoTextFontsChanged() {
            root.invalidateFontFamilies()
        }
    }

    Theme { id: theme }

    implicitWidth: theme.menuMinWidth
    implicitHeight: theme.controlHeight

    RowLayout {
        anchors.fill: parent
        spacing: theme.space4

        Components.MiniButton {
            id: fontTrigger
            objectName: root.triggerObjectName.length > 0
                ? root.triggerObjectName
                : root.objectName + "Button"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.fillHeight: true
            text: root.currentFamily || qsTr("Choose font")
            iconName: "chevron-down"
            role: "secondary"
            active: fontPopup.visible
            enabled: root.enabled
            Accessible.name: qsTr("Text font family")
            tooltip: qsTr("Search installed fonts and preview each family.")
            onClicked: {
                root.searchText = ""
                fontPopup.toggleTransient(fontTrigger)
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

    AppPopup {
        id: fontPopup
        objectName: root.objectName + "Popup"
        parent: Overlay.overlay
        transientMenu: true
        margins: theme.space8
        modal: false
        dim: false
        focus: true
        padding: theme.space8
        property real anchorX: 0
        property real anchorTop: 0
        property real anchorBottom: 0
        readonly property real preferredPopupHeight: Math.min(theme.scaled(500), popupAvailableHeight)
        readonly property real spaceAboveAnchor: Math.max(0, anchorTop - margins)
        readonly property real spaceBelowAnchor: Math.max(0, (parent ? parent.height : preferredPopupHeight) - margins - anchorBottom - theme.space4)
        readonly property bool opensAboveAnchor: spaceBelowAnchor < preferredPopupHeight && spaceAboveAnchor > spaceBelowAnchor
        readonly property real resolvedPopupHeight: Math.max(1, Math.min(preferredPopupHeight, Math.max(spaceAboveAnchor, spaceBelowAnchor)))
        width: Math.min(popupAvailableWidth,
                        Math.max(theme.scaled(360), Math.min(theme.scaled(440), root.width)))
        height: resolvedPopupHeight
        x: parent ? Math.max(margins, Math.min(anchorX, parent.width - width - margins)) : 0
        y: parent
           ? Math.max(margins, Math.min(parent.height - height - margins,
                 opensAboveAnchor ? anchorTop - height - theme.space4 : anchorBottom + theme.space4))
           : 0

        function updatePopupAnchor() {
            if (!parent)
                return
            const origin = root.mapToItem(parent, 0, 0)
            anchorX = origin.x
            anchorTop = origin.y
            anchorBottom = origin.y + root.height
        }

        onAboutToShow: {
            updatePopupAnchor()
            root.loadFamilies()
            // Initialize before the enter transition: the current font should
            // already be highlighted while the popup is appearing.
            fontList.currentIndex = root.filteredFamilies.length > 0
                ? Math.max(0, root.selectedFamilyIndex()) : -1
        }
        onOpened: {
            Qt.callLater(function() {
                if (!fontPopup.visible)
                    return
                fontList.currentIndex = root.filteredFamilies.length > 0
                    ? Math.max(0, root.selectedFamilyIndex()) : -1
                searchField.forceActiveFocus()
            })
        }
        onClosed: root.searchText = ""

        contentItem: ColumnLayout {
            spacing: theme.space6
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: theme.space2
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Choose a font")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeSection
                        font.weight: theme.weightSection
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Preview the family before applying it")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        elide: Text.ElideRight
                    }
                }
                Components.MiniButton {
                    objectName: root.objectName + "Close"
                    square: true
                    iconName: "close"
                    role: "quiet"
                    Accessible.name: qsTr("Close font picker")
                    tooltip: Accessible.name
                    onClicked: fontPopup.closeTransient(true)
                }
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
                    if (fontList.count > 0 && fontList.currentIndex < 0)
                        fontList.currentIndex = 0
                    event.accepted = true
                }
                Keys.onEscapePressed: function(event) {
                    fontPopup.closeTransient(true)
                    event.accepted = true
                }
            }
            Label {
                objectName: root.objectName + "MatchStatus"
                Layout.fillWidth: true
                text: root.filteredFamilies.length > 0
                    ? qsTr("%1 families · Enter to apply · Esc to close").arg(root.filteredFamilies.length)
                    : (root.fontFamilies.length > 0
                        ? qsTr("No fonts match this search.")
                        : qsTr("No installed font families were found."))
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            Rectangle {
                objectName: root.objectName + "Specimen"
                Layout.fillWidth: true
                Layout.preferredHeight: theme.scaled(70)
                visible: fontPopup.height >= theme.scaled(300)
                radius: theme.radiusControl
                color: theme.fieldFill
                border.width: theme.hairlineWidth
                border.color: theme.controlEdge
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.space8
                    spacing: theme.space2
                    Label {
                        objectName: root.objectName + "SpecimenFamily"
                        Layout.fillWidth: true
                        text: root.previewFamily
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: root.objectName + "SpecimenText"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: root.resolvedPreviewText
                        color: theme.textStrong
                        font.family: root.previewFamily
                        font.pixelSize: theme.scaled(22)
                        font.weight: theme.weightControl
                        verticalAlignment: Text.AlignVCenter
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                    }
                }
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
                Keys.onEscapePressed: function(event) {
                    fontPopup.closeTransient(true)
                    event.accepted = true
                }
                Keys.onUpPressed: function(event) {
                    if (currentIndex <= 0) {
                        searchField.forceActiveFocus()
                    } else {
                        currentIndex -= 1
                    }
                    event.accepted = true
                }
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
                            objectName: root.objectName + "Option" + String(fontOption.index) + "Sample"
                            text: "Ag 123"
                            color: theme.textMuted
                            font.family: String(fontOption.modelData)
                            font.pixelSize: theme.typeBody
                            font.weight: theme.weightControl
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
        }
        background: PopupSurface {}
    }

    onSearchTextChanged: {
        if (searchField.text !== root.searchText)
            searchField.text = root.searchText
        if (fontPopup.visible)
            fontList.currentIndex = root.filteredFamilies.length > 0 ? 0 : -1
    }
    onAvailableFontsChanged: root.invalidateFontFamilies()
    onControllerChanged: root.invalidateFontFamilies()
    onCurrentFamilyChanged: {
        if (root.familiesLoaded && root.fontFamilies.length === 0 && root.currentFamily.length > 0)
            root.invalidateFontFamilies()
    }
    onXChanged: if (fontPopup.visible) fontPopup.updatePopupAnchor()
    onYChanged: if (fontPopup.visible) fontPopup.updatePopupAnchor()
    onWidthChanged: if (fontPopup.visible) fontPopup.updatePopupAnchor()
    onHeightChanged: if (fontPopup.visible) fontPopup.updatePopupAnchor()
    onEnabledChanged: if (!root.enabled && fontPopup.visible) fontPopup.close()
}
