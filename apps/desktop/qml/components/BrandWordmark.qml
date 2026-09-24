import QtQuick

Item {
    id: root

    property color accent: theme.brandBlue
    property color foreground: theme.brandPaper
    property bool compact: false
    property bool showMark: true
    property bool showDescriptor: false
    property string descriptor: "AI VISUAL COMPANION"

    Theme { id: theme }

    readonly property string brandLockupContract:
        "looksy-brand-lockup:v4|source:provided-looksy-logo|lettering:exact-raster|period:additive-rgb|variants:dark-light"
    readonly property bool lightLogo: root.foreground.r < 0.5
    readonly property real logoHeight: root.compact ? 34 : 44
    readonly property real logoAspectRatio: root.showMark ? (1795 / 454) : (1473 / 454)
    readonly property url logoSource: {
        if (root.showMark)
            return root.lightLogo
                    ? "qrc:/photo-editor/brand/looksy-logo-light.png"
                    : "qrc:/photo-editor/brand/looksy-logo-dark.png"
        return root.lightLogo
                ? "qrc:/photo-editor/brand/looksy-wordmark-light.png"
                : "qrc:/photo-editor/brand/looksy-wordmark-dark.png"
    }

    implicitWidth: logoHeight * logoAspectRatio
    implicitHeight: logoHeight + (root.showDescriptor ? descriptorLabel.implicitHeight + 3 : 0)

    Image {
        id: logoImage
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.logoHeight * root.logoAspectRatio
        height: root.logoHeight
        source: root.logoSource
        sourceSize: root.showMark ? Qt.size(1795, 454) : Qt.size(1473, 454)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: true
        Accessible.ignored: true
    }

    Text {
        id: descriptorLabel
        visible: root.showDescriptor
        anchors.left: parent.left
        anchors.top: logoImage.bottom
        anchors.topMargin: 3
        text: root.descriptor
        color: theme.textMuted
        font.family: theme.fontMono
        font.pixelSize: root.compact ? 7 : 8
        font.weight: Font.Medium
        font.letterSpacing: root.compact ? 1.35 : 1.7
        verticalAlignment: Text.AlignVCenter
    }
}
