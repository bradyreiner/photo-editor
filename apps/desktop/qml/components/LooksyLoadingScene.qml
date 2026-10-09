import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root
    property string scene: "library"
    property string title: ""
    property string detail: ""
    property bool active: true
    property bool failed: false
    property bool reducedMotion: false
    property real progress: -1
    property bool compact: false
    property bool showProgress: true
    property real phase: 0
    readonly property bool motionEnabled: active && visible && enabled && !failed && !reducedMotion && !theme.reducedMotion
    readonly property bool traveling: false
    readonly property real boundedProgress: Math.max(0, Math.min(1, progress))
    readonly property string renderingContract: "looksy-loading-scene:v3|art:procedural-line|character:single-body|activity:integrated-light|empty:resting|hidden:stopped"
    implicitWidth: compact ? 280 : 440
    implicitHeight: content.implicitHeight
    Accessible.role: active ? Accessible.ProgressBar : Accessible.Graphic
    Accessible.name: title
    Accessible.description: detail + (progress >= 0 ? " " + Math.round(boundedProgress * 100) + "%" : "")
    Theme { id: theme }

    Column {
        id: content
        width: parent.width
        spacing: root.compact ? 8 : 12
        Item {
            id: stage
            objectName: "loadingSceneStage"
            width: parent.width
            height: root.compact ? 126 : 178
            // The scene is a handful of document lines, not a separate image.
            Canvas {
                id: contextLines
                objectName: "loadingSceneLineContext"
                anchors.fill: parent
                antialiasing: true
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onVisibleChanged: { if (visible) requestPaint() }
                Connections { target: root; function onSceneChanged() { contextLines.requestPaint() } }
                onPaint: {
                    const c = getContext("2d")
                    c.reset()
                    if (!root.visible || width <= 0 || height <= 0) return
                    c.strokeStyle = "#526167"
                    c.lineWidth = 1
                    c.lineCap = "round"
                    const x = width / 2, y = height * 0.7
                    c.globalAlpha = 0.45
                    c.beginPath(); c.moveTo(x - 100, y + 22); c.lineTo(x + 100, y + 22); c.stroke()
                    c.globalAlpha = 0.6
                    if (root.scene === "timeline") {
                        for (let row = 0; row < 2; ++row) {
                            const left = x + 48 + row * 8, top = y - 34 + row * 15
                            c.strokeRect(left, top, 44 - row * 9, 7)
                        }
                    } else if ((root.scene === "library" || root.scene === "import")) {
                        for (let card = 2; card >= 0; --card) {
                            c.strokeRect(x + 51 + card * 5, y - 39 - card * 5, 28, 31)
                        }
                        c.beginPath(); c.moveTo(x + 56, y - 16); c.lineTo(x + 63, y - 23); c.lineTo(x + 73, y - 14); c.stroke()
                    } else if (root.scene === "export") {
                        c.beginPath(); c.moveTo(x + 51, y - 25); c.lineTo(x + 79, y - 25)
                        c.moveTo(x + 71, y - 33); c.lineTo(x + 79, y - 25); c.lineTo(x + 71, y - 17); c.stroke()
                    } else {
                        c.beginPath(); c.arc(x + 66, y - 24, 17, -Math.PI * 0.7, Math.PI * 0.7); c.stroke()
                        c.strokeRect(x + 60, y - 30, 12, 12)
                    }
                }
            }
            AdditiveLightWaves {
                objectName: "loadingSceneLightWaves"
                width: Math.min(200, parent.width * 0.62)
                height: 30
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.72
                active: root.motionEnabled
                reducedMotion: root.reducedMotion
                opacity: root.failed ? 0 : root.active ? 0.4 : 0.15
            }
            LooksyAgentCharacter {
                id: character
                objectName: "loadingSceneRobot"
                width: root.compact ? 86 : 118
                height: width * 1.16
                x: (stage.width - width) / 2 - 12
                y: stage.height * 0.7 + 22 - feetBaseline
                animated: root.motionEnabled
                reducedMotion: root.reducedMotion
                progress: root.showProgress ? root.progress : -1
                mode: root.failed ? "warning" : !root.active ? "idle"
                    : root.scene === "timeline" ? "assemble"
                    : root.scene === "export" ? "export"
                    : root.scene === "library" ? "sort" : "scan"
                tool: "none"
                Accessible.ignored: true
            }
        }
        Label {
            width: parent.width
            visible: root.title.length > 0
            text: root.title
            horizontalAlignment: Text.AlignHCenter
            color: root.failed ? theme.error : theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: root.compact ? 14 : 18
            font.weight: Font.Medium
            wrapMode: Text.WordWrap
        }
        Label {
            width: parent.width
            visible: root.detail.length > 0
            text: root.detail
            horizontalAlignment: Text.AlignHCenter
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: root.compact ? 11 : 13
            wrapMode: Text.WordWrap
        }
        ProgressStrip {
            objectName: "loadingSceneProgress"
            x: root.width * 0.28
            width: root.width * 0.44
            height: 3
            visible: root.showProgress && root.progress >= 0
            progress: root.progress
            failed: root.failed
            active: root.motionEnabled
        }
        Label {
            width: parent.width
            visible: root.showProgress && root.progress >= 0
            text: Math.round(root.boundedProgress * 100) + "%"
            horizontalAlignment: Text.AlignHCenter
            color: theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: 11
        }
    }
}
