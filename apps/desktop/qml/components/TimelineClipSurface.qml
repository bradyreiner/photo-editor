import QtQuick

// Editing chrome stays readable even when the authored image is transparent.
// Media identity lives in the content; the body stays quiet and borderless.
Rectangle {
    id: root
    required property var theme
    property string mediaType: "video"
    property bool selected: false
    property bool hovered: false
    property bool searchHighlighted: false
    readonly property color materialAccent: mediaType === "audio" ? "#8bb6a0"
            : mediaType === "title" || mediaType === "caption" ? "#b5a3cf"
            : mediaType === "photo" ? "#8dbbb7"
            : mediaType === "shape" || mediaType === "generator" || mediaType === "motion-generator" ? "#c5b28b"
            : mediaType === "adjustment" ? "#a4abc9" : "#8faac4"
    readonly property real labelRailHeight: Math.min(height, theme.scaled(20))
    readonly property color materialFill: mediaType === "audio" ? "#29463e"
            : mediaType === "title" || mediaType === "caption" ? "#443652"
            : mediaType === "photo" ? "#27464b"
            : mediaType === "shape" || mediaType === "generator" || mediaType === "motion-generator" ? "#4b4030"
            : mediaType === "adjustment" ? "#333b52" : "#2b3c50"

    radius: theme.scaled(3)
    color: Qt.tint(materialFill, theme.alpha(materialAccent,
        selected ? 0.20 : hovered ? 0.10 : 0))
    // Selection and hover chrome are drawn by the parent overlay. Keep only
    // unselected search matches outlined so normal rows do not stack borders.
    border.width: searchHighlighted && !selected && !hovered ? theme.hairlineWidth : 0
    border.color: searchHighlighted && !selected && !hovered
            ? theme.accentWarm300 : "transparent"

}
