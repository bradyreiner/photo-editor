pragma ComponentBehavior: Bound
import QtQuick
import PhotoEditor

Rectangle {
    id: loupeView

    required property var previewController
    required property bool loupeVisible
    required property real loupeX
    required property real loupeY
    required property real loupeZoom
    required property real loupeSize
    required property real viewportWidth
    required property real viewportHeight
    required property real viewportZoom
    required property real viewportPanX
    required property real viewportPanY
    required property string viewportObjectName
    required property var previewSource
    required property bool nativePreviewActive
    required property bool pixelInspectionActive
    required property real previewAnchorX
    required property real previewAnchorY
    required property real previewAnchorWidth
    required property real previewAnchorHeight
    required property int previewFillMode
    required property real baseContentX
    required property real baseContentY
    required property real baseContentWidth
    required property real baseContentHeight
    required property color accentColor

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    objectName: "loupeView"
    visible: loupeView.loupeVisible
    x: loupeView.clamp(loupeView.loupeX - width / 2,
                       6,
                       Math.max(6, loupeView.viewportWidth - width - 6))
    y: loupeView.clamp(loupeView.loupeY - height / 2,
                       6,
                       Math.max(6, loupeView.viewportHeight - height - 6))
    width: Math.min(loupeView.loupeSize, Math.max(96, loupeView.viewportWidth - 12))
    height: width
    radius: width / 2
    color: theme.mediaCanvas
    border.width: 2
    border.color: theme.alpha(loupeView.accentColor, 0.88)
    clip: true
    z: 12

    Image {
        width: loupeView.previewAnchorWidth * loupeView.loupeZoom
        height: loupeView.previewAnchorHeight * loupeView.loupeZoom
        x: loupeView.width / 2 -
           (loupeView.loupeX - loupeView.previewAnchorX) * loupeView.loupeZoom
        y: loupeView.height / 2 -
           (loupeView.loupeY - loupeView.previewAnchorY) * loupeView.loupeZoom
        source: visible ? loupeView.previewSource : ""
        fillMode: loupeView.previewFillMode
        asynchronous: true
        cache: false
        visible: !loupeView.nativePreviewActive
        smooth: !loupeView.pixelInspectionActive
        mipmap: false
        scale: loupeView.viewportZoom
        transform: Translate {
            x: loupeView.viewportPanX * loupeView.loupeZoom
            y: loupeView.viewportPanY * loupeView.loupeZoom
        }
    }

    EditorPreviewTextureItem {
        objectName: loupeView.viewportObjectName.length > 0
                    ? loupeView.viewportObjectName + "LoupeNativePreviewImage"
                    : "loupeNativePreviewImage"
        width: loupeView.baseContentWidth * loupeView.loupeZoom
        height: loupeView.baseContentHeight * loupeView.loupeZoom
        x: loupeView.width / 2 -
           (loupeView.loupeX -
            (loupeView.baseContentX + loupeView.viewportPanX)) * loupeView.loupeZoom
        y: loupeView.height / 2 -
           (loupeView.loupeY -
            (loupeView.baseContentY + loupeView.viewportPanY)) * loupeView.loupeZoom
        scale: loupeView.viewportZoom
        transformOrigin: Item.Center
        visible: loupeView.nativePreviewActive
        controller: loupeView.previewController
        crispPixels: loupeView.pixelInspectionActive
    }

    Rectangle {
        anchors.centerIn: parent
        width: 16
        height: 1
        color: theme.alpha(theme.polar, 0.78)
    }

    Rectangle {
        anchors.centerIn: parent
        width: 1
        height: 16
        color: theme.alpha(theme.polar, 0.78)
    }

    Theme { id: theme }
}
