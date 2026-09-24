import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root

    property string compactSource: ""
    property string standardSource: ""
    property string detailSource: ""
    property string fallbackSource: ""
    property string clipLabel: "Clip"
    property string generationState: "missing"
    property real sourceDurationSeconds: 0
    property real trimStartSeconds: 0
    property real trimEndSeconds: sourceDurationSeconds
    property bool viewportActive: true
    property real waveformOpacity: 0.42
    readonly property string timelineWaveformContract: "timeline-waveform:v2|cache:media-revision-multiresolution-bounded|tiers:600-2400-9600|zoom:adaptive|trim:source-cropped|generation:async-serial|viewport:lazy|background:transparent|fallback:progressive"
    readonly property real sourceStartFraction: sourceDurationSeconds > 0
                                                       ? Math.max(0, Math.min(1, trimStartSeconds / sourceDurationSeconds))
                                                       : 0
    readonly property real sourceSpanFraction: sourceDurationSeconds > 0
                                                      ? Math.max(0.0001,
                                                                 Math.min(1 - sourceStartFraction,
                                                                          (trimEndSeconds - trimStartSeconds)
                                                                          / sourceDurationSeconds))
                                                      : 1
    readonly property real requiredFullSourcePixels: Math.max(1, width) / sourceSpanFraction
    readonly property string preferredTier: requiredFullSourcePixels <= 600 ? "compact"
                                                   : requiredFullSourcePixels <= 2400 ? "standard"
                                                                                     : "detail"
    readonly property string resolvedSource: {
        if (preferredTier === "compact" && compactSource.length > 0)
            return compactSource
        if (preferredTier !== "detail" && standardSource.length > 0)
            return standardSource
        if (detailSource.length > 0)
            return detailSource
        if (standardSource.length > 0)
            return standardSource
        if (compactSource.length > 0)
            return compactSource
        return fallbackSource
    }
    readonly property real resolvedSourcePixelWidth: resolvedSource === detailSource && detailSource.length > 0
                                                               ? 9600
                                                               : resolvedSource === compactSource && compactSource.length > 0
                                                                 ? 600
                                                                 : 2400
    readonly property string resolvedTier: resolvedSource === detailSource && detailSource.length > 0
                                                       ? "detail"
                                                       : resolvedSource === compactSource && compactSource.length > 0
                                                         ? "compact"
                                                         : "standard"

    clip: true
    Accessible.role: Accessible.Graphic
    Accessible.name: "Audio waveform"
    Accessible.description: resolvedSource.length > 0
                            ? "Source-accurate " + resolvedTier + " waveform detail"
                            : generationState === "generating"
                              ? "Waveform is being generated in the background"
                              : "Waveform unavailable"

    Image {
        id: waveformImage
        objectName: "timelineWaveformImage-" + root.clipLabel
        anchors.fill: parent
        source: root.viewportActive ? root.resolvedSource : ""
        sourceClipRect: root.sourceStartFraction <= 0.0001
                        && root.sourceSpanFraction >= 0.9999
                        ? Qt.rect(0, 0, 0, 0)
                        : Qt.rect(root.sourceStartFraction * root.resolvedSourcePixelWidth,
                                  0,
                                  Math.max(1, root.sourceSpanFraction * root.resolvedSourcePixelWidth),
                                  160)
        fillMode: Image.Stretch
        asynchronous: true
        cache: true
        smooth: true
        mipmap: root.resolvedTier !== "detail"
        opacity: root.waveformOpacity
        visible: String(source).length > 0
        z: 1
    }

    Item {
        anchors.fill: parent
        visible: root.viewportActive
                 && (root.generationState === "generating"
                     || (root.generationState === "ready"
                         && waveformImage.status !== Image.Ready))
        opacity: root.generationState === "ready" ? 0.52 : 0.34
        z: 1

        Row {
            id: waveformPlaceholderRow
            anchors.centerIn: parent
            width: Math.min(parent.width, 180)
            height: parent.height * 0.54
            spacing: 3

            Repeater {
                model: Math.max(3, Math.floor(waveformPlaceholderRow.width / 7))

                Rectangle {
                    required property int index
                    width: 3
                    height: Math.max(3,
                                     waveformPlaceholderRow.height
                                     * (0.22 + ((index * 7) % 9) / 12))
                    y: (waveformPlaceholderRow.height - height) / 2
                    radius: 1.5
                    color: "#dce5ec"
                }
            }
        }
    }

    HoverHandler { id: waveformHover }

    ToolTip.visible: waveformHover.hovered && generationState === "generating"
    ToolTip.text: "Building cached waveform detail in the background"
}
