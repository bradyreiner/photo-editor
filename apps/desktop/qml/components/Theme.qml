import QtQuick

QtObject {
    property bool darkMode: typeof appShell !== "undefined" ? appShell.darkMode : true

    readonly property string fontDisplay: Qt.platform.os === "osx" ? "Helvetica Neue" : "Noto Sans"
    readonly property string fontUi: Qt.platform.os === "osx" ? "Helvetica Neue" : "Noto Sans"
    readonly property string fontMono: "Menlo"

    readonly property color ink950: darkMode ? "#0c0c0d" : "#ffffff"
    readonly property color ink900: darkMode ? "#111214" : "#f7f6f4"
    readonly property color ink850: darkMode ? "#161719" : "#efede8"
    readonly property color ink800: darkMode ? "#1c1d20" : "#e5e2dc"
    readonly property color ink750: darkMode ? "#232427" : "#dad6cf"
    readonly property color ink700: darkMode ? "#2d2f33" : "#cecac2"
    readonly property color lineStrong: darkMode ? "#38342f" : "#c4bfb6"
    readonly property color lineSoft: darkMode ? "#252320" : "#d8d4cc"

    readonly property color textStrong: darkMode ? "#efede8" : "#1a1917"
    readonly property color textMain: darkMode ? "#ddd8d0" : "#33302b"
    readonly property color textMuted: darkMode ? "#b2a99e" : "#655e55"
    readonly property color textFaint: darkMode ? "#8a8177" : "#877f74"

    readonly property color accentWarm500: "#b57a60"
    readonly property color accentWarm400: "#c89078"
    readonly property color accentWarm300: "#dfb8a8"
    readonly property color accentWarmFill: darkMode ? "#2f221d" : "#f5ebe6"

    readonly property color accentCool500: "#829888"
    readonly property color accentCool400: "#9cae9f"
    readonly property color accentCool300: "#c2cfc4"
    readonly property color accentCoolFill: darkMode ? "#1b2520" : "#e8f0ea"

    readonly property color success: "#7fa87c"
    readonly property color warning: "#b98f54"
    readonly property color error: "#a56a5e"
    readonly property color focusRing: darkMode ? "#d2a364" : "#8d6338"
    readonly property color focusFill: darkMode ? "#2e2119" : "#f2e4d7"

    readonly property int space4: 4
    readonly property int space6: 6
    readonly property int space8: 8
    readonly property int space10: 10
    readonly property int space12: 12
    readonly property int space14: 14
    readonly property int space16: 16
    readonly property int space18: 18
    readonly property int space20: 20
    readonly property int space24: 24
    readonly property int space28: 28
    readonly property int radius4: 3
    readonly property int radius6: 5
    readonly property int radius8: 6
    readonly property int radius10: 7
    readonly property int radius12: 8
    readonly property int radius16: 10

    readonly property int controlHeightDense: 26
    readonly property int controlHeight: 30
    readonly property int controlHeightTall: 34
    readonly property int panelPadding: 12
    readonly property int panelPaddingCompact: 8
    readonly property int sectionPadding: 10
    readonly property int sectionPaddingCompact: 8
    readonly property int gapDense: 6
    readonly property int gapControl: 7
    readonly property int gapPanel: 9
    readonly property int radiusControl: radius6
    readonly property int radiusChip: radius6
    readonly property int radiusPanel: radius8
    readonly property int radiusSurface: radius12

    readonly property int durationFast: 110
    readonly property int durationMedium: 180
    readonly property int durationSlow: 240

    function alpha(color, opacity) {
        return Qt.rgba(color.r, color.g, color.b, opacity);
    }
}
