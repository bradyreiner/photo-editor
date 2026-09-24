pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window

Item {
    id: pixelGridOverlay

    required property string viewportObjectName
    required property bool pixelGridVisible
    required property real inspectionPixelWidth
    required property real inspectionPixelHeight
    required property real sourcePixelScreenWidth
    required property real sourcePixelScreenHeight
    required property real paintedContentX
    required property real paintedContentY
    required property real paintedContentWidth
    required property real paintedContentHeight
    required property real pixelGridThreshold
    required property color gridLineColor

    objectName: pixelGridOverlay.viewportObjectName.length > 0
                ? pixelGridOverlay.viewportObjectName + "PixelGridOverlay"
                : "pixelGridOverlay"
    visible: pixelGridOverlay.pixelGridVisible
    z: 16.8

    readonly property int sourceWidth: Math.max(1, Math.round(pixelGridOverlay.inspectionPixelWidth))
    readonly property int sourceHeight: Math.max(1, Math.round(pixelGridOverlay.inspectionPixelHeight))
    readonly property real stepX: pixelGridOverlay.sourcePixelScreenWidth
    readonly property real stepY: pixelGridOverlay.sourcePixelScreenHeight
    readonly property real contentLeft: pixelGridOverlay.paintedContentX
    readonly property real contentTop: pixelGridOverlay.paintedContentY
    readonly property real visibleLeft: Math.max(0, contentLeft)
    readonly property real visibleTop: Math.max(0, contentTop)
    readonly property real visibleRight: Math.min(width, contentLeft + pixelGridOverlay.paintedContentWidth)
    readonly property real visibleBottom: Math.min(height, contentTop + pixelGridOverlay.paintedContentHeight)
    readonly property bool gridGeometryValid: visible &&
                                               stepX >= pixelGridOverlay.pixelGridThreshold &&
                                               stepY >= pixelGridOverlay.pixelGridThreshold &&
                                               visibleRight > visibleLeft &&
                                               visibleBottom > visibleTop
    readonly property int verticalStartIndex: gridGeometryValid
        ? Math.max(0, Math.floor((visibleLeft - contentLeft) / stepX))
        : 0
    readonly property int verticalEndIndex: gridGeometryValid
        ? Math.min(sourceWidth, Math.ceil((visibleRight - contentLeft) / stepX))
        : -1
    readonly property int horizontalStartIndex: gridGeometryValid
        ? Math.max(0, Math.floor((visibleTop - contentTop) / stepY))
        : 0
    readonly property int horizontalEndIndex: gridGeometryValid
        ? Math.min(sourceHeight, Math.ceil((visibleBottom - contentTop) / stepY))
        : -1

    Repeater {
        model: Math.max(0, pixelGridOverlay.verticalEndIndex -
                          pixelGridOverlay.verticalStartIndex + 1)

        delegate: Rectangle {
            required property int index
            readonly property int sourceIndex: pixelGridOverlay.verticalStartIndex + index
            x: pixelGridOverlay.contentLeft + sourceIndex * pixelGridOverlay.stepX
            y: pixelGridOverlay.visibleTop
            width: Math.max(0.5, 1.0 / Math.max(1.0, Number(Screen.devicePixelRatio || 1.0)))
            height: Math.max(0, pixelGridOverlay.visibleBottom - pixelGridOverlay.visibleTop)
            color: pixelGridOverlay.gridLineColor
            antialiasing: false
        }
    }

    Repeater {
        model: Math.max(0, pixelGridOverlay.horizontalEndIndex -
                          pixelGridOverlay.horizontalStartIndex + 1)

        delegate: Rectangle {
            required property int index
            readonly property int sourceIndex: pixelGridOverlay.horizontalStartIndex + index
            x: pixelGridOverlay.visibleLeft
            y: pixelGridOverlay.contentTop + sourceIndex * pixelGridOverlay.stepY
            width: Math.max(0, pixelGridOverlay.visibleRight - pixelGridOverlay.visibleLeft)
            height: Math.max(0.5, 1.0 / Math.max(1.0, Number(Screen.devicePixelRatio || 1.0)))
            color: pixelGridOverlay.gridLineColor
            antialiasing: false
        }
    }
}
