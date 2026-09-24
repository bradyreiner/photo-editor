pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: matrix

    property color accent: theme.accentWarm400
    property color headerColor: theme.textMuted
    property var rows: []
    property int editCommitIntervalMs: 16
    property string pendingCellKey: ""
    property real pendingCellValue: 0
    property bool hasPendingCellEdit: false

    signal cellEdited(string key, real value)
    signal interactionStarted()
    signal interactionFinished()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: 178
    radius: 4
    color: theme.alpha(theme.glassBase, 0.42)
    border.width: 0

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function queueCellEdited(key, value) {
        if (!editCommitTimer.running && !hasPendingCellEdit) {
            matrix.cellEdited(key, value)
            editCommitTimer.start()
            return
        }
        pendingCellKey = key
        pendingCellValue = value
        hasPendingCellEdit = true
        if (!editCommitTimer.running)
            editCommitTimer.start()
    }

    function flushCellEdited() {
        if (!hasPendingCellEdit)
            return
        const key = pendingCellKey
        const value = pendingCellValue
        hasPendingCellEdit = false
        editCommitTimer.stop()
        matrix.cellEdited(key, value)
    }

    Timer {
        id: editCommitTimer
        interval: matrix.editCommitIntervalMs
        repeat: false
        onTriggered: matrix.flushCellEdited()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                Layout.preferredWidth: 72
                text: "OUTPUT"
                color: matrix.headerColor
                font.family: theme.fontUi
                font.pixelSize: 9
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0
            }

            Repeater {
                model: ["RED", "GREEN", "BLUE"]

                Label {
                    id: channelHeader
                    required property string modelData
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: channelHeader.modelData
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 9
                    font.weight: Font.Black
                    font.letterSpacing: 0
                }
            }
        }

        Repeater {
            model: matrix.rows

            RowLayout {
                id: mixerRow
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                Label {
                    Layout.preferredWidth: 72
                    text: String(mixerRow.modelData.label || "")
                    color: String(mixerRow.modelData.color || "").length > 0
                           ? mixerRow.modelData.color
                           : theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    font.weight: Font.Black
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
                }

                Repeater {
                    model: mixerRow.modelData.cells || []

                    Rectangle {
                        id: mixerCell
                        required property var modelData
                        readonly property real cellValue: Number(modelData.value || 0)
                        readonly property bool modified: Math.abs(cellValue - Number(modelData.defaultValue || 0)) > 0.5
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 38
                        radius: 3
                        color: modified ? theme.alpha(matrix.accent, 0.12) : theme.alpha(theme.ink950, 0.34)
                        border.width: 1
                        border.color: modified ? theme.alpha(matrix.accent, 0.38) : theme.alpha(theme.polar, 0.10)

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            height: 1
                            color: theme.alpha(theme.polar, 0.16)
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.bottom: parent.verticalCenter
                            width: parent.width
                            height: Math.max(2, Math.abs(mixerCell.cellValue) / 200 * parent.height / 2)
                            visible: mixerCell.cellValue >= 0 && mixerCell.modified
                            color: matrix.accent
                            opacity: 0.48
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.verticalCenter
                            width: parent.width
                            height: Math.max(2, Math.abs(mixerCell.cellValue) / 200 * parent.height / 2)
                            visible: mixerCell.cellValue < 0 && mixerCell.modified
                            color: matrix.accent
                            opacity: 0.48
                        }

                        Label {
                            anchors.centerIn: parent
                            text: Math.round(mixerCell.cellValue).toString()
                            color: mixerCell.modified ? matrix.accent : "#C8D2DC"
                            font.family: theme.fontMono
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.letterSpacing: 0
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.SizeVerCursor
                            onPressed: function(mouse) {
                                matrix.interactionStarted()
                                matrix.queueCellEdited(String(mixerCell.modelData.key || ""), matrix.clamp(200 - mouse.y / Math.max(1, height) * 400, -200, 200))
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed)
                                    matrix.queueCellEdited(String(mixerCell.modelData.key || ""), matrix.clamp(200 - mouse.y / Math.max(1, height) * 400, -200, 200))
                            }
                            onReleased: {
                                matrix.flushCellEdited()
                                matrix.interactionFinished()
                            }
                            onCanceled: {
                                matrix.flushCellEdited()
                                matrix.interactionFinished()
                            }
                            onDoubleClicked: matrix.cellEdited(String(mixerCell.modelData.key || ""), Number(mixerCell.modelData.defaultValue || 0))
                        }
                    }
                }
            }
        }
    }
}
