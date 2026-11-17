pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    default property alias contentData: sourceFrame.data

    property real sourceAspectRatio: width / Math.max(1, height)
    property real cropLeft: 0
    property real cropTop: 0
    property real cropRight: 1
    property real cropBottom: 1
    property string frameMode: "fit"
    property string displayViewportObjectName: "timelineSourceCropDisplayViewport"
    property string sourceMatteSource: ""
    property string sourceMatteMode: "alpha"
    property bool sourceMatteInvert: false
    property color sourceMatteFillColor: "transparent"
    property bool sourceMatteFillEnabled: false

    readonly property real boundedSourceAspectRatio: Math.max(
        0.01, Number(sourceAspectRatio || width / Math.max(1, height)))
    readonly property real boundedCropLeft: Math.max(0, Math.min(1, cropLeft))
    readonly property real boundedCropTop: Math.max(0, Math.min(1, cropTop))
    readonly property real boundedCropRight: Math.max(
        boundedCropLeft + 0.01, Math.min(1, cropRight))
    readonly property real boundedCropBottom: Math.max(
        boundedCropTop + 0.01, Math.min(1, cropBottom))
    readonly property real cropWidth: boundedCropRight - boundedCropLeft
    readonly property real cropHeight: boundedCropBottom - boundedCropTop
    readonly property real cropAspectRatio: boundedSourceAspectRatio
                                             * cropWidth / cropHeight
    // Fit letterboxes the cropped source inside the target. Keep that fitted
    // viewport separate from the target so pixels outside the source crop can
    // never bleed into the letterbox margins.
    readonly property rect displayRect: {
        const targetWidth = Math.max(1, width)
        const targetHeight = Math.max(1, height)
        const mode = String(frameMode || "fit")
        if (mode !== "fit")
            return Qt.rect(0, 0, targetWidth, targetHeight)

        const targetAspect = targetWidth / targetHeight
        if (cropAspectRatio > targetAspect) {
            const displayHeight = targetWidth / cropAspectRatio
            return Qt.rect(0, (targetHeight - displayHeight) * 0.5,
                           targetWidth, displayHeight)
        }
        const displayWidth = targetHeight * cropAspectRatio
        return Qt.rect((targetWidth - displayWidth) * 0.5, 0,
                       displayWidth, targetHeight)
    }
    readonly property rect sourceFrameRect: {
        const mode = String(frameMode || "fit")
        let sourceLeft = boundedCropLeft
        let sourceTop = boundedCropTop
        let sourceWidth = cropWidth
        let sourceHeight = cropHeight

        if (mode === "fill") {
            const targetAspect = Math.max(1, width) / Math.max(1, height)
            if (cropAspectRatio > targetAspect) {
                sourceWidth = Math.max(
                    0.01, Math.min(cropWidth,
                                   cropHeight * targetAspect / boundedSourceAspectRatio))
                sourceLeft += (cropWidth - sourceWidth) * 0.5
            } else {
                sourceHeight = Math.max(
                    0.01, Math.min(cropHeight,
                                   cropWidth * boundedSourceAspectRatio / targetAspect))
                sourceTop += (cropHeight - sourceHeight) * 0.5
            }
        }

        const videoWidth = displayRect.width / sourceWidth
        const videoHeight = displayRect.height / sourceHeight
        return Qt.rect(-sourceLeft * videoWidth,
                       -sourceTop * videoHeight,
                       videoWidth, videoHeight)
    }

    clip: true

    Item {
        id: displayViewport
        objectName: root.displayViewportObjectName
        x: root.displayRect.x
        y: root.displayRect.y
        width: root.displayRect.width
        height: root.displayRect.height
        clip: true

        Item {
            id: sourceFrame
            x: root.sourceFrameRect.x
            y: root.sourceFrameRect.y
            width: root.sourceFrameRect.width
            height: root.sourceFrameRect.height
            layer.enabled: root.sourceMatteSource.length > 0
                           && sourceMatteImage.status === Image.Ready
            layer.effect: TimelineSourceMatteEffect {
                matteSource: sourceMatteImage
                matteEnabled: sourceMatteImage.status === Image.Ready ? 1 : 0
                matteMode: String(root.sourceMatteMode || "alpha") === "luminance" ? 1 : 0
                matteInvert: root.sourceMatteInvert ? 1 : 0
                matteFillEnabled: root.sourceMatteFillEnabled ? 1 : 0
                matteFillColor: root.sourceMatteFillColor
            }
        }

        Image {
            id: sourceMatteImage
            x: sourceFrame.x
            y: sourceFrame.y
            width: sourceFrame.width
            height: sourceFrame.height
            source: root.sourceMatteSource
            visible: true
            opacity: 0
            asynchronous: true
            cache: true
            fillMode: Image.Stretch
        }
    }
}
