pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var boards
    required property int currentIndex
    property bool busy: false
    property bool focused: true
    property int selectedPhotoCount: 0
    property bool compact: width < 640
    property bool selecting: false
    signal boardRequested(int index)
    signal selectRequested()
    signal focusRequested()
    signal overviewRequested()
    signal layoutRequested(string preset)
    Theme { id: theme }
    implicitHeight: compact ? 98 : 108
    color: theme.ink900
    radius: theme.radius8
    border.width: theme.hairlineWidth
    border.color: theme.lineSoft
    function revealCurrent() {
        if (strip.dragging || strip.flicking || !visible || strip.width <= 0) return
        if (currentIndex >= 0 && currentIndex < (boards || []).length)
            strip.positionViewAtIndex(currentIndex, ListView.Contain)
    }
    onCurrentIndexChanged: Qt.callLater(revealCurrent)
    onBoardsChanged: Qt.callLater(revealCurrent)
    onVisibleChanged: if (visible) Qt.callLater(revealCurrent)
    onWidthChanged: Qt.callLater(revealCurrent)
    Component.onCompleted: Qt.callLater(revealCurrent)
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            MiniButton {
                objectName: "boardPreviousButton"
                iconName: "chevron-left"
                a11yName: qsTr("Previous board")
                tooltip: qsTr("Open the previous board")
                enabled: !root.busy && root.currentIndex > 0
                onClicked: root.boardRequested(root.currentIndex - 1)
            }
            Controls.Label {
                text: qsTr("%1 / %2").arg(root.currentIndex + 1).arg((root.boards || []).length)
                color: theme.textMain
                font.pixelSize: theme.scaled(12)
            }
            MiniButton {
                objectName: "boardNextButton"
                iconName: "chevron-right"
                a11yName: qsTr("Next board")
                tooltip: qsTr("Open the next board")
                enabled: !root.busy && root.currentIndex + 1 < (root.boards || []).length
                onClicked: root.boardRequested(root.currentIndex + 1)
            }
            Item { Layout.fillWidth: true }
            MiniButton {
                objectName: "boardSelectToolButton"
                text: qsTr("Select")
                iconName: "select"
                labelVisible: !root.compact
                square: root.compact
                active: root.selecting
                tooltip: qsTr("Click and drag photos. Shift-click to select two photos for a layout.")
                enabled: !root.busy
                onClicked: root.selectRequested()
            }
            MiniButton {
                objectName: "boardFitButton"
                text: qsTr("Fit board")
                iconName: "fullscreen"
                labelVisible: !root.compact
                square: root.compact
                active: root.focused
                enabled: !root.busy
                onClicked: root.focusRequested()
            }
            MiniButton {
                objectName: "boardOverviewButton"
                text: qsTr("Overview")
                iconName: "grid"
                labelVisible: !root.compact
                square: root.compact
                active: !root.focused
                enabled: !root.busy
                onClicked: root.overviewRequested()
            }
            MenuButton {
                objectName: "boardQuickLayoutButton"
                text: qsTr("Layouts")
                iconName: "grid"
                compact: true
                enabled: !root.busy
                primaryOpensMenu: true
                tooltipText: qsTr("Select one or two photo layers, then choose a layout.")
                menuItems: [
                    { key: "fit", label: qsTr("Full photo"), description: qsTr("Fit the selected photo without cropping."), enabled: root.selectedPhotoCount === 1, disabledReason: qsTr("Select one photo layer.") },
                    { key: "fill", label: qsTr("Fill board"), description: qsTr("Cover the board with the selected photo."), enabled: root.selectedPhotoCount === 1, disabledReason: qsTr("Select one photo layer.") },
                    { key: "stack", label: qsTr("Two stacked"), description: qsTr("Fit two photos in horizontal rows."), enabled: root.selectedPhotoCount === 2, disabledReason: qsTr("Shift-click two photo layers.") },
                    { key: "columns", label: qsTr("Side by side"), description: qsTr("Fit two photos in equal columns."), enabled: root.selectedPhotoCount === 2, disabledReason: qsTr("Shift-click two photo layers.") }
                ]
                onMenuItemTriggered: function(key) { root.layoutRequested(key) }
            }
        }
        ListView {
            id: strip
            objectName: "boardNavigationStrip"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.boards || []
            currentIndex: root.currentIndex
            onCountChanged: Qt.callLater(root.revealCurrent)
            WheelHandler {
                target: null
                onWheel: function(event) {
                    const pixel = event.pixelDelta
                    const delta = Math.abs(pixel.x) + Math.abs(pixel.y) > 0 ? pixel.x + pixel.y : event.angleDelta.x * 0.65 + event.angleDelta.y * 0.65
                    strip.contentX = Math.max(0, Math.min(Math.max(0, strip.contentWidth - strip.width), strip.contentX - delta))
                    event.accepted = true
                }
            }
            delegate: Controls.AbstractButton {
                id: thumb
                required property int index
                required property var modelData
                objectName: "boardNavigationThumb" + index
                width: root.compact ? 72 : 88
                height: strip.height
                enabled: !root.busy
                Accessible.name: qsTr("Open board %1: %2").arg(index + 1).arg(String(modelData.boardName || ""))
                background: Rectangle {
                    color: theme.ink900
                    radius: 3
                    border.width: thumb.index === root.currentIndex ? 2 : 1
                    border.color: thumb.index === root.currentIndex ? theme.primaryAccent : theme.lineSoft
                }
                contentItem: Image {
                    anchors.fill: parent
                    anchors.margins: 3
                    source: String(thumb.modelData.previewSource || "")
                    sourceSize.width: 176
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                Controls.Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    text: String(thumb.index + 1)
                    color: "white"
                    font.pixelSize: theme.scaled(10)
                    style: Text.Outline
                    styleColor: "#b0000000"
                }
                onClicked: root.boardRequested(index)
                Keys.onLeftPressed: if (index > 0) root.boardRequested(index - 1)
                Keys.onRightPressed: if (index + 1 < strip.count) root.boardRequested(index + 1)
            }
        }
    }
}
