import QtQuick

Item {
    id: root

    property color foreground: theme.brandPaper
    property color backgroundColor: theme.ink950
    property color accent: theme.brandBlue
    property bool framed: false
    property url orbSource: "qrc:/photo-editor/brand/looksy-rgb-orb.png"

    implicitWidth: 32
    implicitHeight: 32
    baselineOffset: height * 0.62
    Accessible.role: Accessible.Graphic
    Accessible.name: "Looksy luminous RGB mark"
    readonly property string markContract:
        "looksy-mark:v4|asset:period-crop|source:provided-looksy-logo|render:exact-rgb-light"

    Theme { id: theme }

    Rectangle {
        visible: root.framed
        anchors.fill: parent
        radius: Math.min(width, height) * 0.22
        color: root.backgroundColor
    }

    Image {
        anchors.fill: parent
        source: root.orbSource
        sourceSize: Qt.size(1024, 1024)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: true
    }
}
