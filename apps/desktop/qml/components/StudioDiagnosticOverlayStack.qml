import QtQuick
import QtQuick.Controls.Basic

Item {
    id: overlayStack

    required property real contentX
    required property real contentY
    required property real contentWidth
    required property real contentHeight
    required property real contentScale
    required property real anchorX
    required property real anchorY
    required property bool previewContentVisible
    required property bool highCostOverlaysVisible
    required property bool focusCheckEnabled
    required property bool focusPeakingEnabled
    required property bool clippingEnabled
    required property bool falseColorEnabled
    required property bool gamutEnabled
    required property bool zoneMapEnabled
    required property bool colorToolMaskEnabled
    required property bool compareMode
    required property bool softProofEnabled
    required property string softProofProfile
    required property string outputColorSpace
    required property color accentColor
    required property var focusCheckSource
    required property var focusPeakingSource
    required property var clippingSource
    required property var falseColorSource
    required property var gamutSource
    required property var zoneMapSource
    required property var colorToolMaskSource

    Image {
        id: focusCheckOverlay
        objectName: "focusCheckOverlay"
        x: overlayStack.contentX
        y: overlayStack.contentY
        width: overlayStack.contentWidth
        height: overlayStack.contentHeight
        scale: overlayStack.contentScale
        transformOrigin: Item.Center
        source: overlayStack.focusCheckSource
        fillMode: Image.Stretch
        asynchronous: true
        cache: false
        visible: overlayStack.focusCheckEnabled &&
                 overlayStack.highCostOverlaysVisible &&
                 overlayStack.previewContentVisible && source.toString().length > 0
        z: 11
    }

    Image {
        id: clippingOverlay
        objectName: "clippingOverlay"
        x: overlayStack.contentX
        y: overlayStack.contentY
        width: overlayStack.contentWidth
        height: overlayStack.contentHeight
        scale: overlayStack.contentScale
        transformOrigin: Item.Center
        source: overlayStack.clippingSource
        fillMode: Image.Stretch
        asynchronous: true
        cache: false
        visible: overlayStack.clippingEnabled &&
                 overlayStack.highCostOverlaysVisible &&
                 overlayStack.previewContentVisible && source.toString().length > 0
        z: 12
    }

    Label {
        id: clippingOverlayTargetLabel
        objectName: "clippingOverlayTargetLabel"
        x: overlayStack.anchorX + 10
        y: overlayStack.anchorY + 10
        visible: clippingOverlay.visible
        z: 17
        text: "Preview clipping  Blue shadows / Red highlights"
        color: "#f4f7fa"
        font.family: theme.fontMono
        font.pixelSize: 10
        padding: 5
        background: Rectangle {
            color: theme.alpha(theme.ink950, 0.82)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha("#ff6870", 0.45)
            radius: 3
        }
    }

    Image {
        id: focusPeakingOverlay
        objectName: "focusPeakingOverlay"
        x: overlayStack.contentX
        y: overlayStack.contentY
        width: overlayStack.contentWidth
        height: overlayStack.contentHeight
        scale: overlayStack.contentScale
        transformOrigin: Item.Center
        source: overlayStack.focusPeakingSource
        fillMode: Image.Stretch
        asynchronous: true
        cache: false
        visible: overlayStack.focusPeakingEnabled &&
                 overlayStack.highCostOverlaysVisible &&
                 overlayStack.previewContentVisible && source.toString().length > 0
        z: 13
    }

    Image {
        id: falseColorOverlay
        objectName: "falseColorOverlay"
        x: overlayStack.contentX
        y: overlayStack.contentY
        width: overlayStack.contentWidth
        height: overlayStack.contentHeight
        scale: overlayStack.contentScale
        transformOrigin: Item.Center
        source: overlayStack.falseColorSource
        fillMode: Image.Stretch
        asynchronous: true
        cache: false
        visible: overlayStack.falseColorEnabled &&
                 overlayStack.highCostOverlaysVisible &&
                 overlayStack.previewContentVisible && source.toString().length > 0
        z: 14
    }

    Image {
        id: gamutOverlay
        objectName: "gamutOverlay"
        x: overlayStack.contentX
        y: overlayStack.contentY
        width: overlayStack.contentWidth
        height: overlayStack.contentHeight
        scale: overlayStack.contentScale
        transformOrigin: Item.Center
        source: overlayStack.gamutSource
        fillMode: Image.Stretch
        asynchronous: true
        cache: false
        visible: overlayStack.gamutEnabled &&
                 overlayStack.highCostOverlaysVisible &&
                 overlayStack.previewContentVisible && source.toString().length > 0
        z: 15
    }

    Label {
        id: gamutOverlayTargetLabel
        objectName: "gamutOverlayTargetLabel"
        x: overlayStack.anchorX + 10
        y: clippingOverlayTargetLabel.visible
           ? clippingOverlayTargetLabel.y + clippingOverlayTargetLabel.height + 6
           : overlayStack.anchorY + 10
        visible: gamutOverlay.visible
        z: 17
        text: overlayStack.softProofEnabled
              ? "Proof gamut  " + overlayStack.softProofProfile
              : "Output gamut  " + overlayStack.outputColorSpace
        color: "#f4f7fa"
        font.family: theme.fontMono
        font.pixelSize: 10
        padding: 5
        background: Rectangle {
            color: theme.alpha(theme.ink950, 0.82)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha("#ff4870", 0.45)
            radius: 3
        }
    }

    Image {
        id: zoneMapOverlay
        objectName: "zoneMapOverlay"
        x: overlayStack.contentX
        y: overlayStack.contentY
        width: overlayStack.contentWidth
        height: overlayStack.contentHeight
        scale: overlayStack.contentScale
        transformOrigin: Item.Center
        source: overlayStack.zoneMapSource
        fillMode: Image.Stretch
        asynchronous: true
        cache: false
        visible: overlayStack.zoneMapEnabled &&
                 overlayStack.highCostOverlaysVisible &&
                 overlayStack.previewContentVisible && source.toString().length > 0
        z: 16
    }

    Image {
        id: colorToolMaskPreviewOverlay
        objectName: "colorToolMaskPreviewOverlay"
        x: overlayStack.contentX
        y: overlayStack.contentY
        width: overlayStack.contentWidth
        height: overlayStack.contentHeight
        scale: overlayStack.contentScale
        transformOrigin: Item.Center
        source: overlayStack.colorToolMaskSource
        fillMode: Image.Stretch
        asynchronous: true
        cache: false
        visible: overlayStack.colorToolMaskEnabled &&
                 overlayStack.previewContentVisible && source.toString().length > 0 &&
                 !overlayStack.compareMode
        z: 16.5
    }

    Label {
        objectName: "colorToolMaskPreviewOverlayLabel"
        x: overlayStack.anchorX + 10
        y: gamutOverlayTargetLabel.visible
           ? gamutOverlayTargetLabel.y + gamutOverlayTargetLabel.height + 6
           : overlayStack.anchorY + 10
        visible: colorToolMaskPreviewOverlay.visible
        z: 17
        text: "Color tool mask"
        color: "#f4f7fa"
        font.family: theme.fontMono
        font.pixelSize: 10
        padding: 5
        background: Rectangle {
            color: theme.alpha(theme.ink950, 0.82)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(overlayStack.accentColor, 0.45)
            radius: 3
        }
    }

    Theme { id: theme }
}
