import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

InspectorSection {
    id: group

    required property Item workspace
    property string resetGroup: ""
    property string compareGroup: ""

    Theme { id: groupTheme }

    Layout.fillWidth: true
    accent: workspace.mint
    showAccentMarker: false
    uppercaseTitle: false
    titleFontFamily: groupTheme.fontUi
    titlePixelSize: groupTheme.typeLabel
    titleWeight: groupTheme.weightSection
    beforeAfterAvailable: compareGroup.length > 0
    beforeAfterActive: workspace.sectionComparisonActive(compareGroup)
    beforeAfterTooltip: beforeAfterActive
                        ? "Restore the current edits. Click to toggle; hold to compare temporarily."
                        : (compareGroup === "all-adjustments"
                           ? "Compare all adjustments on this target. Click to toggle; hold temporarily."
                           : "Compare this section on the selected target only. Click to toggle; hold temporarily.")
    resetAvailable: resetGroup.length > 0
    resetTooltip: workspace.activeAdjustmentLayerIndex() >= 0
                  ? "Reset this panel on the active layer only."
                  : "Reset this panel only."

    onVisibleChanged: {
        if (!visible && workspace.sectionComparisonActive(compareGroup))
            workspace.toggleSectionComparison(compareGroup)
    }
    onBeforeAfterRequested: {
        workspace.toggleSectionComparison(compareGroup)
    }
    onBeforeAfterHoldRequested: function(held) {
        workspace.holdSectionComparison(compareGroup, held)
    }
    onResetRequested: workspace.resetPrimaryAdjustmentGroup(resetGroup)
}
