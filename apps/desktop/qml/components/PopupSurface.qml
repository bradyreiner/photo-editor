pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

Rectangle {
    id: root
    objectName: "popupSurface"

    property color accent: theme.accentWarm400
    property color fillColor: theme.popupFill
    property color borderColor: theme.alpha(theme.glassEdge, 0.52)
    property color shadowColor: theme.alpha("#000000", 0.38)
    property bool accentRail: false
    readonly property string popupSurfaceContract: "popup-surface:v2|shared|fill:soft-gradient|border:single|accent:optional-rail|radius:consistent|nested-card:false|no-overlap:true"
    readonly property string surfaceHierarchyContract: "surface-hierarchy:v2|popover-dialog|structure-border:true|decorative-border:false|accent-restraint:true|content-owned-by-caller:true"

    Theme { id: theme }

    implicitWidth: theme.menuMinWidth
    radius: theme.radiusSurface
    gradient: Gradient {
        GradientStop { position: 0.0; color: root.fillColor }
        GradientStop { position: 1.0; color: Qt.darker(root.fillColor, 1.075) }
    }
    border.width: 1
    border.color: borderColor
    clip: true
    layer.enabled: root.shadowColor.a > 0 && GraphicsInfo.api !== GraphicsInfo.Software
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
