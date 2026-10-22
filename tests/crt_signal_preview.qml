import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../apps/desktop/qml/components" as Components

ApplicationWindow {
    id: window
    width: 1000
    height: 640
    minimumWidth: 860
    minimumHeight: 600
    visible: true
    title: "Looksy · Animated CRT robot"
    color: "#111719"

    component TestArt: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: 24
            radius: 20
            color: "#152a31"
            border.color: "#9dc9cb"
            border.width: 2
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 46
            text: "LOOKSY"
            color: "#dce9e6"
            font.family: "Helvetica Neue"
            font.pixelSize: 43
            font.weight: Font.DemiBold
        }
        Rectangle {
            anchors.centerIn: parent
            width: 142; height: 142; radius: 71
            color: "#103d49"
            border.width: 4
            border.color: "#a4dedc"
            Rectangle {
                anchors.centerIn: parent
                width: 92; height: 92; radius: 46
                color: "#264b67"
                border.width: 3
                border.color: "#e5b9a5"
                Rectangle {
                    anchors.centerIn: parent
                    width: 48; height: 48; radius: 24
                    color: "#090f18"
                    border.width: 2
                    border.color: "#7bbac9"
                }
            }
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height - 74
            spacing: 9
            Repeater {
                model: ["#f29185", "#95ddba", "#83b3f0", "#ead9a4"]
                delegate: Rectangle {
                    required property color modelData
                    width: 58; height: 20; radius: 3
                    color: modelData
                }
            }
        }
    }

    ColumnLayout {
        visible: !demoMode
        anchors.fill: parent
        anchors.margins: 28
        spacing: 14
        Label {
            text: "Live CRT rendering"
            color: "#edf0ed"
            font.pixelSize: 24
            font.weight: Font.DemiBold
        }
        Label {
            text: "A renderer preview on photo-editor artwork. This is not a character design."
            color: "#aebbc0"
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 22
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Label { text: "CLEAN SOURCE"; color: "#aebbc0" }
                TestArt { Layout.fillWidth: true; Layout.fillHeight: true }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Label { text: "LIVE GPU SIGNAL"; color: "#a4dedc" }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TestArt { id: art; anchors.fill: parent }
                    Components.CrtSignalSurface {
                        id: crt
                        objectName: "crtPreviewSurface"
                        anchors.fill: parent
                        sourceItem: art
                        strength: intensity.value
                        active: !pause.checked
                        reducedMotion: reduced.checked
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Glitch pulse"; enabled: crt.motionEnabled; onClicked: crt.react() }
            Label { text: "CRT strength"; color: "#aebbc0" }
            Slider { id: intensity; from: 0; to: 1; value: 0.9; Layout.fillWidth: true }
            CheckBox { id: pause; text: "Pause" }
            CheckBox { id: reduced; text: "Reduced motion" }
        }
        Label {
            text: "Scanlines · RGB phosphor · local glow · rolling beam · short signal displacement"
            color: "#aebbc0"
            font.pixelSize: 12
        }
    }
    Column {
        visible: demoMode
        x: 36; y: 28
        spacing: 8
        Label { text: "Looksy"; color: "#e7eee7"; font.pixelSize: 27; font.weight: Font.DemiBold }
        Label { text: "An animated little CRT robot"; color: "#91aaa8"; font.pixelSize: 14 }
    }
    Rectangle {
        visible: demoMode
        anchors.horizontalCenter: parent.horizontalCenter
        y: character.y + character.height * 0.903
        width: 165; height: 15; radius: 80
        color: "#090e10"
        opacity: 0.8
    }
    Components.LooksyAgentCharacter {
        id: character
        objectName: "crtWiringCharacter"
        width: demoMode ? 370 : 96
        height: demoMode ? 420 : 112
        x: demoMode ? (window.width - width) / 2 : 0
        y: demoMode ? 90 : 0
        animated: demoMode && !robotPause.checked
        reducedMotion: robotReduced.checked
        mode: "greeting"
        progress: mode === "render" ? 0.45 : -1
        talking: mode === "talk"
        MouseArea {
            anchors.fill: parent
            enabled: demoMode
            cursorShape: Qt.PointingHandCursor
            onClicked: { character.blinkOnce(); character.react() }
        }
    }
    ColumnLayout {
        visible: demoMode
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        spacing: 12
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Repeater {
                model: [{label: "Idle", state: "idle"}, {label: "Wave", state: "greeting"},
                    {label: "Talk", state: "talk"}, {label: "Work", state: "render"},
                    {label: "Walk", state: "walk"}, {label: "Happy", state: "success"}]
                delegate: Button {
                    required property var modelData
                    text: modelData.label
                    highlighted: character.mode === modelData.state
                    onClicked: { autoplay.checked = false; character.mode = modelData.state }
                }
            }
            Button { text: "Glitch"; onClicked: character.react() }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            CheckBox { id: autoplay; text: "Cycle animations"; checked: true }
            CheckBox { id: robotPause; text: "Pause" }
            CheckBox { id: robotReduced; text: "Reduced motion" }
        }
    }
    Timer {
        property int step: 0
        interval: 5000
        repeat: true
        running: demoMode && autoplay.checked && character.motionEnabled
        onTriggered: {
            const states = ["idle", "talk", "render", "success", "walk", "greeting"]
            character.mode = states[step % states.length]
            step += 1
        }
    }
}
