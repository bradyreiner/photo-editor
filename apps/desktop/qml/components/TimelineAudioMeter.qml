import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    required property var theme
    property var meter: ({})
    property bool reducedMotion: false
    property string loudnessText: ""
    readonly property string timelineAudioMeterContract: "timeline-audio-meter:v1|peak:truepeak:rms:lufs|scale:0-minus6-minus12-minus24-minus48|overload:latch|motion:optional"

    function db(value) {
        return Math.max(-60, 20 * Math.log(Math.max(0.000001, Number(value || 0))) / Math.LN10)
    }

    function normalizedDb(value) {
        return Math.max(0, Math.min(1, (db(value) + 60) / 60))
    }

    implicitWidth: theme.scaled(92)
    implicitHeight: theme.scaled(242)
    Accessible.role: Accessible.Indicator
    Accessible.name: "Audio meter"
    Accessible.description: "Peak " + db(Math.max(Number(meter.left || 0), Number(meter.right || 0))).toFixed(1)
                            + " dBFS, true peak "
                            + db(Math.max(Number(meter.truePeakLeft || 0), Number(meter.truePeakRight || 0))).toFixed(1)
                            + " dBTP, RMS "
                            + db(Math.max(Number(meter.rmsLeft || 0), Number(meter.rmsRight || 0))).toFixed(1)

    RowLayout {
        anchors.fill: parent
        spacing: theme.space4

        Row {
            Layout.fillHeight: true
            Layout.preferredWidth: theme.scaled(34)
            spacing: theme.space3

            Repeater {
                model: [
                    Math.max(Number(root.meter.left || 0), Number(root.meter.rmsLeft || 0)),
                    Math.max(Number(root.meter.right || 0), Number(root.meter.rmsRight || 0))
                ]

                delegate: Rectangle {
                    required property int index
                    required property real modelData
                    width: theme.scaled(14)
                    height: parent ? parent.height : 0
                    radius: theme.radius4
                    color: theme.alpha(theme.ink950, 0.72)
                    border.width: 0
                    clip: true

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent && root ? parent.height * root.normalizedDb(modelData) : 0
                        radius: parent.radius
                        color: root && root.db(modelData) > -1
                               ? theme.error
                               : root && root.db(modelData) > -9
                                 ? theme.warning : theme.success

                        Behavior on height {
                            enabled: root && !root.reducedMotion
                            NumberAnimation { duration: 70; easing.type: Easing.OutCubic }
                        }
                    }

                    Rectangle {
                        readonly property real truePeak: !root ? 0 : index === 0
                                                         ? Number(root.meter.truePeakLeft || 0)
                                                         : Number(root.meter.truePeakRight || 0)
                        anchors.left: parent.left
                        anchors.right: parent.right
                        y: parent && root ? Math.max(0, parent.height * (1 - root.normalizedDb(truePeak)) - 1) : 0
                        height: 2
                        color: root && root.db(truePeak) > -1 ? theme.error : theme.textStrong
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: theme.scaled(24)

            Repeater {
                model: [0, -6, -12, -24, -48]

                delegate: Label {
                    required property int modelData
                    anchors.right: parent ? parent.right : undefined
                    y: parent ? Math.max(0, Math.min(parent.height - implicitHeight,
                                            parent.height * (1 - (modelData + 60) / 60)
                                            - implicitHeight / 2)) : 0
                    text: String(modelData)
                    color: theme.textFaint
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
                }
            }
        }

        ColumnLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: theme.space2

            StatusChip {
                text: root.meter.overloadLatched === true ? "OVR" : "OK"
                tone: root.meter.overloadLatched === true ? "danger" : "neutral"
                compact: true
                minimumReadableWidth: 30
                maximumReadableWidth: 38
                tooltipText: root.meter.overloadLatched === true
                             ? "Overload is latched until Reset Meters."
                             : "No overload since the last reset."
            }

            Item { Layout.fillHeight: true }

            Label {
                text: "P " + root.db(Math.max(Number(root.meter.left || 0),
                                               Number(root.meter.right || 0))).toFixed(1)
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
            }

            Label {
                text: "TP " + root.db(Math.max(Number(root.meter.truePeakLeft || 0),
                                                Number(root.meter.truePeakRight || 0))).toFixed(1)
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
            }

            Label {
                text: "R " + root.db(Math.max(Number(root.meter.rmsLeft || 0),
                                               Number(root.meter.rmsRight || 0))).toFixed(1)
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
            }

            Label {
                visible: root.loudnessText.length > 0
                text: root.loudnessText
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                wrapMode: Text.Wrap
            }
        }
    }
}
