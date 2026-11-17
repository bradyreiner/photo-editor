import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    property string title: qsTr("Tools")
    property string targetText: ""
    property string contextKey: ""
    property bool active: false
    property bool floating: false
    property bool footerVisible: false
    property bool contentSized: false
    readonly property alias contentItem: toolContent
    readonly property alias footerContentItem: toolFooter
    readonly property real preferredContentHeight: toolContent.implicitHeight + headerRow.implicitHeight +
        (targetLabel.visible ? targetLabel.implicitHeight + frameLayout.spacing : 0) +
        theme.hairlineWidth + frameLayout.spacing * 2 + theme.panelPadding * 2 +
        (toolFooter.visible ? toolFooter.implicitHeight + frameLayout.spacing : 0)
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
    onActiveChanged: {
        restoreWindowFocus()
        if (active) resetScroll()
    }
    function resetScroll() {
        Qt.callLater(function() {
            if (toolScroll.contentItem) toolScroll.contentItem.contentY = 0
        })
    }
    onContextKeyChanged: resetScroll()

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
        color: theme.ink950
        border.width: theme.highContrast ? theme.hairlineWidth : 0
        border.color: theme.controlEdge
        radius: root.floating ? theme.radiusPanel : 0
        clip: true

        Rectangle {
            x: frameLayout.x + headerRow.x - theme.space4
            y: frameLayout.y + headerRow.y - theme.space4
            width: headerRow.width + theme.space8
            height: headerRow.height + theme.space8
            radius: theme.radiusControl
            color: theme.ink800
        }

        ColumnLayout {
            id: frameLayout
            anchors.fill: parent
            anchors.margins: theme.panelPadding
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
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    Accessible.name: root.title
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
                    tooltip: qsTr("Hide tools. Reopen from the View menu.")
                    a11yName: tooltip
                    onClicked: root.closeRequested()
                }
            }
            Label {
                id: targetLabel
                objectName: "creativeToolsTargetLabel"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: text.length > 0
                text: root.targetText
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
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
                wheelEnabled: !root.contentSized
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: root.contentSized ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
                Binding {
                    target: toolScroll.contentItem
                    property: "interactive"
                    value: !root.contentSized
                    when: toolScroll.contentItem !== null
                }
                ColumnLayout {
                    id: toolContent
                    objectName: "creativeToolsContent"
                    width: toolScroll.availableWidth
                    spacing: theme.space12
                }
            }
            RowLayout {
                id: toolFooter
                objectName: "creativeToolsFooter"
                Layout.fillWidth: true
                spacing: theme.space6
                visible: root.footerVisible
            }
        }
    }
}
