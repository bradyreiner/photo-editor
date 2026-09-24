import QtQuick
import QtQuick.Layouts

GridLayout {
    id: grid

    required property Item workspace
    required property var previewController
    property string objectPrefix: ""
    property bool large: false
    property bool horizontal: false
    readonly property int effectiveColumns: columns

    objectName: objectPrefix.length > 0 ? objectPrefix + "ScopeGrid" : (large ? "largeScopeGrid" : "inspectorScopeGrid")
    Layout.fillWidth: true
    columns: horizontal ? (width >= 760 ? 4 : 2) : (large && workspace.scopeLayoutPreset === "Quad" ? 2 : 1)
    columnSpacing: 10
    rowSpacing: 10

    Repeater {
        model: grid.workspace.scopeAnalysisModes()

        StudioScopeTile {
            required property string modelData
            workspace: grid.workspace
            previewController: grid.previewController
            kind: modelData
            large: grid.large
            Layout.fillHeight: grid.large || grid.horizontal
        }
    }
}
