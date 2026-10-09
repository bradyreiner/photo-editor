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
    required property var carouselReadingOrderDialog
    property alias readingOrderList: orderList
    property alias closeButton: closeControl
    property string selectedKey: ""
    readonly property var state: carouselDocument.readingOrderState
    readonly property var entries: state.entries || []
    readonly property var selectedEntry: {
        for (let index = 0; index < entries.length; ++index)
            if (String(entries[index].key) === selectedKey) return entries[index];
        return ({});
    }
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function choose(index, focusList) {
        if (!entries.length) {
            selectedKey = "";
            orderList.currentIndex = -1;
            return;
        }
        const row = Math.max(0, Math.min(index, entries.length - 1));
        selectedKey = String(entries[row].key);
        orderList.currentIndex = row;
        orderList.positionViewAtIndex(row, ListView.Contain);
        if (focusList) orderList.forceActiveFocus(Qt.TabFocusReason);
    }
    function restoreSelection() {
        for (let index = 0; index < entries.length; ++index) {
            if (String(entries[index].key) === selectedKey) {
                choose(index, false);
                return;
            }
        }
        choose(Math.max(0, orderList.currentIndex), false);
    }
    onEntriesChanged: Qt.callLater(restoreSelection)
    function focusInitialTarget() {
        for (let index = 0; index < entries.length; ++index) {
            if (String(entries[index].itemId || "") === String(carouselDocument.selectedItemId) && carouselDocument.selectedItemId.length) {
                choose(index, true);
                return;
            }
        }
        restoreSelection();
        if (entries.length) orderList.forceActiveFocus(Qt.TabFocusReason);
        else closeControl.forceActiveFocus(Qt.TabFocusReason);
    }

    RowLayout {
        objectName: "carouselReadingOrderHeader"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            Layout.fillWidth: true
            text: "Reading order"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
        }
        ActionButton {
            id: closeControl
            objectName: "carouselReadingOrderCloseButton"
            text: "Close"
            iconName: "close"
            compact: true
            onClicked: content.carouselReadingOrderDialog.close()
        }
    }
    ColumnLayout {
        objectName: "carouselReadingOrderContext"
        Layout.fillWidth: true
        spacing: content.theme.space6
        Label {
            Layout.fillWidth: true
            text: "Board " + Number(content.state.boardNumber || 1) +
                  (String(content.state.boardName || "").length ? " · " + content.state.boardName : "") +
                  " · " + Number(content.state.meaningfulEntryCount || 0) + " included of " + content.entries.length + " elements"
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        Label {
            Layout.fillWidth: true
            text: "Order visible content for delivery. Decorative media stays in the artwork and is excluded from delivery order."
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    ListView {
        id: orderList
        objectName: "carouselReadingOrderList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: content.theme.scaled(Math.min(360, Math.max(120, content.entries.length * 76)))
        spacing: content.theme.space4
        clip: true
        reuseItems: true
        activeFocusOnTab: true
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        model: content.entries
        Accessible.name: "Ordered readable elements"
        Accessible.role: Accessible.List
        onHeightChanged: Qt.callLater(content.restoreSelection)
        Keys.onDownPressed: content.choose(currentIndex + 1, true)
        Keys.onUpPressed: content.choose(currentIndex - 1, true)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                content.choose(event.key === Qt.Key_Home ? 0 : count - 1, true);
                event.accepted = true;
            }
        }
        ScrollBar.vertical: ScrollBar {
            objectName: "carouselReadingOrderScrollBar"
            policy: ScrollBar.AsNeeded
            active: true
            visible: orderList.count > 0 && orderList.contentHeight > orderList.height
        }
        delegate: Rectangle {
            id: entry
            required property var modelData
            required property int index
            objectName: "carouselReadingOrderRow_" + index
            width: orderList.width - content.theme.space8
            height: Math.max(content.theme.scaled(56), entryLabels.implicitHeight) + content.theme.space8 * 2
            radius: content.theme.radiusControl
            color: content.selectedKey === String(modelData.key) ? content.theme.alpha(content.theme.focusFill, 0.14) : "transparent"
            border.width: content.selectedKey === String(modelData.key) ? content.theme.focusBorderWidth : 0
            border.color: orderList.activeFocus ? content.theme.focusRing : content.theme.lineSoft
            Accessible.role: Accessible.ListItem
            Accessible.name: "Position " + modelData.position + ", " + modelData.kind + ", " + modelData.label + (modelData.meaningful ? ", included" : ", decorative")
            Accessible.selectable: true
            Accessible.selected: content.selectedKey === String(modelData.key)
            Accessible.onPressAction: content.choose(index, true)
            TapHandler { onTapped: content.choose(entry.index, true) }
            RowLayout {
                anchors.fill: parent
                anchors.margins: content.theme.space8
                spacing: content.theme.space12
                Label {
                    Layout.preferredWidth: content.theme.scaled(32)
                    Layout.alignment: Qt.AlignTop
                    text: String(entry.modelData.position)
                    color: content.theme.textMuted
                    font.family: content.theme.fontMono
                    font.pixelSize: content.theme.typeControl
                }
                ColumnLayout {
                    id: entryLabels
                    Layout.fillWidth: true
                    spacing: content.theme.space4
                    Label {
                        objectName: "carouselReadingOrderLabel_" + entry.index
                        Layout.fillWidth: true
                        text: String(entry.modelData.label || "Readable element")
                        color: entry.modelData.meaningful ? content.theme.textMain : content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeControl
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                    Label {
                        Layout.fillWidth: true
                        text: String(entry.modelData.kind) + " · " +
                              (entry.modelData.sourceType === "slot" ? String(entry.modelData.slot) + " slot" : "Canvas layer") +
                              (!entry.modelData.meaningful ? " · Decorative" : "")
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                }
            }
        }
        Label {
            anchors.centerIn: parent
            width: parent.width - content.theme.space16
            visible: orderList.count === 0
            text: "Add visible text or imagery to this board to set its reading order."
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    ColumnLayout {
        objectName: "carouselReadingOrderFooter"
        Layout.fillWidth: true
        visible: content.entries.length > 0
        spacing: content.theme.space8
        ToneCheckBox {
            objectName: "carouselReadingOrderMeaningfulToggle"
            Layout.fillWidth: true
            visible: Boolean(content.selectedEntry.canToggleMeaningful)
            allowLabelWrap: true
            text: "Include selected imagery in delivery order"
            checked: Boolean(content.selectedEntry.meaningful)
            onClicked: content.carouselDocument.setReadingOrderEntryMeaningful(content.selectedKey, checked)
        }
        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: content.theme.space8
            ActionButton {
                objectName: "carouselReadingOrderMoveEarlierButton"
                text: "Move earlier"
                compact: true
                enabled: Number(content.selectedEntry.position || 0) > 1
                disabledReason: "This is the first element in reading order."
                onClicked: content.carouselDocument.moveReadingOrderEntry(content.selectedKey, -1)
            }
            ActionButton {
                objectName: "carouselReadingOrderMoveLaterButton"
                text: "Move later"
                compact: true
                enabled: Number(content.selectedEntry.position || 0) > 0 && Number(content.selectedEntry.position) < content.entries.length
                disabledReason: "This is the last element in reading order."
                onClicked: content.carouselDocument.moveReadingOrderEntry(content.selectedKey, 1)
            }
            ActionButton {
                objectName: "carouselReadingOrderFocusButton"
                text: "Show on canvas"
                tone: "primary"
                compact: true
                enabled: content.selectedKey.length > 0
                onClicked: {
                    if (content.carouselDocument.focusReadingOrderEntry(content.selectedKey)) {
                        content.carouselReadingOrderDialog.focusCanvasOnClose = true;
                        content.carouselReadingOrderDialog.close();
                    }
                }
            }
        }
    }
}
