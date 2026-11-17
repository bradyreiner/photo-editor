import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: scopeDock
    required property Item workspace
    required property var previewController
    property bool large: false
    property string placement: "bottom"
    property string objectPrefix: "dock"
    readonly property bool compactPlots: placement === "bottom" && height < theme.scaled(330) && width < theme.scaled(760)
    readonly property bool hasReference: Number(workspace.scopeReferenceFrame.sampleCount || 0) > 0
    readonly property var domains: [
        {key: "final-display", label: "Display"}, {key: "working", label: "Working"},
        {key: "scene-linear", label: "Scene linear"}, {key: "pre-output", label: "Output"}
    ].concat(previewController.softProofEnabled ? [{key: "proof", label: "Soft proof"}] : [])
    spacing: theme.space6
    Theme { id: theme }

    component ScopeAction: MiniButton {
        square: true
        labelVisible: false
        role: "icon"
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.gapCommand
        StudioSectionTitle { Layout.fillWidth: true; label: "Scopes"; visible: !scopeDock.compactPlots }
        MenuButton {
            Layout.fillWidth: true
            visible: scopeDock.compactPlots
            text: scopeDock.workspace.scopeCompactKind
            compact: true
            primaryOpensMenu: true
            accessibleName: "Active scope"
            menuItems: scopeDock.workspace.scopeAnalysisModes().map(function(kind) {
                return {key: kind, label: kind, checked: scopeDock.workspace.scopeCompactKind === kind}
            })
            onMenuItemTriggered: function(key) { scopeDock.workspace.scopeCompactKind = key }
        }
        ScopeAction {
            objectName: scopeDock.objectPrefix + "ScopeFreezeButton"
            text: scopeDock.previewController.scopesFrozen ? "Resume scopes" : "Freeze scopes"
            iconName: scopeDock.previewController.scopesFrozen ? "play" : "pause"
            active: scopeDock.previewController.scopesFrozen
            tooltip: text
            onClicked: scopeDock.previewController.scopesFrozen = !scopeDock.previewController.scopesFrozen
        }
        ScopeAction {
            objectName: scopeDock.objectPrefix + "ScopeReferenceButton"
            text: scopeDock.hasReference ? "Clear scope reference" : "Hold scope reference"
            iconName: "compare"
            active: scopeDock.hasReference
            enabled: scopeDock.hasReference || Number(scopeDock.previewController.scopeFrame.sampleCount || 0) > 0
            tooltip: text
            onClicked: scopeDock.workspace.scopeReferenceFrame = scopeDock.hasReference ? ({}) : scopeDock.previewController.scopeFrame
        }
        ScopeAction {
            objectName: "largeScopesImageModeButton"
            visible: scopeDock.large
            text: "Return to image"
            iconName: "image"
            tooltip: text
            onClicked: scopeDock.workspace.setScopesViewMode("image")
        }
        ScopeAction {
            id: settingsButton
            objectName: scopeDock.objectPrefix + "ScopeSettingsButton"
            text: "Scope settings"
            iconName: "settings"
            tooltip: text
            active: scopeSettings.opened
            onClicked: scopeSettings.toggleTransient(settingsButton)
        }
    }
    Label {
        objectName: scopeDock.objectPrefix + "ScopeAsyncStatusLabel"
        Layout.fillWidth: true
        text: scopeDock.previewController.scopeDomainLabel + " · " + scopeDock.previewController.scopeRangeMode
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }
    AppPopup {
        id: scopeSettings
        objectName: scopeDock.objectPrefix + "ScopeSettingsPopup"
        parent: Overlay.overlay
        width: Math.min(popupAvailableWidth, theme.scaled(350))
        padding: theme.popupPadding
        onAboutToShow: {
            const overlay = Overlay.overlay
            const anchor = settingsButton.mapToItem(overlay, settingsButton.width, settingsButton.height)
            x = Math.max(8, Math.min(overlay.width - width - 8, anchor.x - width))
            y = anchor.y + height + 6 <= overlay.height - 8
                ? anchor.y + 6 : Math.max(8, anchor.y - settingsButton.height - height - 6)
        }
        contentItem: ScrollView {
            implicitHeight: settingsContent.implicitHeight
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: settingsContent
                width: scopeSettings.availableWidth
                spacing: theme.space8
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: theme.space8
                    rowSpacing: theme.space4
                    Label { text: "Domain"; color: theme.textMuted; font.pixelSize: theme.typeLabel }
                    ToneComboBox {
                        objectName: scopeDock.objectPrefix + "ScopeDomainControl"
                        Layout.fillWidth: true
                        accessibleLabel: "Scope analysis domain"
                        model: scopeDock.domains
                        textRole: "label"
                        currentIndex: Math.max(0, scopeDock.domains.findIndex(function(item) { return item.key === scopeDock.previewController.scopeDomain }))
                        onActivated: function(index) { scopeDock.previewController.scopeDomain = scopeDock.domains[index].key }
                    }
                    Label { text: "Range"; color: theme.textMuted; font.pixelSize: theme.typeLabel }
                    ToneComboBox {
                        objectName: scopeDock.objectPrefix + "ScopeRangeControl"
                        Layout.fillWidth: true
                        accessibleLabel: "Scope signal range"
                        model: ["0–100%", "−25–400%", "0–1600%"]
                        currentIndex: ["SDR", "Extended", "HDR"].indexOf(scopeDock.previewController.scopeRangeMode)
                        onActivated: function(index) { scopeDock.previewController.scopeRangeMode = ["SDR", "Extended", "HDR"][index] }
                    }
                    Label { text: "Quality"; color: theme.textMuted; font.pixelSize: theme.typeLabel }
                    ToneComboBox {
                        objectName: scopeDock.objectPrefix + "ScopeSamplingControl"
                        Layout.fillWidth: true
                        accessibleLabel: "Scope sampling quality"
                        model: ["Fast", "Balanced", "Precise"]
                        currentIndex: model.indexOf(scopeDock.workspace.scopeSamplingMode)
                        onActivated: function(index) { scopeDock.workspace.setScopeSamplingMode(model[index]) }
                    }
                    Label { text: "Area"; color: theme.textMuted; font.pixelSize: theme.typeLabel }
                    ToneComboBox {
                        objectName: scopeDock.objectPrefix + "ScopeRegionControl"
                        Layout.fillWidth: true
                        accessibleLabel: "Scope analysis region"
                        model: ["Full image", "Visible area"]
                        currentIndex: scopeDock.workspace.scopeRegionMode === "Visible area" ? 1 : 0
                        onActivated: scopeDock.workspace.scopeRegionMode = currentText
                    }
                }
                InspectorSlider {
                    objectName: scopeDock.objectPrefix + "ScopeDensityGain"
                    Layout.fillWidth: true
                    label: "Gain"
                    from: 0.25; to: 4; stepSize: 0.05; defaultValue: 1
                    value: scopeDock.workspace.scopeDensityGain
                    valueText: value.toFixed(2) + "×"
                    onValueDragged: function(value) { scopeDock.workspace.scopeDensityGain = value }
                    onResetRequested: scopeDock.workspace.scopeDensityGain = 1
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.gapCommand
                    MiniButton { objectName: scopeDock.objectPrefix + "ScopeRgbToggle"; text: "RGB"; role: "segmented"; active: scopeDock.workspace.scopeShowRgb; tooltip: "RGB channels"; onClicked: scopeDock.workspace.scopeShowRgb = !active }
                    MiniButton { objectName: scopeDock.objectPrefix + "ScopeLumaToggle"; text: "Y"; role: "segmented"; active: scopeDock.workspace.scopeShowLuma; tooltip: "Luminance"; a11yName: "Luminance"; onClicked: scopeDock.workspace.scopeShowLuma = !active }
                    ScopeAction { objectName: scopeDock.objectPrefix + "ScopeDensityToggle"; text: "Density"; iconName: "density"; active: scopeDock.workspace.scopeShowDensity; tooltip: text; onClicked: scopeDock.workspace.scopeShowDensity = !active }
                    ScopeAction { objectName: scopeDock.objectPrefix + "ScopeTraceToggle"; text: "Trace"; iconName: "curve"; active: scopeDock.workspace.scopeShowTrace; tooltip: text; onClicked: scopeDock.workspace.scopeShowTrace = !active }
                    ScopeAction { objectName: scopeDock.objectPrefix + "ScopeGridToggle"; text: "Grid"; iconName: "grid"; active: scopeDock.workspace.scopeShowGrid; tooltip: text; onClicked: scopeDock.workspace.scopeShowGrid = !active }
                    MiniButton { objectName: scopeDock.objectPrefix + "ScopeLogToggle"; text: "Log"; role: "segmented"; active: scopeDock.workspace.scopeLogHistogram; tooltip: "Logarithmic histogram"; onClicked: scopeDock.workspace.scopeLogHistogram = !active }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.gapCommand
                    Label { Layout.fillWidth: true; text: "Dock"; color: theme.textMuted; font.pixelSize: theme.typeLabel }
                    ScopeAction { objectName: scopeDock.objectPrefix + "ScopeBottomDockButton"; text: "Dock scopes below"; iconName: "panel-bottom"; active: scopeDock.workspace.scopeDockActive("bottom"); tooltip: text; onClicked: scopeDock.workspace.openScopesDock("bottom") }
                    ScopeAction { objectName: scopeDock.objectPrefix + "ScopeLeftDockButton"; text: "Dock scopes left"; iconName: "panel-left"; active: scopeDock.workspace.scopeDockActive("left"); tooltip: text; onClicked: scopeDock.workspace.openScopesDock("left") }
                    ScopeAction { objectName: scopeDock.objectPrefix + "ScopeRightDockButton"; text: "Dock scopes right"; iconName: "panel-right"; active: scopeDock.workspace.scopeDockActive("right"); tooltip: text; onClicked: scopeDock.workspace.openScopesDock("right") }
                    ScopeAction { objectName: scopeDock.objectPrefix + "ScopeLargeDockButton"; text: "Expand scopes"; iconName: "fullscreen"; active: scopeDock.workspace.scopesViewMode() === "large"; tooltip: text; onClicked: scopeDock.workspace.setScopesViewMode("large") }
                    ScopeAction {
                        objectName: scopeDock.objectPrefix + "ScopeLayoutButton"
                        visible: scopeDock.large
                        text: scopeDock.workspace.scopeLayoutPreset === "Quad" ? "Stack scopes" : "Quad scopes"
                        iconName: scopeDock.workspace.scopeLayoutPreset === "Quad" ? "list" : "grid"
                        tooltip: text
                        onClicked: scopeDock.workspace.setScopeLayoutPreset(scopeDock.workspace.scopeLayoutPreset === "Quad" ? "Stacked" : "Quad")
                    }
                }
            }
        }
    }
    StudioScopeGrid {
        Layout.fillWidth: true
        Layout.fillHeight: scopeDock.large || scopeDock.placement === "bottom"
        workspace: scopeDock.workspace
        previewController: scopeDock.previewController
        large: scopeDock.large
        horizontal: scopeDock.placement === "bottom"
        compact: scopeDock.compactPlots
        objectPrefix: scopeDock.objectPrefix
    }
}
