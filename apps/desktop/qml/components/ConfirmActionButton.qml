import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string text: ""
    property string confirmText: "Confirm"
    property string helperText: "Press again to confirm"
    property string iconName: ""
    property bool compact: false
    property bool enabled: true
    property bool fillButtonWidth: true
    property int minimumReadableWidth: 0
    readonly property bool armed: armedState
    signal confirmed()

    Theme { id: theme }

    property bool armedState: false

    implicitWidth: buttonColumn.implicitWidth
    implicitHeight: buttonColumn.implicitHeight

    function disarm() {
        armedState = false
        armTimer.stop()
    }

    Timer {
        id: armTimer
        interval: 3200
        onTriggered: root.armedState = false
    }

    ColumnLayout {
        id: buttonColumn
        anchors.fill: parent
        spacing: theme.space4

        ActionButton {
            id: button
            Layout.fillWidth: root.fillButtonWidth
            text: root.armed ? root.confirmText : root.text
            iconName: root.iconName
            compact: root.compact
            minimumReadableWidth: root.minimumReadableWidth
            enabled: root.enabled
            tone: "danger"
            active: root.armed
            onClicked: {
                if (!root.enabled)
                    return

                if (root.armed) {
                    root.disarm()
                    root.confirmed()
                    return
                }

                root.armedState = true
                armTimer.restart()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.armed
            text: root.helperText
            color: theme.error
            font.family: theme.fontMono
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }
    }
}
