import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string markerId: ""
    property string label: "Marker"
    property color accent: theme.accentWarm500

    signal jumpRequested(string markerId)
    signal editRequested(string markerId)

    Theme { id: theme }

    readonly property string timelineMarkerPinContract: "timeline-marker-pin:v1|line:true|label-fit:true|jump-edit:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    width: 2
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: "Timeline marker"

    Keys.onReturnPressed: root.jumpRequested(root.markerId)
    Keys.onEnterPressed: root.jumpRequested(root.markerId)
    Keys.onSpacePressed: root.jumpRequested(root.markerId)

    Rectangle {
        anchors.top: parent.top
        width: 2
        height: root.height
        color: root.accent
        opacity: markerMouse.containsMouse || root.activeFocus ? 0.92 : 0.70
    }

    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        radius: theme.radius6
        color: root.accent
        width: Math.min(140, Math.max(28, markerLabel.implicitWidth + theme.space12))
        height: markerLabel.implicitHeight + theme.space6
        border.width: root.activeFocus ? 1 : 0
        border.color: theme.focusRing

        Text {
            id: markerLabel
            anchors.centerIn: parent
            width: parent.width - theme.space12
            text: root.label
            color: theme.ink950
            font.family: theme.fontMono
            font.pixelSize: 8
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 6
        }
    }

    MouseArea {
        id: markerMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.jumpRequested(root.markerId)
        onDoubleClicked: root.editRequested(root.markerId)
    }

    ToolTip.visible: markerMouse.containsMouse
    ToolTip.delay: 350
    ToolTip.timeout: 3000
    ToolTip.text: root.label
}
