import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root

    required property var previewController

    spacing: 6
    visible: root.previewController.retouchCount > 0

    ActionButton {
        Layout.fillWidth: false
        text: "Before"
        iconName: "compare"
        showText: false
        tooltipText: "Show retouch before"
        Accessible.name: "Show retouch before"
        compact: true
        accent: "#8fb7d8"
        tone: active ? "primary" : "secondary"
        active: root.previewController.retouchBeforeAfterEnabled
        onClicked: root.previewController.retouchBeforeAfterEnabled = true
    }

    ActionButton {
        Layout.fillWidth: false
        text: "After"
        iconName: "compare"
        showText: false
        tooltipText: "Show retouch after"
        Accessible.name: "Show retouch after"
        compact: true
        accent: "#8fb7d8"
        tone: active ? "primary" : "secondary"
        active: !root.previewController.retouchBeforeAfterEnabled
        onClicked: root.previewController.retouchBeforeAfterEnabled = false
    }
}
