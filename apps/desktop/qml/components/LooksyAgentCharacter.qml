import QtQuick

Item {
    id: root

    property string mode: "idle"
    property string tool: "auto"
    property bool talking: false
    property bool celebrating: false
    property bool animated: true
    property color accentColor: theme.primaryAccent
    property url spriteSource: "qrc:/photo-editor/agent/looksy-agent-sprite-sheet.png"
    property int frame: 0
    property int celebrationFrame: 0
    property int activePose: 0
    property int previousPose: 0
    property real poseBlend: 1

    readonly property bool motionEnabled: animated && !theme.reducedMotion && visible
    readonly property bool dance: celebrating && motionEnabled && celebrationFrame < 174
    readonly property string normalizedMode: String(mode || "idle").toLowerCase()
    readonly property string normalizedTool: String(tool || "auto").toLowerCase()
    readonly property int poseIndex: {
        if (root.dance || root.normalizedMode === "dance" || root.normalizedMode === "celebrate")
            return 7
        if (root.normalizedMode === "wave")
            return 6
        if (root.celebrating && root.celebrationFrame >= 174)
            return 6
        if (root.normalizedMode === "walk" || root.normalizedMode === "walking")
            return 4
        if (root.normalizedMode === "point" || root.normalizedTool === "frame")
            return 5
        if (root.normalizedMode === "success")
            return 6
        if (root.normalizedMode === "inspect" || root.normalizedMode === "observe" ||
                root.normalizedMode === "review" || root.normalizedTool === "loupe")
            return 2
        if (root.normalizedMode === "thinking" || root.normalizedMode === "think" ||
                root.normalizedMode === "warning")
            return 1
        if (root.normalizedMode === "adjust" || root.normalizedMode === "processing" ||
                root.normalizedTool === "sampler" || root.normalizedTool === "tone-key")
            return 3
        return 0
    }
    readonly property rect activeClip: clipForPose(activePose)
    readonly property rect previousClip: clipForPose(previousPose)
    readonly property string visualDesignContract:
        "looksy-agent-character:v4|name:looksy|form:retro-camera-robot|sprite-atlas:4x2|cell:362x543|poses:idle-think-inspect-adjust-walk-point-thumbs-up-dance|motion:state-crossfade-subtle"

    Theme { id: theme }

    implicitWidth: 132
    implicitHeight: 188
    Accessible.role: Accessible.Graphic
    Accessible.name: "Looksy visual assistant, " + mode

    function clipForPose(index) {
        const column = index % 4
        const row = Math.floor(index / 4)
        return Qt.rect(column * 362, row * 543, 362, 543)
    }

    function updatePose() {
        if (root.poseIndex === root.activePose)
            return
        root.previousPose = root.activePose
        root.activePose = root.poseIndex
        root.poseBlend = 0
        poseBlendAnimation.restart()
    }

    Timer {
        interval: 16
        repeat: true
        running: root.motionEnabled
        onTriggered: {
            root.frame = (root.frame + 1) % 3600
            if (root.celebrating && root.celebrationFrame < 174)
                root.celebrationFrame += 1
        }
    }

    SequentialAnimation {
        id: poseBlendAnimation
        NumberAnimation {
            target: root
            property: "poseBlend"
            to: 1
            duration: theme.reducedMotion ? 0 : 180
            easing.type: Easing.OutCubic
        }
    }

    Item {
        id: spriteStage
        anchors.fill: parent
        y: root.motionEnabled && root.talking ? Math.sin(root.frame * 0.07) * 0.45 : 0
        x: root.motionEnabled && root.normalizedMode === "walk"
           ? Math.sin(root.frame * 0.07) * 1.2 : 0
        rotation: root.motionEnabled && root.normalizedMode === "walk"
                  ? Math.sin(root.frame * 0.07) * 0.35 : 0
        scale: root.motionEnabled && root.dance ? 1 + Math.sin(root.frame * 0.12) * 0.012
               : root.motionEnabled && root.talking ? 1 + Math.sin(root.frame * 0.1) * 0.003 : 1

        Behavior on x { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.InOutSine } }
        Behavior on rotation { NumberAnimation { duration: 150; easing.type: Easing.InOutSine } }
        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.InOutSine } }

        Image {
            anchors.fill: parent
            source: root.spriteSource
            sourceClipRect: root.previousClip
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            asynchronous: true
            opacity: 1 - root.poseBlend
        }

        Image {
            anchors.fill: parent
            source: root.spriteSource
            sourceClipRect: root.activeClip
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            asynchronous: true
            opacity: root.poseBlend
        }

    }

    onModeChanged: updatePose()
    onToolChanged: updatePose()
    onCelebratingChanged: {
        if (root.celebrating)
            root.celebrationFrame = 0
        updatePose()
    }
    onMotionEnabledChanged: updatePose()
    Component.onCompleted: {
        root.activePose = root.poseIndex
        root.previousPose = root.poseIndex
        root.poseBlend = 1
    }
}
