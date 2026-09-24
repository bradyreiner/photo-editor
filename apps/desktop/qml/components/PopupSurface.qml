import QtQuick
import QtQuick.Effects

Rectangle {
    id: root
    objectName: "popupSurface"

    property color accent: theme.accentWarm400
    property color fillColor: theme.surfaceTop
    property color borderColor: theme.alpha(theme.glassEdge, 0.52)
    property color shadowColor: "transparent"
    property bool accentRail: false
    readonly property string popupSurfaceContract: "popup-surface:v2|shared|fill:soft-gradient|border:single|accent:optional-rail|radius:consistent|nested-card:false|no-overlap:true"
    readonly property string surfaceHierarchyContract: "surface-hierarchy:v2|popover-dialog|structure-border:true|decorative-border:false|accent-restraint:true|content-owned-by-caller:true"

    Theme { id: theme }

    implicitWidth: theme.menuMinWidth
    radius: theme.radius12
    gradient: Gradient {
        GradientStop { position: 0.0; color: root.fillColor }
        GradientStop { position: 1.0; color: theme.surfaceBottom }
    }
    border.width: 1
    border.color: borderColor
    clip: true
    layer.enabled: root.shadowColor.a > 0
    layer.effect: MultiEffect {
        shadowEnabled: root.shadowColor.a > 0
        shadowColor: root.shadowColor
        shadowBlur: 0.42
        shadowVerticalOffset: 7
    }

    Rectangle {
        visible: root.accentRail
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        color: root.accent
        opacity: 0.86
    }
}
