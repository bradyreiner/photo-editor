import QtQuick

QtObject {
    // Looksy keeps its editor chrome nearly monochrome so image color stays
    // trustworthy. RGB light is reserved for the identity mark and agent.
    readonly property bool darkMode: true
    readonly property real interfaceScale: typeof appShell !== "undefined" ? Number(appShell.interfaceScale || 1.0) : 1.0
    readonly property bool reducedMotion: typeof appShell !== "undefined" ? Boolean(appShell.reducedMotion) : false
    readonly property bool highContrast: typeof appShell !== "undefined" ? Boolean(appShell.highContrast) : false
    readonly property string colorVisionMode: typeof appShell !== "undefined" ? String(appShell.colorVisionMode || "standard") : "standard"
    readonly property string accessibilityContract: "accessibility-theme:v3|appearance:dark-only|scale:0.9-2.0|contrast:standard-high|motion:standard-reduced|color-vision:standard-protanopia-deuteranopia-tritanopia-grayscale|focus:2px-min|targets:44px-primary|text:10px-min-dense"

    readonly property string fontDisplay: fontUi
    readonly property string fontUi: typeof appShell !== "undefined" && appShell.systemUiFontFamily
                                     ? appShell.systemUiFontFamily : "Helvetica Neue"
    readonly property string fontMono: Qt.platform.os === "osx" ? "Menlo" : "Noto Sans Mono"

    // Looksy identity palette: deep graphite, cool white, and additive RGB.
    readonly property color brandInk: "#050505"
    readonly property color brandPaper: "#F4F6F8"
    readonly property color brandRed: "#FF3B30"
    readonly property color brandGreen: "#30D158"
    readonly property color brandBlue: "#3282FF"
    readonly property color brandPanel: "#0D0D0D"

    // Optical instrument palette: cool light defines active edges while large
    // surfaces and the photographic surround remain neutral.
    readonly property color interaction500: "#2467CF"
    readonly property color interaction400: brandBlue
    readonly property color interaction300: "#8AB8FF"
    readonly property color interactionFill: "#17243A"
    // Display-space registration primitives, reserved for brief completion motion.
    readonly property color lightRed: brandRed
    readonly property color lightGreen: brandGreen
    readonly property color lightBlue: brandBlue
    readonly property color processCyan: "#00FFFF"
    readonly property color processMagenta: "#FF00FF"
    readonly property color processYellow: "#FFFF00"
    readonly property color spectralViolet: "#AD97FA"
    readonly property color spectralMagenta: "#ED98D3"
    readonly property color surfaceTop: "#0D0D0D"
    readonly property color surfaceBottom: "#0D0D0D"
    readonly property color surfaceInset: "#050505"
    readonly property color controlEdge: "#2E2E2E"
    readonly property color actionPrimaryFill: brandPaper
    readonly property color actionPrimaryHover: "#FFFFFF"
    readonly property color actionPrimaryPressed: "#DDE0E5"
    readonly property color actionPrimaryText: brandInk
    readonly property color fieldFill: ink850
    readonly property color fieldHoverFill: ink800
    readonly property string darkPaletteContract: "looksy-dark-palette:v6|surfaces:near-black-neutral|controls:graphite-steps|interaction:rgb-blue|brand:rgb-additive|media-well:neutral-black|signals:functional"

    // Shared application type scale. Dense editor surfaces should use these
    // semantic steps instead of introducing workspace-specific pixel sizes.
    readonly property int typeMicro: scaled(9)
    readonly property int typeCaption: scaled(11)
    readonly property int typeLabel: scaled(12)
    readonly property int typeBody: scaled(12)
    readonly property int typeControl: scaled(12)
    readonly property int typeSection: scaled(14)
    readonly property int typeTitle: scaled(14)
    readonly property int typeDialogTitle: scaled(15)
    readonly property int typeHero: scaled(16)

    readonly property color ink950: "#050505"
    readonly property color ink900: "#0D0D0D"
    readonly property color ink850: "#171717"
    readonly property color ink800: "#202020"
    readonly property color ink750: "#2A2A2A"
    readonly property color ink700: "#363636"
    readonly property color lineStrong: highContrast ? "#F1F3F4" : "#484848"
    readonly property color lineSoft: highContrast ? "#AEB5BC" : "#303030"

    readonly property color textStrong: brandPaper
    readonly property color textMain: highContrast ? "#FFFFFF" : "#E0E2E7"
    readonly property color textMuted: highContrast ? "#ECEEF1" : "#9C9CAF"
    readonly property color textFaint: highContrast ? "#CDD1D8" : "#747985"

    // Interaction roles stay distinguishable by both hue and shape/text cues.
    // Cool optical light defines actions; selection, keyboard focus, markers,
    // audio routing, warnings, and errors each have their own semantic token.
    readonly property color primaryAccent: brandBlue
    readonly property color selectionAccent: brandBlue
    readonly property color focusAccent: brandPaper
    readonly property color markerAccent: "#AEB4C0"
    readonly property color audioAccent: brandGreen
    readonly property color warningAccent: warning
    readonly property color errorAccent: error
    readonly property string semanticAccentContract: "semantic-accents:v3|primary:rgb-blue|selection:rgb-blue|focus:paper|marker:neutral|audio:rgb-green|warning:amber|error:rgb-red|non-color-cues:required"
    readonly property string darkContrastContract: "dark-contrast:v2|normal-text-target:4.5|control-target:3|focus-width:2|composited-surface-review:required"

    // Compatibility aliases route existing workspaces through the same roles. They
    // identify selection and primary actions without tinting the whole UI.
    readonly property color accentWarm500: interaction500
    readonly property color accentWarm400: interaction400
    readonly property color accentWarm300: interaction300
    readonly property color accentWarmFill: interactionFill

    readonly property color accentCool500: "#2467CF"
    readonly property color accentCool400: "#8AB8FF"
    readonly property color accentCool300: "#D6E6FF"
    readonly property color accentCoolFill: "#17243A"

    readonly property color success: brandGreen
    readonly property color warning: "#F6C945"
    readonly property color error: brandRed
    readonly property color focusRing: highContrast ? (darkMode ? "#FFFFFF" : "#000000") : focusAccent
    readonly property color focusFill: "#20242C"
    readonly property string interactionStateContract: "interaction-states:v1|rest:transparent-or-surface|hover:editor-control-hover|pressed:accent-stronger|focus:2px-focus-ring|disabled:muted-stable|destructive:error-hue-plus-label|selected:accent-fill-plus-edge|motion:theme-duration-fast|no-color-only:true"

    readonly property color glassBase: ink900
    readonly property color glassLift: ink800
    readonly property color glassEdge: lineStrong
    readonly property color editorControlIdle: ink850
    readonly property color editorControlHover: ink750
    readonly property color editorSelectionFill: "#17243A"
    readonly property color editorSelectionEdge: selectionAccent
    // Media wells stay neutral in both themes so image and video contrast is
    // stable; surrounding controls continue to follow the active theme.
    readonly property color mediaCanvas: "#050505"
    readonly property color polar: brandPaper
    readonly property color ember: warning
    readonly property color vectorGreen: success
    readonly property color signalAmber: warning
    readonly property color signalCyan: "#63BBD0"
    readonly property color signalRed: error
    readonly property int cutCorner: 6

    readonly property int space2: scaled(2)
    readonly property int space3: scaled(3)
    readonly property int space4: scaled(4)
    readonly property int space5: scaled(5)
    readonly property int space6: scaled(6)
    readonly property int space8: scaled(8)
    readonly property int space10: scaled(10)
    readonly property int space12: scaled(12)
    readonly property int space14: scaled(14)
    readonly property int space16: scaled(16)
    readonly property int space18: scaled(18)
    readonly property int space20: scaled(20)
    readonly property int space24: scaled(24)
    readonly property int space28: scaled(28)
    readonly property int radius4: scaled(4)
    readonly property int radius6: scaled(6)
    readonly property int radius8: scaled(8)
    readonly property int radius10: scaled(10)
    readonly property int radius12: scaled(12)
    readonly property int radius16: scaled(16)

    readonly property int controlHeightDense: scaled(26)
    readonly property int controlHeight: scaled(32)
    readonly property int controlHeightTall: scaled(34)
    readonly property int controlHeightStandard: scaled(40)
    readonly property int controlHeightPrimary: scaled(44)
    readonly property int iconButtonSizeCompact: scaled(32)
    readonly property int iconButtonSize: scaled(36)
    readonly property int railButtonSize: scaled(44)
    readonly property int railButtonSizeCompact: scaled(36)
    readonly property int navHeight: scaled(52)
    readonly property int navItemMinWidth: scaled(74)
    readonly property int navUnderlineHeight: 2
    readonly property int sidebarRowHeight: scaled(40)
    readonly property int sidebarRowHeightCompact: scaled(34)
    readonly property int panelPadding: scaled(12)
    readonly property int panelPaddingCompact: scaled(8)
    readonly property int panelPaddingStandard: scaled(20)
    readonly property int sectionPadding: scaled(10)
    readonly property int sectionPaddingCompact: scaled(8)
    readonly property int sectionPaddingStandard: scaled(16)
    readonly property int gapDense: scaled(6)
    readonly property int gapControl: scaled(8)
    readonly property int gapPanel: scaled(8)
    readonly property int gapRelated: scaled(8)
    readonly property int gapUnrelated: scaled(16)
    readonly property int labelMinWidth: scaled(96)
    readonly property int valueColumnWidth: scaled(58)
    readonly property int menuMinWidth: scaled(180)
    readonly property int menuMaxWidth: scaled(320)
    readonly property int menuItemHeight: scaled(34)
    readonly property int radiusControl: scaled(3)
    readonly property int radiusChip: radiusControl
    readonly property int radiusPanel: radius8
    readonly property int radiusSurface: radius8
    readonly property int chromeBorderWidth: 0
    readonly property int hairlineWidth: 1
    readonly property int focusBorderWidth: Math.max(2, scaled(2))

    readonly property int durationFast: reducedMotion ? 0 : 110
    readonly property int durationMedium: reducedMotion ? 0 : 180
    readonly property int durationSlow: reducedMotion ? 0 : 240

    function scaled(value) {
        return Math.max(1, Math.round(Number(value) * interfaceScale))
    }

    function selectionPalette(operation) {
        const key = String(operation || "add").toLowerCase()
        if (colorVisionMode === "grayscale")
            return key === "subtract" ? "#FFFFFF" : (key === "intersect" ? "#777777" : "#111111")
        if (colorVisionMode === "protanopia" || colorVisionMode === "deuteranopia")
            return key === "subtract" ? "#F0E442" : (key === "intersect" ? "#CC79A7" : "#0072B2")
        if (colorVisionMode === "tritanopia")
            return key === "subtract" ? "#56B4E9" : (key === "intersect" ? "#FFFFFF" : "#D55E00")
        return key === "subtract" ? "#F0E442" : (key === "intersect" ? "#CC79A7" : "#0072B2")
    }

    // Display-space material blends for UI chrome, never for photo pixels.
    // Screen adds a luminous intersection; multiply creates an ink-like edge.
    function opticalLight(a, b) {
        return Qt.rgba(1 - (1 - a.r) * (1 - b.r),
                       1 - (1 - a.g) * (1 - b.g),
                       1 - (1 - a.b) * (1 - b.b), 1)
    }

    function opticalInk(a, b) {
        return Qt.rgba(a.r * b.r, a.g * b.g, a.b * b.b, 1)
    }

    function alpha(color, opacity) {
        if (color === undefined || color === null)
            return Qt.rgba(0, 0, 0, opacity);
        return Qt.rgba(color.r, color.g, color.b, opacity);
    }
}
