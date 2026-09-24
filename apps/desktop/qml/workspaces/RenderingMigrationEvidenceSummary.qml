import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "../components" as Components

Label {
    Components.Theme { id: theme }
    required property var evidence
    required property color readyColor
    required property color pendingColor
    readonly property bool ready: String(evidence.evidenceSha256 || "").length === 64

    Layout.fillWidth: true
    text: ready
          ? "Measured comparison · ΔE00 p95 " +
            Number(evidence.p95DeltaE00).toFixed(2) +
            " · max " + Number(evidence.maximumDeltaE00).toFixed(2) +
            " · candidate clips " + String(evidence.candidateShadowClipCount) +
            " shadow / " + String(evidence.candidateHighlightClipCount) +
            " highlight"
          : "Preparing settled perceptual and clipping evidence…"
    color: ready ? readyColor : pendingColor
    font.family: theme.fontMono
    font.pixelSize: 9
    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
}
