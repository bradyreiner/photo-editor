pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../../components"

ColumnLayout {
    id: content
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselFontBrowserDialog
    readonly property var selection: root.selection || ({})
    readonly property bool hasTextSelection: Boolean(selection.hasSelection && selection.isText && !selection.locked)
    readonly property var families: root.fontBrowserFamilies()
    readonly property int sectionFontCount: root.fontBrowserSection === "favorites"
        ? carouselDocument.favoriteFontFamilies.length : root.fontBrowserSection === "recent"
        ? carouselDocument.recentFontFamilies.length : carouselDocument.installedFontFamilies.length
    readonly property string currentFamily: String(selection.resolvedTextFontFamily || selection.textFontFamily || "")
    property string chosenFamily: ""
    property bool revealingInitialFamily: false
    property bool initialRevealScheduled: false
    readonly property string selectedFamily: families.indexOf(chosenFamily) >= 0 ? chosenFamily : ""
    readonly property string sampleText: String(selection.text || "Aa Bb Cc 0123").replace(/\s+/g, " ").slice(0, 160)
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function focusInitialTarget() {
        revealingInitialFamily = true;
        chosenFamily = "";
        if (selection.textFontAvailable) {
            for (let index = 0; index < families.length; ++index) {
                if (String(families[index]).toLowerCase() === currentFamily.toLowerCase()) {
                    chosenFamily = String(families[index]);
                    break;
                }
            }
        }
        carouselFontSearchField.forceActiveFocus(Qt.TabFocusReason);
        scheduleInitialReveal();
    }
    function scheduleInitialReveal() {
        if (!revealingInitialFamily || initialRevealScheduled)
            return;
        initialRevealScheduled = true;
        Qt.callLater(function() {
            initialRevealScheduled = false;
            if (revealingInitialFamily && carouselFontFamilyList.currentIndex >= 0)
                carouselFontFamilyList.positionViewAtIndex(carouselFontFamilyList.currentIndex, ListView.Center);
        });
    }
    function selectFamily(index) {
        revealingInitialFamily = false;
        if (families.length === 0)
            return;
        chosenFamily = String(families[Math.max(0, Math.min(index, families.length - 1))]);
        carouselFontFamilyList.positionViewAtIndex(carouselFontFamilyList.currentIndex, ListView.Contain);
    }
    function applySelection() {
        if (selectedFamily.length > 0 && content.hasTextSelection)
            content.root.chooseFontBrowserFamily(selectedFamily);
    }

    RowLayout {
        objectName: "carouselFontBrowserHeader"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            Layout.fillWidth: true
            text: "Installed fonts"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        ActionButton {
            objectName: "carouselFontBrowserCloseButton"
            text: "Close"
            iconName: "close"
            compact: true
            onClicked: content.carouselFontBrowserDialog.close()
        }
    }
    Label {
        objectName: "carouselFontBrowserContext"
        Layout.fillWidth: true
        text: !content.hasTextSelection ? "Select a text layer to choose its font."
              : content.selection.textFontAvailable ? "Current: " + content.currentFamily
              : "Missing: " + content.currentFamily + ". Select a replacement to review text heights before applying."
        color: content.selection.textFontAvailable ? content.theme.textMuted : content.theme.warning
        font.family: content.theme.fontUi
        font.pixelSize: content.theme.typeCaption
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    ToneTextField {
        id: carouselFontSearchField
        objectName: "carouselFontSearchField"
        Layout.fillWidth: true
        placeholderText: "Search installed font names"
        accessibleLabel: "Search fonts"
        text: content.root.fontBrowserSearchText
        onTextEdited: {
            content.revealingInitialFamily = false;
            content.root.fontBrowserSearchText = text;
        }
        Keys.onDownPressed: {
            content.selectFamily(Math.max(0, carouselFontFamilyList.currentIndex));
            carouselFontFamilyList.forceActiveFocus(Qt.TabFocusReason);
        }
    }
    Flow {
        objectName: "carouselFontBrowserNavigation"
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: content.theme.space6
        Repeater {
            model: [
                { label: "All installed", value: "all" },
                { label: "Favorites", value: "favorites" },
                { label: "Recent", value: "recent" }
            ]
            delegate: FilterChip {
                required property var modelData
                objectName: "carouselFontBrowserSection_" + modelData.value
                text: modelData.label + "  " + (modelData.value === "all"
                      ? content.carouselDocument.installedFontFamilies.length
                      : modelData.value === "favorites" ? content.carouselDocument.favoriteFontFamilies.length
                      : content.carouselDocument.recentFontFamilies.length)
                active: content.root.fontBrowserSection === modelData.value
                onClicked: {
                    content.revealingInitialFamily = false;
                    content.root.fontBrowserSection = modelData.value;
                }
            }
        }
    }
    ListView {
        id: carouselFontFamilyList
        objectName: "carouselFontFamilyList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.min(content.theme.scaled(420), Math.max(content.theme.scaled(160), content.sectionFontCount * content.theme.scaled(100)))
        clip: true
        spacing: content.theme.space4
        model: content.families
        reuseItems: true
        activeFocusOnTab: true
        currentIndex: content.families.indexOf(content.chosenFamily)
        onCurrentItemChanged: content.scheduleInitialReveal()
        onContentHeightChanged: content.scheduleInitialReveal()
        onHeightChanged: content.scheduleInitialReveal()
        onMovementStarted: content.revealingInitialFamily = false
        Accessible.role: Accessible.List
        Accessible.name: "Installed font previews"
        Accessible.description: "Use arrow keys to select a family, then Use font or Preview fallback."
        Keys.onDownPressed: content.selectFamily(currentIndex + 1)
        Keys.onUpPressed: content.selectFamily(currentIndex < 0 ? 0 : currentIndex - 1)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                content.selectFamily(event.key === Qt.Key_Home ? 0 : count - 1);
                event.accepted = true;
            }
        }
        Keys.onReturnPressed: content.applySelection()
        Keys.onEnterPressed: content.applySelection()
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            active: true
            visible: carouselFontFamilyList.count > 0 && size < 1
        }
        delegate: Rectangle {
            id: fontFamilyRow
            required property string modelData
            required property int index
            objectName: "carouselFontFamilyRow_" + index
            readonly property bool selected: content.selectedFamily === modelData
            readonly property bool isCurrent: Boolean(content.selection.textFontAvailable)
                && content.currentFamily.toLowerCase() === modelData.toLowerCase()
            width: ListView.view ? ListView.view.width - content.theme.space8 : 0
            implicitHeight: fontRowContent.implicitHeight + content.theme.space12 * 2
            onHeightChanged: if (selected) content.scheduleInitialReveal()
            radius: content.theme.radiusControl
            color: selected ? content.theme.ink800 : fontRowHover.hovered ? content.theme.ink850 : "transparent"
            border.width: selected && carouselFontFamilyList.activeFocus ? content.theme.focusBorderWidth : 0
            border.color: content.theme.focusRing
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData
            Accessible.selectable: true
            Accessible.selected: selected
            Accessible.description: isCurrent ? "Current font" : "Font sample"
            Accessible.onPressAction: {
                content.selectFamily(index);
                carouselFontFamilyList.forceActiveFocus(Qt.TabFocusReason);
            }
            RowLayout {
                id: fontRowContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: content.theme.space12
                spacing: content.theme.space12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: content.theme.space6
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: fontFamilyRow.modelData
                            color: content.theme.textStrong
                            font.family: content.theme.fontUi
                            font.pixelSize: content.theme.typeControl
                            font.weight: Font.DemiBold
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                        Label {
                            visible: fontFamilyRow.isCurrent
                            text: "Current"
                            color: content.theme.textMuted
                            font.family: content.theme.fontUi
                            font.pixelSize: content.theme.typeCaption
                        }
                    }
                    Label {
                        objectName: "carouselFontSample_" + fontFamilyRow.index
                        Layout.fillWidth: true
                        text: content.sampleText
                        color: content.theme.textMain
                        font.family: fontFamilyRow.modelData
                        font.pixelSize: content.theme.scaled(20)
                        maximumLineCount: 2
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                }
                ActionButton {
                    objectName: "carouselFavoriteFont_" + fontFamilyRow.index
                    compact: true
                    showText: false
                    iconName: "star"
                    checkable: true
                    checked: content.root.fontFamilyInList(fontFamilyRow.modelData, content.carouselDocument.favoriteFontFamilies)
                    active: checked
                    tooltipText: (checked ? "Remove " : "Add ") + fontFamilyRow.modelData + (checked ? " from favorites" : " to favorites")
                    onClicked: {
                        content.revealingInitialFamily = false;
                        const removingVisibleRow = content.root.fontBrowserSection === "favorites"
                            && content.root.fontFamilyInList(fontFamilyRow.modelData, content.carouselDocument.favoriteFontFamilies);
                        const nextIndex = fontFamilyRow.index;
                        content.carouselDocument.toggleFavoriteFontFamily(fontFamilyRow.modelData);
                        if (removingVisibleRow) {
                            Qt.callLater(function() {
                                content.selectFamily(nextIndex);
                                carouselFontFamilyList.forceActiveFocus(Qt.TabFocusReason);
                            });
                        }
                    }
                }
            }
            HoverHandler {
                id: fontRowHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: {
                    content.selectFamily(fontFamilyRow.index);
                    carouselFontFamilyList.forceActiveFocus(Qt.MouseFocusReason);
                }
            }
        }
        Label {
            objectName: "carouselFontBrowserEmptyState"
            anchors.centerIn: parent
            width: Math.max(0, parent.width - content.theme.space20 * 2)
            visible: carouselFontFamilyList.count === 0
            text: content.root.fontBrowserSearchText.trim().length > 0 ? "No fonts match this search in this section."
                  : content.root.fontBrowserSection === "favorites" ? "No favorite fonts yet. Star an installed family."
                  : content.root.fontBrowserSection === "recent" ? "No recent fonts yet. Choose a font to add it here."
                  : "No installed fonts are available."
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    RowLayout {
        objectName: "carouselFontBrowserFooter"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            objectName: "carouselFontBrowserSelection"
            Layout.fillWidth: true
            text: content.selectedFamily.length > 0 ? content.selectedFamily : "Select a font"
            color: content.selectedFamily.length > 0 ? content.theme.textStrong : content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeBody
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        ActionButton {
            objectName: "carouselFontBrowserApplyButton"
            compact: true
            text: content.selection.textFontAvailable ? "Use font" : "Preview fallback"
            tone: "primary"
            enabled: content.hasTextSelection && content.selectedFamily.length > 0
            disabledReason: content.hasTextSelection ? "Select an installed family first." : "Select a text layer first."
            onClicked: content.applySelection()
        }
    }
}
