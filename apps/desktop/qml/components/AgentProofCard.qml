import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string label: "Proof"
    property string imageObjectName: ""
    property url source: ""
    property bool showLabel: true

    Theme { id: theme }

    readonly property string agentProofCardContract: "agent-proof-card:v1|image-fit:true|label-badge:true|empty-placeholder:true|tooltip-full:true|keyboard-focus:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    Layout.fillWidth: true
    radius: theme.radiusControl
    color: proofHover.hovered || activeFocus
           ? theme.alpha(theme.ink900, 0.54)
           : theme.alpha(theme.ink950, 0.46)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? theme.focusRing : theme.alpha(theme.lineSoft, proofHover.hovered ? 0.38 : 0.24)
    clip: true
    activeFocusOnTab: true
    Accessible.name: root.label
    Accessible.description: root.source.toString()

    HoverHandler {
        id: proofHover
    }

    Image {
        objectName: root.imageObjectName
        anchors.fill: parent
        anchors.margins: 2
        source: root.source
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: false
        visible: status === Image.Ready
    }

    AppIcon {
        anchors.centerIn: parent
        name: "image"
        color: theme.textFaint
        implicitWidth: 18
        implicitHeight: 18
        visible: root.source.toString().length === 0
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 5
        radius: theme.radiusControl
        color: theme.alpha(theme.ink950, 0.76)
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.20)
        implicitWidth: proofRoleLabel.implicitWidth + 10
        implicitHeight: 20
        visible: root.showLabel

        Text {
            id: proofRoleLabel
            anchors.centerIn: parent
            width: parent.width - 10
            text: root.label
            color: theme.textStrong
            font.family: theme.fontMono
            font.pixelSize: 9
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            elide: Text.ElideNone
            horizontalAlignment: Text.AlignHCenter
        }
    }

    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && (proofHover.hovered)
        delay: 350
        timeout: 3000
        text: root.label
    }
}
