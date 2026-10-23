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
    readonly property string renderingContract: "looksy-loading-scene:v4|art:caustic-wavefront|character:none|color:additive-rgb|empty:resting|hidden:stopped"
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
            OpticalLightField {
                objectName: "loadingSceneLightField"
                anchors.fill: parent
                active: root.motionEnabled
                reducedMotion: root.reducedMotion
                failed: root.failed
                variant: root.scene === "timeline" ? 2 : root.scene === "export" ? 4
                    : root.scene === "import" ? 3 : root.scene === "startup" ? 1 : 0
                opacity: root.failed ? 0.45 : root.active ? 1 : 0.35
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
            reducedMotion: root.reducedMotion
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
