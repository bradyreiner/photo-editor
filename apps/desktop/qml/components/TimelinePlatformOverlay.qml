pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property var definition: ({})
    property string deviceCrop: "none"
    property bool showSafeZones: true
    property bool showControls: true
    property bool showProfile: true
    property bool showCaptions: true

    readonly property string overlayContract: "timeline-platform-overlay-surface:v1|geometry:normalized-platform-safe-regions|layers:unsafe-wash-safe-corners-controls-profile-caption-device-cutout|interaction:none|render-path:program-only"
    readonly property real safeLeft: Math.max(0, Math.min(0.45, Number(definition.safeLeft || 0.06)))
    readonly property real safeRight: Math.max(0, Math.min(0.45, Number(definition.safeRight || 0.06)))
    readonly property real safeTop: Math.max(0, Math.min(0.45, Number(definition.safeTop || 0.08)))
    readonly property real safeBottom: Math.max(0, Math.min(0.45, Number(definition.safeBottom || 0.18)))
    readonly property bool feedLayout: String(definition.key || "") === "feed"

    Theme { id: theme }

    enabled: false
    clip: true
    Accessible.ignored: true

    Rectangle {
        objectName: "timelinePlatformUnsafeTop"
        visible: root.showSafeZones
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height * root.safeTop
        color: theme.alpha(theme.ink950, 0.22)
    }

    Rectangle {
        objectName: "timelinePlatformUnsafeBottom"
        visible: root.showSafeZones
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * root.safeBottom
        color: theme.alpha(theme.ink950, 0.30)
    }

    Rectangle {
        objectName: "timelinePlatformUnsafeLeft"
        visible: root.showSafeZones
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: parent.height * root.safeTop
        anchors.bottomMargin: parent.height * root.safeBottom
        width: parent.width * root.safeLeft
        color: theme.alpha(theme.ink950, 0.16)
    }

    Rectangle {
        objectName: "timelinePlatformUnsafeRight"
        visible: root.showSafeZones
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: parent.height * root.safeTop
        anchors.bottomMargin: parent.height * root.safeBottom
        width: parent.width * root.safeRight
        color: theme.alpha(theme.ink950, 0.24)
    }

    Canvas {
        id: safeGuideCanvas
        objectName: "timelinePlatformSafeGuide"
        anchors.fill: parent
        visible: root.showSafeZones

        function schedulePaint() {
            requestPaint()
        }

        onWidthChanged: schedulePaint()
        onHeightChanged: schedulePaint()
        Connections {
            target: root
            function onDefinitionChanged() { safeGuideCanvas.schedulePaint() }
            function onShowSafeZonesChanged() { safeGuideCanvas.schedulePaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const left = width * root.safeLeft
            const right = width * (1 - root.safeRight)
            const top = height * root.safeTop
            const bottom = height * (1 - root.safeBottom)
            const corner = Math.max(8, Math.min(width, height) * 0.035)
            ctx.strokeStyle = theme.alpha(theme.textStrong, 0.64)
            ctx.lineWidth = Math.max(1, theme.hairlineWidth)
            ctx.setLineDash([])
            ctx.beginPath()
            ctx.moveTo(left, top + corner)
            ctx.lineTo(left, top)
            ctx.lineTo(left + corner, top)
            ctx.moveTo(right - corner, top)
            ctx.lineTo(right, top)
            ctx.lineTo(right, top + corner)
            ctx.moveTo(right, bottom - corner)
            ctx.lineTo(right, bottom)
            ctx.lineTo(right - corner, bottom)
            ctx.moveTo(left + corner, bottom)
            ctx.lineTo(left, bottom)
            ctx.lineTo(left, bottom - corner)
            ctx.stroke()
        }
    }

    RowLayout {
        objectName: "timelinePlatformTopControls"
        visible: root.showControls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Math.max(theme.space8, parent.width * 0.045)
        anchors.rightMargin: Math.max(theme.space8, parent.width * 0.045)
        anchors.topMargin: Math.max(theme.space6, parent.height * 0.028)
        spacing: theme.space6

        Label {
            Layout.fillWidth: true
            text: String(root.definition.label || "Platform") + "  ·  PREVIEW"
            color: theme.alpha(theme.textStrong, 0.86)
            font.family: theme.fontUi
            font.pixelSize: Math.max(9, Math.min(theme.typeCaption, parent.parent.height * 0.035))
            font.weight: Font.DemiBold
        }

        Repeater {
            model: root.feedLayout ? ["•••"] : ["LIVE", "⌕"]
            delegate: Label {
                required property var modelData
                text: String(modelData)
                color: theme.alpha(theme.textStrong, 0.76)
                font.family: theme.fontUi
                font.pixelSize: Math.max(9, Math.min(theme.typeCaption, root.height * 0.032))
                font.weight: Font.DemiBold
            }
        }
    }

    ColumnLayout {
        objectName: "timelinePlatformActionRail"
        visible: root.showControls && !root.feedLayout
        anchors.right: parent.right
        anchors.rightMargin: Math.max(theme.space6, parent.width * 0.035)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: parent.height * Math.max(0.18, root.safeBottom - 0.02)
        spacing: Math.max(theme.space6, root.height * 0.018)

        Repeater {
            model: root.showProfile ? ["+", "♥", "◯", "↗", "♫"] : ["♥", "◯", "↗", "♫"]
            delegate: ColumnLayout {
                required property int index
                required property var modelData
                spacing: 1

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.max(17, Math.min(30, root.width * 0.07))
                    Layout.preferredHeight: Layout.preferredWidth
                    radius: width / 2
                    color: index === 0 && root.showProfile
                           ? theme.alpha(theme.accentWarm300, 0.90)
                           : theme.alpha(theme.ink950, 0.62)

                    Label {
                        anchors.centerIn: parent
                        text: String(parent.parent.modelData)
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: Math.max(9, parent.width * 0.46)
                        font.weight: Font.DemiBold
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !(index === 0 && root.showProfile) && index < 4
                    text: index === (root.showProfile ? 1 : 0) ? "12K"
                          : index === (root.showProfile ? 2 : 1) ? "248"
                          : index === (root.showProfile ? 3 : 2) ? "Share" : ""
                    color: theme.alpha(theme.textStrong, 0.82)
                    font.family: theme.fontMono
                    font.pixelSize: Math.max(8, Math.min(theme.typeCaption, root.height * 0.026))
                }
            }
        }
    }

    ColumnLayout {
        objectName: "timelinePlatformCaptionRegion"
        visible: root.showCaptions
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: parent.width * Math.max(0.06, root.safeLeft)
        anchors.rightMargin: parent.width * (root.feedLayout ? 0.06 : Math.max(0.19, root.safeRight))
        anchors.bottomMargin: parent.height * (root.feedLayout ? 0.11 : Math.max(0.12, root.safeBottom - 0.06))
        spacing: Math.max(2, theme.space2)

        Label {
            Layout.fillWidth: true
            text: root.showProfile ? "@creator" : "Caption preview"
            color: theme.alpha(theme.textStrong, 0.92)
            font.family: theme.fontUi
            font.pixelSize: Math.max(9, Math.min(theme.typeLabel, root.height * 0.034))
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Rectangle {
            Layout.preferredWidth: Math.max(44, parent.width * 0.78)
            Layout.preferredHeight: Math.max(2, root.height * 0.008)
            radius: height / 2
            color: theme.alpha(theme.textStrong, 0.74)
        }

        Rectangle {
            Layout.preferredWidth: Math.max(30, parent.width * 0.52)
            Layout.preferredHeight: Math.max(2, root.height * 0.008)
            radius: height / 2
            color: theme.alpha(theme.textStrong, 0.44)
        }
    }

    RowLayout {
        objectName: "timelinePlatformFeedControls"
        visible: root.showControls && root.feedLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: parent.width * 0.06
        anchors.rightMargin: parent.width * 0.06
        anchors.bottomMargin: parent.height * 0.045
        spacing: theme.space12

        Repeater {
            model: ["♥", "◯", "↗"]
            delegate: Label {
                required property var modelData
                text: String(modelData)
                color: theme.alpha(theme.textStrong, 0.86)
                font.family: theme.fontUi
                font.pixelSize: Math.max(12, Math.min(theme.typeSection, root.height * 0.048))
            }
        }

        Item { Layout.fillWidth: true }

        AppIcon {
            name: "polygon-tool"
            color: theme.alpha(theme.textStrong, 0.86)
            Layout.preferredWidth: Math.max(14, Math.min(theme.typeSection, root.height * 0.048))
            Layout.preferredHeight: Layout.preferredWidth
            Accessible.ignored: true
        }
    }

    Rectangle {
        objectName: "timelinePlatformDeviceFrame"
        anchors.fill: parent
        visible: root.deviceCrop !== "none"
        radius: Math.max(theme.radius4, Math.min(parent.width, parent.height) * 0.045)
        color: "transparent"
        border.width: Math.max(1, theme.hairlineWidth)
        border.color: theme.alpha(theme.textStrong, 0.36)
    }

    Rectangle {
        objectName: "timelinePlatformDynamicIsland"
        visible: root.deviceCrop === "iphone"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.max(4, parent.height * 0.018)
        width: Math.max(30, parent.width * 0.24)
        height: Math.max(8, parent.height * 0.026)
        radius: height / 2
        color: theme.alpha(theme.ink950, 0.94)
    }

    Rectangle {
        objectName: "timelinePlatformAndroidCamera"
        visible: root.deviceCrop === "android"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.max(5, parent.height * 0.022)
        width: Math.max(7, parent.height * 0.022)
        height: width
        radius: width / 2
        color: theme.alpha(theme.ink950, 0.94)
    }
}
