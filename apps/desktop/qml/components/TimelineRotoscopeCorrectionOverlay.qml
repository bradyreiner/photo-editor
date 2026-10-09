pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property rect sourceBounds: Qt.rect(0, 0, 0, 0)
    property var points: []
    property bool negativeMode: false
    property bool inputEnabled: true
    signal correctionPointsChanged(var points)

    function addPoint(x, y, negative) {
        const bounds = root.sourceBounds
        if (bounds.width <= 0 || bounds.height <= 0
                || x < bounds.x || x > bounds.x + bounds.width
                || y < bounds.y || y > bounds.y + bounds.height)
            return
        const next = (root.points || []).slice(0)
        next.push({
            "x": Math.max(0, Math.min(1, (x - bounds.x) / bounds.width)),
            "y": Math.max(0, Math.min(1, (y - bounds.y) / bounds.height)),
            "label": negative ? 0 : 1
        })
        root.correctionPointsChanged(next)
    }

    Repeater {
        model: root.points
        delegate: Rectangle {
            required property var modelData
            readonly property bool negative: Number(modelData.label) === 0
            x: root.sourceBounds.x + Number(modelData.x) * root.sourceBounds.width - width / 2
            y: root.sourceBounds.y + Number(modelData.y) * root.sourceBounds.height - height / 2
            width: 20
            height: 20
            radius: width / 2
            color: negative ? "#d84f53" : "#38bd83"
            border.width: 2
            border.color: "#f7f8f4"
            opacity: 0.92
            Text {
                anchors.centerIn: parent
                text: negative ? "−" : "+"
                color: "white"
                font.pixelSize: 15
                font.weight: Font.Bold
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.inputEnabled
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        onClicked: function(mouse) {
            const isNegative = root.negativeMode || (mouse.modifiers & Qt.AltModifier)
            root.addPoint(mouse.x, mouse.y, isNegative)
        }
    }
}
