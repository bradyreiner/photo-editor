import QtQuick

QtObject {
    property bool darkMode: typeof appShell !== "undefined" ? appShell.darkMode : true

    readonly property string fontDisplay: Qt.platform.os === "osx" ? "Helvetica Neue" : "Noto Sans"
    readonly property string fontUi: Qt.platform.os === "osx" ? "Helvetica Neue" : "Noto Sans"
    readonly property string fontMono: Qt.platform.os === "osx" ? "Menlo" : "Noto Sans Mono"

    readonly property color ink950: darkMode ? "#0A0A0C" : "#f7f6f2"
    readonly property color ink900: darkMode ? "#0D0E10" : "#efede8"
    readonly property color ink850: darkMode ? "#111315" : "#e4e0d8"
    readonly property color ink800: darkMode ? "#141416" : "#d8d1c7"
    readonly property color ink750: darkMode ? "#1E1F22" : "#c8beb0"
    readonly property color ink700: darkMode ? "#2A2C30" : "#b8ad9e"
    readonly property color lineStrong: darkMode ? "#4C5055" : "#9c9388"
    readonly property color lineSoft: darkMode ? "#353638" : "#c8beb0"

    readonly property color textStrong: darkMode ? "#E6E6E3" : "#121211"
    readonly property color textMain: darkMode ? "#C8CBCB" : "#282520"
    readonly property color textMuted: darkMode ? "#8F949B" : "#645d54"
    readonly property color textFaint: darkMode ? "#565D66" : "#8b8177"

    readonly property color accentWarm500: "#18C99D"
    readonly property color accentWarm400: "#27E2B3"
    readonly property color accentWarm300: "#75F0D0"
    readonly property color accentWarmFill: darkMode ? "#092A22" : "#d9f6e9"

    readonly property color accentCool500: "#00A7D8"
    readonly property color accentCool400: "#00C2FF"
    readonly property color accentCool300: "#7FE2FF"
    readonly property color accentCoolFill: darkMode ? "#062230" : "#ddf5fb"

    readonly property color success: "#27E2B3"
    readonly property color warning: "#FFB000"
    readonly property color error: "#FF3830"
    readonly property color focusRing: "#27E2B3"
    readonly property color focusFill: darkMode ? "#0D3028" : "#d9f6e9"

    readonly property color glassBase: darkMode ? "#0D0E10" : "#f2eee7"
    readonly property color glassLift: darkMode ? "#17191C" : "#ffffff"
    readonly property color glassEdge: darkMode ? "#353638" : "#c9c0b4"
    readonly property color polar: "#E6E6E3"
    readonly property color ember: accentWarm400
    readonly property color vectorGreen: "#27E2B3"
    readonly property color signalAmber: "#FFB000"
    readonly property color signalCyan: "#00C2FF"
    readonly property color signalRed: "#FF3830"
    readonly property int cutCorner: 6

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
    readonly property int radius4: 5
    readonly property int radius6: 7
    readonly property int radius8: 9
    readonly property int radius10: 10
    readonly property int radius12: 12
    readonly property int radius16: 16

    readonly property int controlHeightDense: 26
    readonly property int controlHeight: 30
    readonly property int controlHeightTall: 34
    readonly property int controlHeightStandard: 40
    readonly property int controlHeightPrimary: 44
    readonly property int iconButtonSizeCompact: 32
    readonly property int iconButtonSize: 36
    readonly property int railButtonSize: 44
    readonly property int railButtonSizeCompact: 36
    readonly property int navHeight: 52
    readonly property int navItemMinWidth: 74
    readonly property int navUnderlineHeight: 2
    readonly property int sidebarRowHeight: 40
    readonly property int sidebarRowHeightCompact: 34
    readonly property int panelPadding: 12
    readonly property int panelPaddingCompact: 8
    readonly property int panelPaddingStandard: 20
    readonly property int sectionPadding: 10
    readonly property int sectionPaddingCompact: 8
    readonly property int sectionPaddingStandard: 16
    readonly property int gapDense: 6
    readonly property int gapControl: 7
    readonly property int gapPanel: 9
    readonly property int gapRelated: 8
    readonly property int gapUnrelated: 16
    readonly property int labelMinWidth: 96
    readonly property int valueColumnWidth: 58
    readonly property int menuMinWidth: 180
    readonly property int menuMaxWidth: 320
    readonly property int menuItemHeight: 34
    readonly property int radiusControl: radius6
    readonly property int radiusChip: radius6
    readonly property int radiusPanel: radius10
    readonly property int radiusSurface: radius8
    readonly property int chromeBorderWidth: 0

    readonly property int durationFast: 110
    readonly property int durationMedium: 180
    readonly property int durationSlow: 240

    function alpha(color, opacity) {
        if (color === undefined || color === null)
            return Qt.rgba(0, 0, 0, opacity);
        return Qt.rgba(color.r, color.g, color.b, opacity);
    }
}
