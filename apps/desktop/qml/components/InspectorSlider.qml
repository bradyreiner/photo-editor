import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string label: ""
    property string valueText: ""
    property color accent: theme.accentWarm400
    property var trackColors: []
    property alias from: control.from
    property alias to: control.to
    property alias stepSize: control.stepSize
    property alias value: control.value
    readonly property alias pressed: control.pressed
    readonly property real normalizedValue: (control.to - control.from) !== 0 ? (control.value - control.from) / (control.to - control.from) : 0
    readonly property bool hasCenterMarker: control.from < 0 && control.to > 0
    readonly property real centerMarkerPosition: hasCenterMarker ? (0 - control.from) / (control.to - control.from) : 0
    readonly property real fillStartPosition: hasCenterMarker ? Math.min(centerMarkerPosition, control.visualPosition) : 0
    readonly property real fillEndPosition: hasCenterMarker ? Math.max(centerMarkerPosition, control.visualPosition) : control.visualPosition
    signal valueDragged(real value)
    signal resetRequested()

    Theme { id: theme }

    onTrackColorsChanged: trackCanvas.requestPaint()

    implicitHeight: 50

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: root.resetRequested()
    }

    Rectangle {
        anchors.fill: parent
        radius: 0
        color: control.pressed ? theme.alpha(theme.glassLift, 0.34) : theme.alpha(theme.glassBase, 0.16)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus
                        ? theme.alpha(theme.focusRing, 0.82)
                        : (control.pressed ? theme.alpha(root.accent, 0.34) : theme.alpha(theme.glassEdge, 0.10))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space12
        anchors.rightMargin: theme.space10
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: theme.space6

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            Label {
                text: root.label
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            Item {
                Layout.fillWidth: true
            }

            Rectangle {
                radius: 0
                color: control.pressed ? theme.alpha(root.accent, 0.10) : theme.alpha(theme.ink950, 0.28)
                border.width: 1
                border.color: control.pressed ? theme.alpha(root.accent, 0.32) : theme.alpha(theme.polar, 0.10)
                implicitWidth: Math.max(60, valueLabel.implicitWidth + theme.space14)
                implicitHeight: 21

                Label {
                    id: valueLabel
                    anchors.centerIn: parent
                    text: root.valueText
                    color: root.pressed ? theme.textStrong : theme.textMain
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: 16

            Slider {
                id: control
                anchors.fill: parent
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0

                background: Item {
                    x: control.leftPadding
                    y: control.topPadding + control.availableHeight / 2 - 1
                    width: control.availableWidth
                    height: 3

                    Canvas {
                        id: trackCanvas
                        anchors.fill: parent

                        onPaint: {
                            const ctx = getContext("2d");
                            ctx.reset();

                            const colors = Array.isArray(root.trackColors) ? root.trackColors : [];
                            if (colors.length > 1) {
                                const gradient = ctx.createLinearGradient(0, 0, width, 0);
                                for (let index = 0; index < colors.length; ++index) {
                                    gradient.addColorStop(index / (colors.length - 1), colors[index]);
                                }
                                ctx.fillStyle = gradient;
                            } else {
                                ctx.fillStyle = theme.alpha(theme.polar, 0.22);
                            }

                            ctx.beginPath();
                            ctx.rect(0, 0, width, height);
                            ctx.closePath();
                            ctx.fill();
                        }

                        Component.onCompleted: requestPaint()
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 0
                        color: Array.isArray(root.trackColors) && root.trackColors.length > 1 ? theme.alpha(theme.ink950, 0.28) : theme.alpha(theme.ink950, 0.72)
                        border.width: 1
                        border.color: theme.alpha(theme.polar, 0.08)
                    }

                    Rectangle {
                        x: root.fillStartPosition * parent.width
                        width: Math.max(2, (root.fillEndPosition - root.fillStartPosition) * parent.width)
                        height: parent.height
                        radius: 0
                        color: root.accent
                        opacity: 0.72
                    }

                    Rectangle {
                        visible: root.hasCenterMarker
                        x: Math.max(0, Math.min(parent.width - width, root.centerMarkerPosition * parent.width - width / 2))
                        width: 2
                        height: 13
                        radius: 0
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.alpha(theme.polar, 0.54)
                    }

                    Repeater {
                        model: 5

                        Rectangle {
                            width: 1
                            height: index === 2 ? 11 : 7
                            radius: 0
                            x: Math.round(index * (parent.width - 1) / 4)
                            anchors.verticalCenter: parent.verticalCenter
                            color: theme.alpha(theme.polar, index === 2 ? 0.32 : 0.18)
                        }
                    }
                }

                handle: Rectangle {
                    x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
                    y: control.topPadding + control.availableHeight / 2 - height / 2
                    width: control.pressed ? 12 : 10
                    height: control.pressed ? 22 : 20
                    radius: 1
                    color: control.pressed ? theme.polar : theme.alpha(theme.polar, 0.92)
                    border.width: 1
                    border.color: control.pressed ? theme.alpha(root.accent, 0.92) : theme.alpha(theme.polar, 0.42)

                    Behavior on width {
                        NumberAnimation { duration: theme.durationFast }
                    }

                    Behavior on height {
                        NumberAnimation { duration: theme.durationFast }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 2
                        height: 12
                        radius: 0
                        color: control.pressed ? root.accent : theme.alpha(theme.ink950, 0.72)
                    }
                }

                onMoved: root.valueDragged(value)
            }
        }
    }
}
