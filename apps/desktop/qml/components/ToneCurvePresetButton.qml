import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root

    property string presetName: ""

    signal previewRequested(string presetName)
    signal previewCleared()
    signal selectedRequested(string presetName)

    Theme { id: theme }

    readonly property string toneCurvePresetButtonContract: "tone-curve-preset-button:v1|preview-hover:true|select:true|keyboard:true|readable-text:true|no-elide:true|tooltip-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    implicitHeight: 30
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.presetName.length > 0 ? root.presetName + " curve preset" : "Curve preset"

    Keys.onReturnPressed: root.selectedRequested(root.presetName)
    Keys.onEnterPressed: root.selectedRequested(root.presetName)
    Keys.onSpacePressed: root.selectedRequested(root.presetName)

    Rectangle {
        anchors.fill: parent
        radius: theme.radius8
        color: presetMouse.containsMouse || root.activeFocus
               ? theme.alpha(theme.glassLift, 0.42)
               : theme.alpha(theme.glassBase, 0.54)
        border.width: root.activeFocus ? 2 : 0
        border.color: root.activeFocus ? theme.focusRing : "transparent"
    }

    Text {
        anchors.centerIn: parent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: theme.space4
        anchors.rightMargin: theme.space4
        text: root.presetName
        color: presetMouse.containsMouse || root.activeFocus ? theme.textMain : theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        font.weight: Font.DemiBold
        font.letterSpacing: 0
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        maximumLineCount: 1
        elide: Text.ElideNone
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 8
    }

    MouseArea {
        id: presetMouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onEntered: root.previewRequested(root.presetName)
        onExited: root.previewCleared()
        onClicked: root.selectedRequested(root.presetName)
    }

    ToolTip.visible: presetMouse.containsMouse && root.presetName.length > 0
    ToolTip.delay: 350
    ToolTip.timeout: 2600
    ToolTip.text: root.presetName + " curve preset"
}
