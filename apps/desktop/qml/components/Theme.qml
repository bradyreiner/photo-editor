import QtQuick

QtObject {
    readonly property string fontDisplay: "Avenir Next Condensed"
    readonly property string fontUi: "Avenir Next"
    readonly property string fontMono: "Menlo"

    readonly property color ink950: "#0c0c0d"
    readonly property color ink900: "#111214"
    readonly property color ink850: "#161719"
    readonly property color ink800: "#1c1d20"
    readonly property color ink750: "#232427"
    readonly property color ink700: "#2d2f33"
    readonly property color lineStrong: "#38342f"
    readonly property color lineSoft: "#252320"

    readonly property color textStrong: "#efede8"
    readonly property color textMain: "#d8d2ca"
    readonly property color textMuted: "#9f968b"
    readonly property color textFaint: "#746c62"

    readonly property color accentWarm500: "#b57a60"
    readonly property color accentWarm400: "#c89078"
    readonly property color accentWarm300: "#dfb8a8"
    readonly property color accentWarmFill: "#2f221d"

    readonly property color accentCool500: "#829888"
    readonly property color accentCool400: "#9cae9f"
    readonly property color accentCool300: "#c2cfc4"
    readonly property color accentCoolFill: "#1b2520"

    readonly property color success: "#7fa87c"
    readonly property color warning: "#b98f54"
    readonly property color error: "#a56a5e"

    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space20: 20
    readonly property int space24: 24
    readonly property int radius4: 3
    readonly property int radius8: 6
    readonly property int radius12: 8
    readonly property int radius16: 10

    readonly property int durationFast: 110
    readonly property int durationMedium: 180
    readonly property int durationSlow: 240

    function alpha(color, opacity) {
        return Qt.rgba(color.r, color.g, color.b, opacity);
    }
}
