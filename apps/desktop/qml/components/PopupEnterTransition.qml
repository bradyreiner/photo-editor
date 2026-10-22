import QtQuick

Transition {
    property Theme theme: Theme {}
    ParallelAnimation {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: theme.opticalOpenDuration; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: theme.reducedMotion ? 1 : 0.995; to: 1; duration: theme.opticalOpenDuration; easing.type: Easing.OutCubic }
    }
}
