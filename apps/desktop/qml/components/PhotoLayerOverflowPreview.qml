pragma ComponentBehavior: Bound

import QtQuick

// A positioning aid only. The regular compositor still owns canvas pixels.
Item {
    id: root
    property url imageSource
    property rect canvasRect
    property rect photoRect
    property real photoRotation: 0
    property real photoOpacity: 1
    readonly property real leftEdge: Math.max(0, Math.min(width, canvasRect.x))
    readonly property real rightEdge: Math.max(0, Math.min(width, canvasRect.x + canvasRect.width))
    readonly property real topEdge: Math.max(0, Math.min(height, canvasRect.y))
    readonly property real bottomEdge: Math.max(0, Math.min(height, canvasRect.y + canvasRect.height))
    readonly property var regions: [Qt.rect(0, 0, width, topEdge),
        Qt.rect(0, bottomEdge, width, height - bottomEdge),
        Qt.rect(0, topEdge, leftEdge, bottomEdge - topEdge),
        Qt.rect(rightEdge, topEdge, width - rightEdge, bottomEdge - topEdge)]
    visible: String(imageSource).length > 0 && photoRect.width > 0 && photoRect.height > 0
    enabled: false

    Repeater {
        model: root.regions
        delegate: Item {
            id: region
            required property rect modelData
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            clip: true
            visible: width > 0 && height > 0
            Image {
                objectName: "photoLayerOutsideCanvasImage"
                x: root.photoRect.x - region.x
                y: root.photoRect.y - region.y
                width: root.photoRect.width
                height: root.photoRect.height
                rotation: root.photoRotation
                opacity: 0.35 * root.photoOpacity
                source: root.visible ? root.imageSource : ""
                asynchronous: true
                fillMode: Image.Stretch
            }
        }
    }
}
