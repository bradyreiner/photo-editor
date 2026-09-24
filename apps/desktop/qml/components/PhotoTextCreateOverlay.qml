pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: root

    Theme { id: theme }
    property bool active: false
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property int canvasWidth: 0
    property int canvasHeight: 0
    property point startPoint: Qt.point(0, 0)
    property point endPoint: Qt.point(0, 0)
    property bool dragging: false
    signal boxRequested(int x, int y, int width, int height)
    signal pointRequested(int x, int y)
    visible: active
    enabled: active && imageWidth > 0 && imageHeight > 0 && canvasWidth > 0 && canvasHeight > 0
    onActiveChanged: dragging = false

    function documentPoint(x, y) {
        return Qt.point(Math.max(0, Math.min(canvasWidth, (x - imageX) * canvasWidth / imageWidth)),
                        Math.max(0, Math.min(canvasHeight, (y - imageY) * canvasHeight / imageHeight)))
    }
    function finishBox() {
        if (!dragging) return
        dragging = false
        const dx = Math.abs(endPoint.x - startPoint.x)
        const dy = Math.abs(endPoint.y - startPoint.y)
        const click = dx * imageWidth / canvasWidth < 4 && dy * imageHeight / canvasHeight < 4
        if (click) {
            pointRequested(Math.round(startPoint.x), Math.round(startPoint.y))
            return
        }
        boxRequested(Math.round(Math.min(startPoint.x, endPoint.x)),
                     Math.round(Math.min(startPoint.y, endPoint.y)),
                     Math.max(16, Math.min(8192, Math.round(dx))),
                     Math.max(16, Math.min(8192, Math.round(dy))))
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.IBeamCursor
        onPressed: function(mouse) {
            if (mouse.x < root.imageX || mouse.y < root.imageY ||
                    mouse.x > root.imageX + root.imageWidth || mouse.y > root.imageY + root.imageHeight) return
            root.startPoint = root.documentPoint(mouse.x, mouse.y)
            root.endPoint = root.startPoint
            root.dragging = true
        }
        onPositionChanged: function(mouse) {
            if (root.dragging) root.endPoint = root.documentPoint(mouse.x, mouse.y)
        }
        onReleased: function(mouse) {
            if (root.dragging) root.endPoint = root.documentPoint(mouse.x, mouse.y)
            root.finishBox()
        }
        onCanceled: root.dragging = false
    }
    Rectangle {
        visible: root.dragging
        x: root.imageX + Math.min(root.startPoint.x, root.endPoint.x) * root.imageWidth / root.canvasWidth
        y: root.imageY + Math.min(root.startPoint.y, root.endPoint.y) * root.imageHeight / root.canvasHeight
        width: Math.abs(root.endPoint.x - root.startPoint.x) * root.imageWidth / root.canvasWidth
        height: Math.abs(root.endPoint.y - root.startPoint.y) * root.imageHeight / root.canvasHeight
        color: theme.alpha(theme.primaryAccent, 0.12)
        border.color: theme.primaryAccent
        border.width: 1
    }
    Label {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        padding: 8
        text: qsTr("Click for point text, or drag a paragraph box. Escape cancels.")
        color: "white"
        background: Rectangle { color: "#e0202428"; radius: 4 }
    }
}
