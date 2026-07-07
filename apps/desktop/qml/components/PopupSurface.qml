import QtQuick

Rectangle {
    id: root
    objectName: "popupSurface"

    property color accent: theme.accentWarm400
    property color fillColor: theme.alpha(theme.glassBase, 0.92)
    property color borderColor: theme.alpha(theme.glassEdge, 0.5)
    property color shadowColor: "transparent"
    property bool accentRail: false
    readonly property string popupSurfaceContract: "popup-surface:v1|shared|fill:subtle|border:single|accent:optional-rail|radius:consistent|nested-card:false|no-overlap:true"
    readonly property string surfaceHierarchyContract: "surface-hierarchy:v1|popover-dialog|structure-border:true|decorative-border:false|accent-restraint:true|content-owned-by-caller:true"

    Theme { id: theme }

    implicitWidth: theme.menuMinWidth
    radius: theme.radius8
    color: fillColor
    border.width: 1
    border.color: borderColor
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        height: 1
        color: theme.alpha(theme.polar, 0.08)
    }
}
