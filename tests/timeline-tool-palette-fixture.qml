import QtQuick
import QtQuick.Controls.Basic
import "../apps/desktop/qml/components"

Window {
    id: fixture
    width: 1240
    height: 170
    visible: true

    property string lastTool: ""
    property string lastCreatedKind: ""
    property var shortcutOverrides: ({})

    Theme { id: paletteTheme }

    TimelineToolPalette {
        id: palette
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        theme: paletteTheme
        shortcutProvider: function(command, fallback) {
            return fixture.shortcutOverrides[command] || fallback
        }
        vertical: width < 700
        hasActiveTimeline: true
        rateStretchEnabled: true
        envelopePenEnabled: true
        onToolRequested: tool => fixture.lastTool = tool
        onCreateRequested: kind => fixture.lastCreatedKind = kind
    }
}
