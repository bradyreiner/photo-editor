import QtQuick

QtObject {
    // Optical studio: neutral photographic surroundings, precise controls,
    // and small moments of colored light in interaction and feedback.
    readonly property bool darkMode: true
    readonly property real interfaceScale: typeof appShell !== "undefined" ? Number(appShell.interfaceScale || 1.0) : 1.0
    readonly property string interfaceDensity: {
        const requested = typeof appShell !== "undefined" ? String(appShell.interfaceDensity || "balanced") : "balanced"
        if (requested === "comfortable" || requested === "inspect") return "comfortable"
        if (requested === "compact" || requested === "clean") return "compact"
        if (requested === "balanced" || requested === "pro") return "balanced"
        return "balanced"
    }
    readonly property bool densityComfortable: interfaceDensity === "comfortable"
    readonly property bool densityBalanced: interfaceDensity === "balanced"
    readonly property bool densityCompact: interfaceDensity === "compact"
    readonly property bool reducedMotion: typeof appShell !== "undefined" ? Boolean(appShell.reducedMotion) : false
    readonly property bool highContrast: typeof appShell !== "undefined" ? Boolean(appShell.highContrast) : false
    readonly property string colorVisionMode: typeof appShell !== "undefined" ? String(appShell.colorVisionMode || "standard") : "standard"
    readonly property string accessibilityContract: "accessibility-theme:v4|appearance:dark-only|scale:0.9-2.0|contrast:standard-high|motion:standard-reduced|color-vision:standard-protanopia-deuteranopia-tritanopia-grayscale|focus:2px-min|targets:density-scaled-desktop|text:10px-min-dense"

    readonly property string fontDisplay: fontUi
    readonly property string fontUi: typeof appShell !== "undefined" && appShell.systemUiFontFamily
                                     ? appShell.systemUiFontFamily : Qt.font({ pixelSize: typeControl }).family
    readonly property string fontMono: Qt.platform.os === "osx" ? "Menlo" : "Noto Sans Mono"

    // Looksy identity palette: deep graphite, cool white, and additive RGB.
    readonly property color brandInk: "#050505"
    readonly property color brandPaper: "#F4F6F8"
    readonly property color brandRed: "#FF3B30"
    readonly property color brandGreen: "#30D158"
    readonly property color brandBlue: "#3282FF"
    readonly property color brandCoral: "#FF6254"
    readonly property color brandPanel: ink900
    readonly property color opticalLight: "#91D9EA"
    readonly property color opticalHoverFill: actionSecondaryHover
    readonly property color opticalSelectedFill: editorSelectionFill
    readonly property color opticalEdge: highContrast ? brandPaper : "#607B88"
    readonly property color popupFill: "#25292E"
    readonly property color warningFill: "#302B23"
    readonly property color errorFill: "#342626"
    readonly property int opticalHoverDuration: reducedMotion ? 0 : 120
    readonly property int opticalOpenDuration: reducedMotion ? 0 : 150
    readonly property string opticalStudioContract: "optical-studio:v2|character:friendly-technical|geometry:sharp-compact|hover:subtle-neutral|press:inset|focus:paper|activity:line-character|warnings:static-symbol|media:neutral"

    // Blue identifies selection in small marks and icons; command surfaces
    // remain graphite, including primary actions such as Import.
    readonly property color interaction500: "#6486AE"
    readonly property color interaction400: "#91B4DE"
    readonly property color interaction300: "#B5CDE9"
    readonly property color interactionFill: "#272F39"
    // Display-space registration primitives, reserved for brief completion motion.
    readonly property color lightRed: brandRed
    readonly property color lightGreen: brandGreen
    readonly property color lightBlue: brandBlue
    readonly property color processCyan: "#00FFFF"
    readonly property color processMagenta: "#FF00FF"
    readonly property color processYellow: "#FFFF00"
    readonly property color spectralViolet: "#AD97FA"
    readonly property color spectralMagenta: "#ED98D3"
    readonly property color surfaceTop: ink900
    readonly property color surfaceBottom: ink900
    readonly property color surfaceInset: "#050505"
    readonly property color controlEdge: highContrast ? "#F1F3F4" : "#41444A"
    readonly property color actionPrimaryFill: "#35383D"
    readonly property color actionPrimaryHover: "#3D4045"
    readonly property color actionPrimaryPressed: "#292C31"
    readonly property color actionSecondaryFill: ink850
    readonly property color actionSecondaryHover: "#2D3035"
    readonly property color actionSecondaryPressed: "#202226"
    readonly property color actionPrimaryText: brandPaper
    readonly property color fieldFill: ink850
    readonly property color fieldHoverFill: ink800
    readonly property string darkPaletteContract: "looksy-dark-palette:v8|surfaces:graphite-neutral|controls:graphite-steps|interaction:muted-blue-marks|primary:graphite|brand:rgb-additive|media-well:neutral-black|signals:functional"

    // Shared application type scale. Dense editor surfaces should use these
    // semantic steps instead of introducing workspace-specific pixel sizes.
    readonly property int typeMicro: scaled(11)
    readonly property int typeCaption: scaled(12)
    readonly property int typeLabel: scaled(13)
    readonly property int typeBody: scaled(13)
    readonly property int typeControl: scaled(13)
    readonly property int typeSection: scaled(14)
    readonly property int typeTitle: scaled(16)
    readonly property int typeDialogTitle: scaled(18)
    readonly property int typeHero: scaled(20)
    readonly property int typePageTitle: scaled(26)
    readonly property int weightControl: Font.Medium
    readonly property int weightSection: Font.DemiBold
    readonly property string typographyContract: "looksy-typography:v1|family:platform-ui|body:13|caption:12|micro:11|controls:medium|headings:semibold|numeric:aligned|scale:shared"

    readonly property color ink950: "#111214"
    readonly property color ink900: "#191A1D"
    readonly property color ink850: "#24262A"
    readonly property color ink800: "#2C2F34"
    readonly property color ink750: "#373B42"
    readonly property color ink700: "#464C56"
    readonly property color lineStrong: highContrast ? "#F1F3F4" : "#525760"
    readonly property color lineSoft: highContrast ? "#AEB5BC" : "#353940"

    readonly property color textStrong: brandPaper
    readonly property color textMain: highContrast ? "#FFFFFF" : "#E0E2E7"
    readonly property color textMuted: highContrast ? "#ECEEF1" : "#B0B5BF"
    readonly property color textFaint: highContrast ? "#CDD1D8" : "#9299A5"

    // Interaction roles stay distinguishable by both hue and shape/text cues.
    // Cool optical light defines actions; selection, keyboard focus, markers,
    // audio routing, warnings, and errors each have their own semantic token.
    readonly property color primaryAccent: interaction400
    readonly property color selectionAccent: highContrast ? interaction300 : interaction400
    readonly property color focusAccent: brandPaper
    readonly property color markerAccent: "#AEB4C0"
    readonly property color audioAccent: brandGreen
    readonly property color warningAccent: warning
    readonly property color errorAccent: error
    readonly property string semanticAccentContract: "semantic-accents:v4|primary:graphite|selection:muted-blue|focus:paper|marker:neutral|audio:rgb-green|warning:amber|error:rgb-red|non-color-cues:required"
    readonly property string darkContrastContract: "dark-contrast:v2|normal-text-target:4.5|control-target:3|focus-width:2|composited-surface-review:required"

    // Compatibility aliases route existing workspaces through the same roles. They
    // identify selection and primary actions without tinting the whole UI.
    readonly property color accentWarm500: interaction500
    readonly property color accentWarm400: interaction400
    readonly property color accentWarm300: interaction300
    readonly property color accentWarmFill: interactionFill

    readonly property color accentCool500: interaction500
    readonly property color accentCool400: interaction400
    readonly property color accentCool300: interaction300
    readonly property color accentCoolFill: interactionFill

    readonly property color success: brandGreen
    readonly property color warning: "#F6C945"
    readonly property color error: brandRed
    readonly property color focusRing: highContrast ? (darkMode ? "#FFFFFF" : "#000000") : focusAccent
    readonly property color focusFill: "#20242C"
    readonly property string interactionStateContract: "interaction-states:v2|rest:transparent-or-graphite|hover:subtle-neutral-fill|pressed:inset-neutral|focus:2px-focus-ring|disabled:muted-stable|destructive:error-hue-plus-label|selected:subtle-fill-and-icon-or-weight|motion:theme-duration-fast|no-color-only:true"

    readonly property color glassBase: ink900
    readonly property color glassLift: ink800
    readonly property color glassEdge: lineStrong
    readonly property color editorControlIdle: ink850
    readonly property color editorControlHover: highContrast ? "#5C5C5C" : actionSecondaryHover
    readonly property color editorSelectionFill: highContrast ? "#30343A" : interactionFill
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

    readonly property int controlHeightDense: scaled(densityComfortable ? 30 : densityCompact ? 26 : 28)
    readonly property int controlHeight: scaled(densityComfortable ? 34 : densityCompact ? 28 : 30)
    readonly property int controlHeightTall: scaled(densityComfortable ? 36 : densityCompact ? 30 : 32)
    readonly property int controlHeightStandard: scaled(densityComfortable ? 40 : densityCompact ? 34 : 36)
    readonly property int controlHeightPrimary: scaled(densityComfortable ? 36 : densityCompact ? 30 : 32)
    readonly property int iconButtonSizeCompact: scaled(densityComfortable ? 32 : densityCompact ? 26 : 28)
    readonly property int iconButtonSize: scaled(densityComfortable ? 36 : densityCompact ? 30 : 32)
    readonly property int railButtonSize: scaled(densityComfortable ? 40 : densityCompact ? 34 : 36)
    readonly property int railButtonSizeCompact: scaled(densityComfortable ? 34 : densityCompact ? 28 : 30)
    readonly property int navHeight: scaled(densityComfortable ? 50 : densityCompact ? 40 : 44)
    readonly property int navItemMinWidth: scaled(densityComfortable ? 80 : densityCompact ? 68 : 74)
    readonly property int navUnderlineHeight: 2
    readonly property int sidebarRowHeight: scaled(densityComfortable ? 38 : densityCompact ? 30 : 34)
    readonly property int sidebarRowHeightCompact: scaled(densityComfortable ? 34 : densityCompact ? 28 : 30)
    readonly property int panelPadding: scaled(densityComfortable ? 16 : densityCompact ? 8 : 12)
    readonly property int panelPaddingCompact: scaled(densityComfortable ? 10 : densityCompact ? 6 : 8)
    readonly property int panelPaddingStandard: scaled(densityComfortable ? 24 : densityCompact ? 16 : 20)
    readonly property int sectionPadding: scaled(densityComfortable ? 12 : densityCompact ? 8 : 10)
    readonly property int sectionPaddingCompact: scaled(densityComfortable ? 10 : densityCompact ? 6 : 8)
    readonly property int sectionPaddingStandard: scaled(densityComfortable ? 18 : densityCompact ? 12 : 16)
    readonly property int gapDense: scaled(densityComfortable ? 8 : densityCompact ? 4 : 6)
    readonly property int gapControl: scaled(densityComfortable ? 10 : densityCompact ? 6 : 8)
    readonly property int gapPanel: scaled(densityComfortable ? 10 : densityCompact ? 6 : 8)
    readonly property int gapRelated: scaled(densityComfortable ? 10 : densityCompact ? 6 : 8)
    readonly property int gapUnrelated: scaled(densityComfortable ? 20 : densityCompact ? 12 : 16)
    readonly property int labelMinWidth: scaled(96)
    readonly property int valueColumnWidth: scaled(58)
    readonly property int menuMinWidth: scaled(180)
    readonly property int menuMaxWidth: scaled(320)
    readonly property int menuItemHeight: scaled(densityComfortable ? 34 : densityCompact ? 28 : 30)
    readonly property int radiusControl: scaled(3)
    readonly property int radiusChip: radiusControl
    readonly property int radiusPanel: radius4
    readonly property int radiusSurface: radius4
    readonly property int chromeBorderWidth: 0
    readonly property int hairlineWidth: 1
    readonly property int focusBorderWidth: Math.max(2, scaled(2))

    readonly property int durationFast: reducedMotion ? 0 : 110
    readonly property int durationMedium: reducedMotion ? 0 : 180
    readonly property int durationSlow: reducedMotion ? 0 : 240

    // Shared state ordering keeps compact, toolbar, menu and named actions
    // consistent. Focus is drawn separately and never changes the fill.
    function commandFill(tone, selected, hovered, pressed, enabled, quiet) {
        if (!enabled)
            return quiet ? "transparent" : alpha(fieldFill, 0.55)
        if (pressed)
            return tone === "primary" ? actionPrimaryPressed : actionSecondaryPressed
        if (tone === "danger")
            return alpha(error, hovered ? 0.12 : quiet ? 0 : 0.06)
        if (selected)
            return hovered ? (highContrast ? editorControlHover : "#303944") : editorSelectionFill
        if (tone === "primary")
            return hovered ? actionPrimaryHover : actionPrimaryFill
        if (hovered)
            return highContrast ? editorControlHover : actionSecondaryHover
        return quiet ? "transparent" : actionSecondaryFill
    }

    function commandBorderWidth(keyboardFocus) {
        return keyboardFocus ? focusBorderWidth : highContrast ? hairlineWidth : 0
    }

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
