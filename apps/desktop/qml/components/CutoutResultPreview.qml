import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    required property var controller
    required property var workspace
    required property var themeContext
    property string background: "checker"
    color: root.themeContext.surfaceTop
    clip: true
    focus: true
    Component.onCompleted: forceActiveFocus(Qt.OtherFocusReason)
    Keys.onShortcutOverride: function(event) { if (event.key === Qt.Key_Escape) event.accepted = true }
    Keys.onEscapePressed: function(event) { root.controller.dismissCutoutPreview(); event.accepted = true }

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.themeContext.space12
        spacing: root.themeContext.space8
        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= root.themeContext.scaled(620) ? 2 : 1
            Text {
                text: qsTr("Cutout preview")
                color: root.themeContext.textMain
                font.family: root.themeContext.fontUi
                font.pixelSize: root.themeContext.typeBody
                Layout.fillWidth: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: root.themeContext.space4
                MenuButton {
                    objectName: "cutoutPreviewBackgroundMenu"
                    text: qsTr("Background")
                    compact: true
                    primaryOpensMenu: true
                    menuItems: [
                        {key: "checker", label: qsTr("Transparency"), checked: root.background === "checker"},
                        {key: "light", label: qsTr("Light"), checked: root.background === "light"},
                        {key: "dark", label: qsTr("Dark"), checked: root.background === "dark"}
                    ]
                    onMenuItemTriggered: function(key) { root.background = key }
                }
                MiniButton {
                    objectName: "cutoutPreviewExportButton"
                    text: qsTr("Export PNG")
                    enabled: !root.controller.photoPaintBusy && !root.controller.photoExportBusy
                    onClicked: root.workspace.chooseCutoutExport(true)
                }
                MiniButton {
                    objectName: "cutoutPreviewReturnButton"
                    text: qsTr("Full composition")
                    role: "primary"
                    onClicked: root.controller.dismissCutoutPreview()
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Canvas {
                id: backdrop
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Connections { target: root; function onBackgroundChanged() { backdrop.requestPaint() } }
                onPaint: {
                    const context = getContext("2d")
                    context.fillStyle = root.background === "light" ? "#f3f3f3" : "#242424"
                    context.fillRect(0, 0, width, height)
                    if (root.background === "checker") {
                        context.fillStyle = "#343434"
                        const tile = root.themeContext.scaled(10)
                        for (let y = 0; y < height; y += tile)
                            for (let x = 0; x < width; x += tile)
                                if ((Math.floor(x / tile) + Math.floor(y / tile)) % 2) context.fillRect(x, y, tile, tile)
                    }
                }
            }
            ColorManagedImage {
                objectName: "cutoutPreviewImage"
                anchors.fill: parent
                imageSource: String(root.controller.cutoutPreview.source || "")
                displayController: root.controller
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
            }
        }
    }
}
