import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string label: ""
    property string valueText: ""
    property color accent: "#c99a52"
    property var trackColors: []
    property alias from: control.from
    property alias to: control.to
    property alias stepSize: control.stepSize
    property alias value: control.value
    readonly property alias pressed: control.pressed
    readonly property real normalizedValue: (control.to - control.from) !== 0 ? (control.value - control.from) / (control.to - control.from) : 0
    readonly property bool hasCenterMarker: control.from < 0 && control.to > 0
    readonly property real centerMarkerPosition: hasCenterMarker ? (0 - control.from) / (control.to - control.from) : 0
    signal valueDragged(real value)
    signal resetRequested()

    onTrackColorsChanged: trackCanvas.requestPaint()

    implicitHeight: 50

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: root.resetRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: root.label
                color: "#dbe2e9"
                font.family: "Avenir Next"
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Item {
                Layout.fillWidth: true
            }

            Rectangle {
                radius: 6
                color: "#171c21"
                implicitWidth: Math.max(48, valueLabel.implicitWidth + 14)
                implicitHeight: 22

                Label {
                    id: valueLabel
                    anchors.centerIn: parent
                    text: root.valueText
                    color: root.pressed ? "#f4f7f9" : "#9ea9b4"
                    font.family: "Menlo"
                    font.pixelSize: 10
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: 22

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
                    y: control.topPadding + control.availableHeight / 2 - 2
                    width: control.availableWidth
                    height: 4

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
                                ctx.fillStyle = "#313841";
                            }

                            const radius = Math.min(2, width / 2, height / 2);
                            ctx.beginPath();
                            ctx.moveTo(radius, 0);
                            ctx.lineTo(width - radius, 0);
                            ctx.quadraticCurveTo(width, 0, width, radius);
                            ctx.lineTo(width, height - radius);
                            ctx.quadraticCurveTo(width, height, width - radius, height);
                            ctx.lineTo(radius, height);
                            ctx.quadraticCurveTo(0, height, 0, height - radius);
                            ctx.lineTo(0, radius);
                            ctx.quadraticCurveTo(0, 0, radius, 0);
                            ctx.closePath();
                            ctx.fill();
                        }

                        Component.onCompleted: requestPaint()
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 2
                        color: Array.isArray(root.trackColors) && root.trackColors.length > 1 ? "#44101519" : "#313841"
                    }

                    Rectangle {
                        x: root.hasCenterMarker ? Math.min(root.centerMarkerPosition * parent.width, control.visualPosition * parent.width) : 0
                        width: root.hasCenterMarker ? Math.max(2, Math.abs(control.visualPosition - root.centerMarkerPosition) * parent.width) : Math.max(6, control.visualPosition * parent.width)
                        height: parent.height
                        radius: 2
                        color: root.accent
                    }

                    Rectangle {
                        visible: root.hasCenterMarker
                        x: Math.max(0, Math.min(parent.width - width, root.centerMarkerPosition * parent.width - width / 2))
                        width: 2
                        height: 10
                        radius: 1
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#6f7983"
                    }
                }

                handle: Rectangle {
                    x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
                    y: control.topPadding + control.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: root.accent

                    Rectangle {
                        anchors.centerIn: parent
                        width: 4
                        height: 4
                        radius: 2
                        color: "#f4f7f8"
                        opacity: control.pressed ? 1.0 : 0.8
                    }
                }

                onMoved: root.valueDragged(value)
            }
        }
    }
}
