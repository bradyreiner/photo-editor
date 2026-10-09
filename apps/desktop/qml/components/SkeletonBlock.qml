import QtQuick

Rectangle {
    id: root

    property int phaseDelay: 0
    property bool animate: visible && !theme.reducedMotion
    property color fillColor: theme.surfaceTop

    Theme { id: theme }

    radius: theme.radius6
    color: fillColor
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.24)
    clip: true

    Rectangle {
        id: shimmer
        width: Math.max(36, root.width * 0.42)
        height: root.height
        x: root.animate ? -width : root.width * 0.32
        visible: root.animate && root.width > 0 && root.height > 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.28; color: theme.alpha(theme.lightRed, 0.035) }
            GradientStop { position: 0.5; color: theme.alpha(theme.brandPaper, 0.10) }
            GradientStop { position: 0.72; color: theme.alpha(theme.lightBlue, 0.045) }
            GradientStop { position: 1.0; color: "transparent" }
        }

        XAnimator on x {
            running: root.animate
            loops: Animation.Infinite
            // Keep the scene graph clock running through the off-screen rest.
            // PauseAnimation would hand scheduling back to the blocked GUI.
            from: -shimmer.width - root.phaseDelay / 1250 * (root.width + shimmer.width)
            to: root.width + 320 / 1250 * (root.width + shimmer.width)
            duration: 1250 + root.phaseDelay + 320
        }
    }
}
