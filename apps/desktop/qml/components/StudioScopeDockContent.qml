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
    spacing: 8

    Theme { id: theme }

    RowLayout {
        Layout.fillWidth: true
        StudioSectionTitle { Layout.fillWidth: true; label: "Scopes" }
        Label {
            objectName: scopeDock.objectPrefix + "ScopeProofingStatusLabel"
            text: scopeDock.previewController.scopeDomainLabel
            color: scopeDock.previewController.softProofEnabled ? theme.accentCool400 : theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
            Layout.maximumWidth: scopeDock.width < 400 ? 130 : 320
        }
        MiniButton {
            objectName: "largeScopesImageModeButton"
            visible: scopeDock.large
            text: "Image"
            role: "secondary"
            tooltip: "Return to the photograph"
            onClicked: scopeDock.workspace.setScopesViewMode("image")
        }
        MiniButton {
            id: settingsButton
            objectName: scopeDock.objectPrefix + "ScopeSettingsButton"
            text: "Settings"
            iconName: "adjust"
            role: "secondary"
            tooltip: "Analysis domain, channels, sampling, and dock placement"
            active: scopeSettings.opened
            onClicked: scopeSettings.open()
        }
    }

    Label {
        objectName: scopeDock.objectPrefix + "ScopeAsyncStatusLabel"
        Layout.fillWidth: true
        text: (scopeDock.previewController.histogramUpdateActive ? "Updating · " : "")
              + scopeDock.workspace.proofingStatusText()
        color: scopeDock.previewController.histogramUpdateActive ? theme.interaction400 : theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 10
        wrapMode: Text.Wrap
    }

    Popup {
        id: scopeSettings
        objectName: scopeDock.objectPrefix + "ScopeSettingsPopup"
        parent: Overlay.overlay
        onAboutToShow: {
            const overlay = Overlay.overlay
            const anchor = settingsButton.mapToItem(overlay, settingsButton.width, settingsButton.height)
            x = Math.max(8, Math.min(overlay.width - width - 8, anchor.x - width))
            const below = anchor.y + 6
            y = below + height <= overlay.height - 8
                ? below : Math.max(8, anchor.y - settingsButton.height - height - 6)
        }
        width: 370
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: theme.surfaceTop
            radius: theme.radius8
            border.width: 1
            border.color: theme.controlEdge
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: "Analysis domain"; color: theme.textStrong; font.pixelSize: theme.typeLabel }
            Flow {
        id: scopeDomainFlow
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: 6

        MiniButton { objectName: scopeDock.objectPrefix + "ScopeSceneLinearDomainButton"; text: "Scene"; role: "segmented"; active: scopeDock.previewController.scopeDomain === "scene-linear"; tooltip: "Analyze characterized scene-linear pixels before creative edits."; onClicked: scopeDock.previewController.scopeDomain = "scene-linear" }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeWorkingDomainButton"; text: "Working"; role: "segmented"; active: scopeDock.previewController.scopeDomain === "working"; tooltip: "Analyze the complete adjusted working-space image before output rendering."; onClicked: scopeDock.previewController.scopeDomain = "working" }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopePreOutputDomainButton"; text: "Output"; role: "segmented"; active: scopeDock.previewController.scopeDomain === "pre-output"; tooltip: "Analyze the photographic output carrier before proof and device conversion."; onClicked: scopeDock.previewController.scopeDomain = "pre-output" }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeProofDomainButton"; text: "Proof"; role: "segmented"; active: scopeDock.previewController.scopeDomain === "proof"; enabled: scopeDock.previewController.softProofEnabled; disabledReason: "Enable Soft Proof to analyze the proof domain."; tooltip: "Analyze proof-simulated pixels before monitor conversion."; onClicked: scopeDock.previewController.scopeDomain = "proof" }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeFinalDisplayDomainButton"; text: "Display"; role: "segmented"; active: scopeDock.previewController.scopeDomain === "final-display"; tooltip: "Analyze final encoded pixels for the active display."; onClicked: scopeDock.previewController.scopeDomain = "final-display" }
    }

            Label { text: "Dock placement"; color: theme.textStrong; font.pixelSize: theme.typeLabel }
            Flow {
        id: scopeDockPlacementFlow
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: 6

        MiniButton { objectName: scopeDock.objectPrefix + "ScopeBottomDockButton"; width: Math.max(58, Math.min(implicitWidth, scopeDockPlacementFlow.width / 4 - 5)); height: implicitHeight; text: "Bottom"; role: "segmented"; active: scopeDock.workspace.scopeDockActive("bottom"); onClicked: scopeDock.workspace.openScopesDock("bottom") }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeLeftDockButton"; width: Math.max(48, Math.min(implicitWidth, scopeDockPlacementFlow.width / 4 - 5)); height: implicitHeight; text: "Left"; role: "segmented"; active: scopeDock.workspace.scopeDockActive("left"); onClicked: scopeDock.workspace.openScopesDock("left") }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeRightDockButton"; width: Math.max(54, Math.min(implicitWidth, scopeDockPlacementFlow.width / 4 - 5)); height: implicitHeight; text: "Right"; role: "segmented"; active: scopeDock.workspace.scopeDockActive("right"); onClicked: scopeDock.workspace.openScopesDock("right") }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeLargeDockButton"; width: Math.max(56, Math.min(implicitWidth, scopeDockPlacementFlow.width / 4 - 5)); height: implicitHeight; text: "Large"; role: "segmented"; active: scopeDock.workspace.scopesViewMode() === "large"; onClicked: scopeDock.workspace.setScopesViewMode("large") }
    }

            Label { text: "Channels & sampling"; color: theme.textStrong; font.pixelSize: theme.typeLabel }
            Flow {
        id: scopeDockToggleFlow
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: 6

        MiniButton { objectName: scopeDock.objectPrefix + "ScopeRgbToggle"; width: Math.max(52, Math.min(implicitWidth, scopeDockToggleFlow.width / 4 - 5)); height: implicitHeight; text: "RGB"; role: "segmented"; active: scopeDock.workspace.scopeShowRgb; onClicked: scopeDock.workspace.scopeShowRgb = !scopeDock.workspace.scopeShowRgb }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeLumaToggle"; width: Math.max(58, Math.min(implicitWidth, scopeDockToggleFlow.width / 4 - 5)); height: implicitHeight; text: "Luma"; role: "segmented"; active: scopeDock.workspace.scopeShowLuma; onClicked: scopeDock.workspace.scopeShowLuma = !scopeDock.workspace.scopeShowLuma }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeDensityToggle"; width: Math.max(70, Math.min(implicitWidth, scopeDockToggleFlow.width / 4 - 5)); height: implicitHeight; text: "Density"; role: "segmented"; active: scopeDock.workspace.scopeShowDensity; onClicked: scopeDock.workspace.scopeShowDensity = !scopeDock.workspace.scopeShowDensity }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeTraceToggle"; width: Math.max(58, Math.min(implicitWidth, scopeDockToggleFlow.width / 4 - 5)); height: implicitHeight; text: "Trace"; role: "segmented"; active: scopeDock.workspace.scopeShowTrace; onClicked: scopeDock.workspace.scopeShowTrace = !scopeDock.workspace.scopeShowTrace }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeLogToggle"; text: scopeDock.workspace.scopeLogHistogram ? "Log" : "Linear"; role: "segmented"; active: scopeDock.workspace.scopeLogHistogram; tooltip: "Toggle logarithmic histogram scale"; onClicked: scopeDock.workspace.scopeLogHistogram = !scopeDock.workspace.scopeLogHistogram }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeGridToggle"; width: Math.max(54, Math.min(implicitWidth, scopeDockToggleFlow.width / 4 - 5)); height: implicitHeight; text: "Grid"; role: "segmented"; active: scopeDock.workspace.scopeShowGrid; onClicked: scopeDock.workspace.scopeShowGrid = !scopeDock.workspace.scopeShowGrid }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeLayoutButton"; width: Math.max(72, Math.min(implicitWidth, scopeDockToggleFlow.width / 3 - 5)); height: implicitHeight; text: scopeDock.workspace.scopeLayoutPreset; role: "segmented"; onClicked: scopeDock.workspace.setScopeLayoutPreset(scopeDock.workspace.scopeLayoutPreset === "Stacked" ? "Quad" : scopeDock.workspace.scopeLayoutPreset === "Quad" ? "Tall" : "Stacked") }
        MiniButton { objectName: scopeDock.objectPrefix + "ScopeSamplingButton"; width: Math.max(78, Math.min(implicitWidth, scopeDockToggleFlow.width / 3 - 5)); height: implicitHeight; text: scopeDock.workspace.scopeSamplingMode; role: "segmented"; onClicked: scopeDock.workspace.setScopeSamplingMode(scopeDock.workspace.scopeSamplingMode === "Fast" ? "Balanced" : scopeDock.workspace.scopeSamplingMode === "Balanced" ? "Precise" : "Fast") }
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
        objectPrefix: scopeDock.objectPrefix
    }
}
