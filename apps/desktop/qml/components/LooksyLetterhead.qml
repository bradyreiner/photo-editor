import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property string descriptor: "AI VISUAL COMPANION"
    property string tagline: "See more. Notice more."
    property color foreground: theme.brandPaper
    property color accent: theme.primaryAccent
    property bool compact: false
    property bool animated: true
    property real reveal: theme.reducedMotion || !animated ? 1 : 0

    Theme { id: theme }

    implicitWidth: compact ? 292 : 356
    implicitHeight: compact ? 54 : 78
    Accessible.role: Accessible.Graphic
    Accessible.name: "Looksy, visual workspace"

    Component.onCompleted: {
        if (root.animated && !theme.reducedMotion)
            revealAnimation.start()
    }

    NumberAnimation {
        id: revealAnimation
        target: root
        property: "reveal"
        from: 0
        to: 1
        duration: 420
        easing.type: Easing.OutCubic
    }

    RowLayout {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.right: parent.right
        spacing: 0
        opacity: 0.4 + root.reveal * 0.6

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: root.compact ? 0 : 2

            BrandWordmark {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                foreground: root.foreground
                accent: root.accent
                compact: root.compact
                showMark: true
                showDescriptor: false
            }

            Text {
                text: root.descriptor
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: root.compact ? 7 : 9
                font.weight: Font.Bold
                font.letterSpacing: root.compact ? 1.5 : 2.1
            }

            Text {
                visible: !root.compact && root.tagline.length > 0
                text: root.tagline
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 10
                font.weight: Font.Medium
            }
        }
    }

}
