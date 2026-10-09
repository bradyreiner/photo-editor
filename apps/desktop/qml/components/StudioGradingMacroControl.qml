import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: control

    property string label: ""
    property string valueText: ""
    property real from: -100
    property real to: 100
    property real value: 0
    property real defaultValue: from <= 0 && to >= 0 ? 0 : from
    property int controlHeight: theme.controlHeightTall
    property color accent: theme.accentWarm400
    property color leftColor: theme.alpha(theme.polar, 0.22)
    property color centerColor: theme.alpha(theme.polar, 0.42)
    property color rightColor: accent
    readonly property real normalizedValue: (to - from) !== 0 ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
    readonly property real defaultPosition: (to - from) !== 0 ? Math.max(0, Math.min(1, (defaultValue - from) / (to - from))) : 0
    readonly property bool modified: Math.abs(value - defaultValue) > 0.5

    signal valueDragged(real value)
    signal interactionStarted()
    signal interactionFinished()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: control.controlHeight
    radius: theme.radiusControl
    color: pointer.containsMouse ? theme.alpha(theme.opticalHoverFill, 0.42)
                                 : control.modified ? theme.alpha(theme.opticalSelectedFill, 0.34)
                                                    : "transparent"
    border.width: 0

    function valueFromX(mouseX) {
        const local = Math.max(0, Math.min(track.width, mouseX - track.x))
        return from + (local / Math.max(1, track.width)) * (to - from)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        spacing: theme.gapControl

        Label {
            Layout.preferredWidth: 82
            Layout.minimumWidth: 64
            text: control.label
            color: control.modified ? control.accent : theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeLabel
            font.weight: theme.weightControl
            font.capitalization: Font.MixedCase
            font.letterSpacing: 0
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: theme.typeMicro
            elide: Text.ElideNone
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 24

            Item {
                id: track
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 5

                Row {
                    anchors.fill: parent
                    spacing: 0
                    clip: true

                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: control.leftColor }
                            GradientStop { position: 1.0; color: control.centerColor }
                        }
                    }
                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: control.centerColor }
                            GradientStop { position: 1.0; color: control.rightColor }
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: theme.alpha(theme.ink950, 0.34)
                }
                Rectangle {
                    x: Math.max(0, Math.min(parent.width - width, control.defaultPosition * parent.width - width / 2))
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: 14
                    color: theme.alpha(theme.polar, 0.42)
                }
                Rectangle {
                    visible: control.modified
                    x: Math.min(control.defaultPosition, control.normalizedValue) * parent.width
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(1, Math.abs(control.normalizedValue - control.defaultPosition) * parent.width)
                    height: 4
                    radius: 2
                    color: control.accent
                    opacity: 0.88
                }
                Rectangle {
                    x: Math.max(0, Math.min(parent.width - width, control.normalizedValue * parent.width - width / 2))
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    radius: theme.radiusControl
                    rotation: 45
                    color: control.modified ? control.accent : theme.alpha(theme.polar, 0.92)
                    border.width: 2
                    border.color: theme.alpha(theme.ink950, 0.78)
                }
            }
        }

        Label {
            Layout.preferredWidth: theme.valueColumnWidth
            Layout.minimumWidth: theme.valueColumnWidth
            horizontalAlignment: Text.AlignRight
            text: control.valueText
            color: control.modified ? control.accent : "#C8D2DC"
            font.family: theme.fontMono
            font.pixelSize: theme.typeCaption
            font.weight: theme.weightSection
            font.letterSpacing: 0
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: function(mouse) {
            control.interactionStarted()
            control.valueDragged(control.valueFromX(mouse.x))
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                control.valueDragged(control.valueFromX(mouse.x))
        }
        onReleased: control.interactionFinished()
        onCanceled: control.interactionFinished()
        onDoubleClicked: control.valueDragged(control.defaultValue)
    }
}
