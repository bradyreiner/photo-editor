import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: overlay

    required property var studioTheme
    required property bool transitionActive
    required property int motionDuration
    required property string transitionTitle
    required property string transitionDetail
    required property color accentColor

    readonly property string motionContract: "tool-switch-transition-motion:v1|duration:" +
                                             String(motionDuration) +
                                             "|overlay-fade:true|interruptible:true|no-layout-shift:true"

    objectName: "editToolTransitionOverlay"
    visible: transitionActive
    color: studioTheme.alpha(studioTheme.ink950, 0.26)
    opacity: visible ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: overlay.motionDuration; easing.type: Easing.OutCubic }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
    }

    Rectangle {
        width: Math.min(340, Math.max(230, parent.width * 0.26))
        height: transitionContent.implicitHeight + 22
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 18
        radius: 8
        color: overlay.studioTheme.alpha(overlay.studioTheme.glassBase, 0.84)
        border.width: 1
        border.color: overlay.studioTheme.alpha(overlay.accentColor, 0.42)

        ColumnLayout {
            id: transitionContent
            anchors.fill: parent
            anchors.margins: 11
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    radius: width / 2
                    color: overlay.accentColor
                    opacity: 0.94
                }

                Label {
                    objectName: "editToolTransitionTitle"
                    Layout.fillWidth: true
                    text: overlay.transitionTitle
                    color: overlay.studioTheme.textStrong
                    font.family: overlay.studioTheme.fontUi
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideNone
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 9
                }
            }

            Label {
                objectName: "editToolTransitionDetail"
                Layout.fillWidth: true
                text: overlay.transitionDetail
                visible: text.length > 0
                color: overlay.studioTheme.textMuted
                font.family: overlay.studioTheme.fontUi
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            ProgressStrip {
                objectName: "editToolTransitionProgress"
                Layout.fillWidth: true
                Layout.preferredHeight: 3
                progress: -1
                accent: overlay.accentColor
            }
        }
    }
}
