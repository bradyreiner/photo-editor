import QtQuick

Transition {
    property Theme theme: Theme {}
    NumberAnimation { property: "opacity"; to: 0; duration: theme.reducedMotion ? 0 : 90; easing.type: Easing.OutCubic }
}
