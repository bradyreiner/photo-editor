pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property bool catalogLoading: false
    property string catalogStatus: ""
    property bool autoStart: true
    property bool catalogObserved: false
    property bool minimumHoldElapsed: false
    property bool completing: false
    property real progressValue: 0
    property int activeStep: 0

    readonly property var loadingItems: [
        { title: "Shell", detail: "Opening workspace", threshold: 0.14 },
        { title: "Controls", detail: "Preparing shortcuts", threshold: 0.32 },
        { title: "Catalog", detail: root.catalogStatus.length > 0 ? root.catalogStatus : "Loading library snapshot", threshold: 0.56 },
        { title: "Preview", detail: "Preparing render path", threshold: 0.76 },
        { title: "Ready", detail: "Finishing startup", threshold: 0.96 }
    ]

    Theme { id: theme }

    modal: true
    dim: true
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 0
    width: parent ? Math.min(520, Math.max(400, parent.width - 64)) : 520
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - implicitHeight) / 2) : 0
    Overlay.modal: Rectangle {
        color: theme.alpha(theme.ink950, 0.68)
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: theme.durationSlow; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: theme.durationSlow; easing.type: Easing.OutCubic }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: theme.durationMedium; easing.type: Easing.InOutSine }
            NumberAnimation { property: "scale"; from: 1; to: 0.985; duration: theme.durationMedium; easing.type: Easing.InOutSine }
        }
    }

    background: Item {}

    contentItem: PopupSurface {
        implicitWidth: root.width
        implicitHeight: contentColumn.implicitHeight + 32
        radius: theme.radius12
        accent: theme.accentWarm400
        accentRail: false

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: 16
            spacing: 18

            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Item {
                    Layout.preferredWidth: 128
                    Layout.preferredHeight: 128

                    Canvas {
                        id: abstractGraphic
                        anchors.fill: parent
                        antialiasing: true
                        opacity: 0.94
                        onPaint: {
                            const ctx = getContext("2d")
                            const w = width
                            const h = height
                            const cx = w / 2
                            const cy = h / 2
                            const progress = Math.max(0, Math.min(1, root.progressValue))

                            ctx.clearRect(0, 0, w, h)
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"

                            ctx.fillStyle = theme.alpha(theme.ink950, 0.36)
                            ctx.beginPath()
                            ctx.rect(cx - 44, cy - 44, 88, 88)
                            ctx.fill()

                            ctx.strokeStyle = theme.alpha(theme.polar, 0.22)
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.rect(cx - 44, cy - 44, 88, 88)
                            ctx.stroke()

                            ctx.strokeStyle = theme.alpha(theme.polar, 0.11)
                            for (let grid = -1; grid <= 1; ++grid) {
                                ctx.beginPath()
                                ctx.moveTo(cx - 34, cy + grid * 22)
                                ctx.lineTo(cx + 34, cy + grid * 22)
                                ctx.stroke()
                                ctx.beginPath()
                                ctx.moveTo(cx + grid * 22, cy - 34)
                                ctx.lineTo(cx + grid * 22, cy + 34)
                                ctx.stroke()
                            }

                            ctx.strokeStyle = theme.alpha(theme.polar, 0.72)
                            ctx.lineWidth = 2.2
                            ctx.beginPath()
                            ctx.arc(cx, cy, 28, -Math.PI * 0.46, Math.PI * (1.38 + progress * 0.18))
                            ctx.stroke()

                            ctx.strokeStyle = theme.alpha(theme.polar, 0.42)
                            ctx.lineWidth = 1.4
                            ctx.beginPath()
                            ctx.arc(cx, cy, 18, Math.PI * 0.72, Math.PI * (1.82 + progress * 0.22))
                            ctx.stroke()

                            ctx.fillStyle = theme.alpha(theme.ink950, 0.82)
                            ctx.beginPath()
                            ctx.arc(cx, cy, 10, 0, Math.PI * 2)
                            ctx.fill()

                            ctx.fillStyle = theme.alpha(theme.polar, 0.86)
                            ctx.beginPath()
                            ctx.arc(cx + Math.cos(progress * Math.PI * 2) * 28,
                                    cy + Math.sin(progress * Math.PI * 2) * 28,
                                    3.4,
                                    0,
                                    Math.PI * 2)
                            ctx.fill()
                        }

                        Connections {
                            target: root
                            function onProgressValueChanged() { abstractGraphic.requestPaint() }
                            function onCompletingChanged() { abstractGraphic.requestPaint() }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        Layout.fillWidth: true
                        text: "Opening PhotoEditor"
                        color: theme.textStrong
                        font.family: theme.fontDisplay
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.catalogLoading && root.catalogStatus.length > 0 ? root.catalogStatus : "Bringing the editor online."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    ProgressStrip {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.preferredHeight: 5
                        progress: root.progressValue
                        accent: theme.accentWarm400
                        trackColor: theme.alpha(theme.ink950, 0.34)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7

                Repeater {
                    model: root.loadingItems

                    delegate: RowLayout {
                        id: stepRow

                        required property int index
                        required property var modelData

                        readonly property bool active: index === root.activeStep && !root.completing
                        readonly property bool complete: root.progressValue >= modelData.threshold || root.completing

                        Layout.fillWidth: true
                        spacing: 10
                        opacity: complete || active ? 1 : 0.62

                        Rectangle {
                            Layout.preferredWidth: 9
                            Layout.preferredHeight: 9
                            radius: width / 2
                            color: stepRow.complete ? theme.polar
                                                    : stepRow.active ? theme.polar : theme.alpha(theme.lineSoft, 0.68)
                            opacity: stepRow.complete || stepRow.active ? 0.9 : 0.52
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                Layout.fillWidth: true
                                text: stepRow.modelData.title
                                color: stepRow.active ? theme.textStrong : theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                font.weight: stepRow.active ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: stepRow.modelData.detail
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        if (root.autoStart)
            Qt.callLater(root.begin)
    }

    onCatalogLoadingChanged: {
        if (root.catalogLoading)
            root.catalogObserved = true
        root.syncActiveStep()
        root.maybeComplete()
    }

    function begin() {
        root.catalogObserved = root.catalogLoading
        root.minimumHoldElapsed = false
        root.completing = false
        root.progressValue = 0.05
        root.activeStep = 0
        root.open()
        minimumHoldTimer.restart()
    }

    function syncActiveStep() {
        let nextStep = 0
        for (let index = 0; index < root.loadingItems.length; ++index) {
            if (root.progressValue >= root.loadingItems[index].threshold)
                nextStep = index
        }
        if (root.catalogLoading)
            nextStep = Math.max(nextStep, 2)
        root.activeStep = Math.min(nextStep, root.loadingItems.length - 1)
    }

    function maybeComplete() {
        if (!root.opened || root.completing || !root.minimumHoldElapsed || root.catalogLoading)
            return

        if (root.catalogObserved || root.progressValue >= 0.88)
            root.finish()
    }

    function finish() {
        root.completing = true
        root.activeStep = root.loadingItems.length - 1
        root.progressValue = 1
        closeTimer.restart()
    }

    Timer {
        id: progressTimer
        interval: 70
        repeat: true
        running: root.opened && !root.completing

        onTriggered: {
            if (root.catalogLoading)
                root.catalogObserved = true

            const cap = root.catalogLoading ? 0.84 : root.catalogObserved ? 0.96 : 0.9
            const step = root.progressValue < 0.26 ? 0.045 : 0.018
            root.progressValue = Math.min(cap, root.progressValue + step)
            root.syncActiveStep()
            root.maybeComplete()
        }
    }

    Timer {
        id: minimumHoldTimer
        interval: 760
        repeat: false
        onTriggered: {
            root.minimumHoldElapsed = true
            root.maybeComplete()
        }
    }

    Timer {
        id: closeTimer
        interval: 430
        repeat: false
        onTriggered: root.close()
    }
}
