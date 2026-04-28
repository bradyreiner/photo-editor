import QtQuick

QtObject {
    property bool darkMode: typeof appShell !== "undefined" ? appShell.darkMode : true

    readonly property string fontDisplay: Qt.platform.os === "osx" ? "SF Pro Display" : "Noto Sans"
    readonly property string fontUi: Qt.platform.os === "osx" ? "SF Pro Text" : "Noto Sans"
    readonly property string fontMono: "Menlo"

    readonly property color ink950: darkMode ? "#050505" : "#fbfaf7"
    readonly property color ink900: darkMode ? "#090a0b" : "#f3f1ec"
    readonly property color ink850: darkMode ? "#0e1012" : "#e9e5dc"
    readonly property color ink800: darkMode ? "#141619" : "#ddd7cd"
    readonly property color ink750: darkMode ? "#1d2024" : "#cfc7ba"
    readonly property color ink700: darkMode ? "#292d32" : "#bfb5a7"
    readonly property color lineStrong: darkMode ? "#60646b" : "#aaa094"
    readonly property color lineSoft: darkMode ? "#2c3036" : "#d0c6b8"

    readonly property color textStrong: darkMode ? "#f7f4ee" : "#171512"
    readonly property color textMain: darkMode ? "#e4ded4" : "#302b25"
    readonly property color textMuted: darkMode ? "#aeb4bd" : "#665d54"
    readonly property color textFaint: darkMode ? "#717b88" : "#8a8075"

    readonly property color accentWarm500: "#b8c3cf"
    readonly property color accentWarm400: "#d7dde4"
    readonly property color accentWarm300: "#f4f7fa"
    readonly property color accentWarmFill: darkMode ? "#171d25" : "#edf1f4"

    readonly property color accentCool500: "#7f8c9a"
    readonly property color accentCool400: "#b8c3cf"
    readonly property color accentCool300: "#f4f7fa"
    readonly property color accentCoolFill: darkMode ? "#141a22" : "#edf1f4"

    readonly property color success: "#7bd88f"
    readonly property color warning: "#d4a15f"
    readonly property color error: "#d76c78"
    readonly property color focusRing: darkMode ? "#f4f7fa" : "#4b5662"
    readonly property color focusFill: darkMode ? "#1c242e" : "#e5e9ed"

    readonly property color glassBase: darkMode ? "#0d0f11" : "#f6f1ea"
    readonly property color glassLift: darkMode ? "#17191c" : "#ffffff"
    readonly property color glassEdge: darkMode ? "#5d6066" : "#d5cbbc"
    readonly property color polar: "#f4f7fa"
    readonly property color ember: accentWarm400
    readonly property int cutCorner: 7

    readonly property int space4: 4
    readonly property int space5: 5
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
    readonly property int radius4: 1
    readonly property int radius6: 2
    readonly property int radius8: 2
    readonly property int radius10: 2
    readonly property int radius12: 3
    readonly property int radius16: 3

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
    readonly property int radiusPanel: 0
    readonly property int radiusSurface: 0

    readonly property int durationFast: 110
    readonly property int durationMedium: 180
    readonly property int durationSlow: 240

    function alpha(color, opacity) {
        return Qt.rgba(color.r, color.g, color.b, opacity);
    }
}
