import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    property string title: qsTr("Tools")
    property string targetText: ""
    property bool active: false
    property bool floating: false
    readonly property alias contentItem: toolContent
    readonly property real preferredContentHeight: toolContent.implicitHeight + theme.scaled(90)
    signal floatingRequested(bool floating)
    signal closeRequested()
    signal undoRequested()
    signal redoRequested()

    function restoreWindowFocus() {
        Qt.callLater(function() {
            const window = root.active && root.floating ? toolWindow : root.Window.window
            if (window && window.visible) {
                // Reparenting a focused item between windows can clear the
                // destination window's root focus scope, even while it is active.
                window.contentItem.focus = true
                window.requestActivate()
            }
        })
    }
    onFloatingChanged: restoreWindowFocus()
    onActiveChanged: restoreWindowFocus()

    Theme { id: theme }
    visible: active && !floating
    implicitWidth: theme.scaled(320)
    implicitHeight: theme.scaled(440)
    Window {
        id: toolWindow
        objectName: "creativeToolsWindow"
        title: root.title + qsTr(" · Looksy")
        transientParent: root.Window.window
        flags: Qt.Tool | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
        width: theme.scaled(390)
        height: theme.scaled(640)
        minimumWidth: theme.scaled(300)
        minimumHeight: theme.scaled(260)
        color: theme.surfaceTop
        visible: root.active && root.floating
        onClosing: function(close) {
            close.accepted = false
            root.closeRequested()
        }
        Shortcut {
            sequence: "Ctrl+Z"
            context: Qt.WindowShortcut
            enabled: toolWindow.visible
            onActivated: root.undoRequested()
        }
        Shortcut {
            sequence: "Ctrl+Shift+Z"
            context: Qt.WindowShortcut
            enabled: toolWindow.visible
            onActivated: root.redoRequested()
        }
    }

    Rectangle {
        id: surface
        objectName: "creativeToolsSurface"
        parent: root.floating ? toolWindow.contentItem : root
        anchors.fill: parent
        color: theme.surfaceTop
        border.width: theme.hairlineWidth
        border.color: theme.controlEdge
        radius: theme.radiusPanel
        clip: true

        ColumnLayout {
            id: frameLayout
            anchors.fill: parent
            anchors.margins: theme.space12
            spacing: theme.space8

            RowLayout {
                id: headerRow
                Layout.fillWidth: true
                spacing: theme.space4
                Label {
                    objectName: "creativeToolsTitle"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.title
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeSection
                    font.weight: Font.DemiBold
                    color: theme.textStrong
                }
                MiniButton {
                    objectName: "creativeToolsFloatButton"
                    square: true
                    iconName: root.floating ? "panel-right" : "fullscreen"
                    role: "icon"
                    tooltip: root.floating ? qsTr("Dock tools in the right sidebar") : qsTr("Float tools in a separate window")
                    a11yName: tooltip
                    onClicked: root.floatingRequested(!root.floating)
                }
                MiniButton {
                    objectName: "creativeToolsCloseButton"
                    square: true
                    iconName: "close"
                    role: "icon"
                    tooltip: qsTr("Hide tools. Reopen from the Layout menu.")
                    a11yName: tooltip
                    onClicked: root.closeRequested()
                }
            }
            Label {
                objectName: "creativeToolsTargetLabel"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: text.length > 0
                text: root.targetText
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                elide: Text.ElideRight
            }
            StudioRowRule { Layout.fillWidth: true }
            ScrollView {
                id: toolScroll
                objectName: "creativeToolsScroll"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    id: toolContent
                    objectName: "creativeToolsContent"
                    width: toolScroll.availableWidth
                    spacing: theme.space12
                }
            }
        }
    }
}
